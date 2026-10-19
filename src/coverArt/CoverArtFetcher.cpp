// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "CoverArtFetcher.h"
#include "MediaControlTypes.h"

#include <curl/curl.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstring>

namespace {

/* curl's URL parser, freed with its handle. */
struct Url {
  CURLU *handle = curl_url();
  ~Url() { curl_url_cleanup(handle); }

  bool parse(const std::string &url) {
    return handle && curl_url_set(handle, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK;
  }
  std::string part(CURLUPart what) const {
    char *value = nullptr;
    if (curl_url_get(handle, what, &value, 0) != CURLUE_OK || !value)
      return std::string();
    std::string copy(value);
    curl_free(value);
    return copy;
  }
  bool has(CURLUPart what) const {
    char *value = nullptr;
    CURLUcode rc = curl_url_get(handle, what, &value, 0);
    curl_free(value);
    return rc == CURLUE_OK;
  }
};

/* What a transfer writes to: the temporary file, and how much is in it. */
struct Sink {
  int fd;
  size_t written;
  bool tooLarge;
};

size_t writeData(char *data, size_t size, size_t count, void *userp) {
  Sink *sink = static_cast<Sink *>(userp);
  size_t length = size * count;
  if (length > CoverArtFetcher::MAX_FILE_SIZE - sink->written) {
    sink->tooLarge = true;
    return 0;
  }
  for (size_t done = 0; done < length;) {
    ssize_t n = write(sink->fd, data + done, length - done);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return 0;
    }
    done += static_cast<size_t>(n);
  }
  sink->written += length;
  return length;
}

struct Transfer {
  const std::atomic<bool> *stopping;
  bool anyAddress;
  bool refused; //!< an address was refused: no point trying again
};

int progress(void *userp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  // non-zero aborts the transfer: the fetcher is going away
  return static_cast<Transfer *>(userp)->stopping->load() ? 1 : 0;
}

curl_socket_t openSocket(void *userp, curlsocktype purpose, struct curl_sockaddr *address) {
  Transfer *transfer = static_cast<Transfer *>(userp);
  if (purpose != CURLSOCKTYPE_IPCXN)
    return CURL_SOCKET_BAD;
  if (!transfer->anyAddress && !CoverArtFetcher::publicAddress(&address->addr, address->addrlen)) {
    transfer->refused = true;
    PMLOG_ERROR(CONST_MODULE_MCD, "cover art: refusing to connect to a local or private address");
    return CURL_SOCKET_BAD;
  }
  return socket(address->family, address->socktype | SOCK_CLOEXEC, address->protocol);
}

bool transientError(CURLcode rc, long status) {
  switch (rc) {
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_PARTIAL_FILE:
      return true;
    case CURLE_HTTP_RETURNED_ERROR:
      return status == 429 || status >= 500;
    default:
      return false;
  }
}

bool ipv4Public(const unsigned char *a) {
  if (a[0] == 0 || a[0] == 10 || a[0] == 127)                  // this network, private, loopback
    return false;
  if (a[0] == 100 && (a[1] & 0xc0) == 64)                       // carrier-grade NAT
    return false;
  if (a[0] == 169 && a[1] == 254)                                // link-local
    return false;
  if (a[0] == 172 && (a[1] & 0xf0) == 16)                        // private
    return false;
  if (a[0] == 192 && a[1] == 168)                                // private
    return false;
  if (a[0] == 192 && a[1] == 0 && (a[2] == 0 || a[2] == 2))      // IETF, documentation
    return false;
  if (a[0] == 198 && (a[1] & 0xfe) == 18)                        // benchmarking
    return false;
  if (a[0] >= 224)                                               // multicast, reserved, broadcast
    return false;
  return true;
}

} // namespace

bool CoverArtFetcher::publicAddress(const struct sockaddr *address, socklen_t length) {
  if (address->sa_family == AF_INET && length >= sizeof(struct sockaddr_in)) {
    const struct sockaddr_in *in = reinterpret_cast<const struct sockaddr_in *>(address);
    return ipv4Public(reinterpret_cast<const unsigned char *>(&in->sin_addr.s_addr));
  }
  if (address->sa_family == AF_INET6 && length >= sizeof(struct sockaddr_in6)) {
    const unsigned char *a = reinterpret_cast<const struct sockaddr_in6 *>(address)->sin6_addr.s6_addr;
    static const unsigned char mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    static const unsigned char nat64[12] = {0, 0x64, 0xff, 0x9b, 0, 0, 0, 0, 0, 0, 0, 0};
    if (memcmp(a, mapped, 12) == 0 || memcmp(a, nat64, 12) == 0)
      return ipv4Public(a + 12);
    bool zeroPrefix = true;
    for (int i = 0; i < 15; i++)
      zeroPrefix = zeroPrefix && a[i] == 0;
    if (zeroPrefix)                                              // :: and ::1
      return false;
    if ((a[0] & 0xfe) == 0xfc)                                   // unique local
      return false;
    if (a[0] == 0xfe && (a[1] & 0xc0) == 0x80)                   // link-local
      return false;
    if (a[0] == 0xff)                                            // multicast
      return false;
    if (a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x0d && a[3] == 0xb8)  // documentation
      return false;
    return true;
  }
  return false;
}

bool CoverArtFetcher::validUrl(const std::string &url) {
  if (url.empty() || url.size() > MAX_URL_LENGTH)
    return false;
  Url parsed;
  if (!parsed.parse(url))
    return false;
  std::string scheme = parsed.part(CURLUPART_SCHEME);
  if (scheme != "http" && scheme != "https")
    return false;
  if (parsed.part(CURLUPART_HOST).empty())
    return false;
  return !parsed.has(CURLUPART_USER) && !parsed.has(CURLUPART_PASSWORD);
}

std::string CoverArtFetcher::fileNameFor(const std::string &url) {
  gchar *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, url.c_str(),
                                                static_cast<gssize>(url.size()));
  std::string name(digest ? digest : "");
  g_free(digest);

  Url parsed;
  if (!parsed.parse(url))
    return name;
  std::string path = parsed.part(CURLUPART_PATH);
  std::string::size_type slash = path.rfind('/');
  std::string last = slash == std::string::npos ? path : path.substr(slash + 1);
  std::string::size_type dot = last.rfind('.');
  if (dot == std::string::npos)
    return name;
  std::string extension = last.substr(dot + 1);
  if (extension.empty() || extension.size() > 5)
    return name;
  for (char &c : extension) {
    if (!isalnum(static_cast<unsigned char>(c)))
      return name;
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  }
  return name + "." + extension;
}

std::string CoverArtFetcher::redact(const std::string &url) {
  Url parsed;
  if (!parsed.parse(url))
    return "(not a URL)";
  std::string port = parsed.part(CURLUPART_PORT);
  return parsed.part(CURLUPART_SCHEME) + "://" + parsed.part(CURLUPART_HOST) +
         (port.empty() ? "" : ":" + port) + parsed.part(CURLUPART_PATH);
}

/* Removes the regular files in dir, nothing else. */
static void emptyDirectory(int dirfd) {
  int copy = dup(dirfd);
  if (copy < 0)
    return;
  DIR *dir = fdopendir(copy);
  if (!dir) {
    close(copy);
    return;
  }
  while (struct dirent *entry = readdir(dir)) {
    struct stat info;
    if (fstatat(dirfd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode))
      unlinkat(dirfd, entry->d_name, 0);
  }
  closedir(dir);
}

CoverArtFetcher::CoverArtFetcher(std::string dir, Done done)
  : dir_(std::move(dir)), done_(std::move(done)) {
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    PMLOG_ERROR(CONST_MODULE_MCD, "%s curl_global_init failed", __FUNCTION__);
    return;
  }
  curlReady_ = true;

  // The cache is created by systemd (CacheDirectory=); make it if it is not.
  if (mkdir(dir_.c_str(), 0700) != 0 && errno != EEXIST)
    PMLOG_ERROR(CONST_MODULE_MCD, "%s cannot create %s: %s", __FUNCTION__, dir_.c_str(), strerror(errno));
  int fd = open(dir_.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) {
    PMLOG_ERROR(CONST_MODULE_MCD, "%s %s is not a directory to use: %s", __FUNCTION__, dir_.c_str(),
                strerror(errno));
  } else {
    emptyDirectory(fd);
    close(fd);
    usable_ = true;
  }
  worker_ = std::thread(&CoverArtFetcher::run, this);
}

CoverArtFetcher::~CoverArtFetcher() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (worker_.joinable())
    worker_.join();
  // Nothing adds a drain source any more; one still pending must not run.
  if (drainSource_)
    g_source_remove(drainSource_);
  if (curlReady_)
    curl_global_cleanup();
}

unsigned CoverArtFetcher::fetch(const std::string &url) {
  if (!usable_ || !validUrl(url))
    return 0;
  std::lock_guard<std::mutex> lock(mutex_);
  if (jobs_.size() >= MAX_PENDING) {
    PMLOG_ERROR(CONST_MODULE_MCD, "%s %zu waiting already, refusing %s", __FUNCTION__,
                jobs_.size(), redact(url).c_str());
    return 0;
  }
  if (++lastId_ == 0)
    ++lastId_;
  jobs_.push_back({lastId_, url});
  wake_.notify_one();
  return lastId_;
}

std::string CoverArtFetcher::pathFor(const std::string &url) const {
  if (!validUrl(url))
    return std::string();
  return dir_ + "/" + fileNameFor(url);
}

void CoverArtFetcher::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
    if (stopping_)
      return;
    Job job = std::move(jobs_.front());
    jobs_.pop_front();
    lock.unlock();
    std::string path = download(job.url);
    lock.lock();
    results_.push_back({job.id, std::move(job.url), std::move(path)});
    if (!drainSource_)
      drainSource_ = g_idle_add(&CoverArtFetcher::drain, this);
  }
}

gboolean CoverArtFetcher::drain(gpointer self) {
  CoverArtFetcher *fetcher = static_cast<CoverArtFetcher *>(self);
  std::vector<Result> results;
  {
    std::lock_guard<std::mutex> lock(fetcher->mutex_);
    results.swap(fetcher->results_);
    fetcher->drainSource_ = 0;
  }
  for (const Result &result : results)
    fetcher->done_(result);
  return G_SOURCE_REMOVE;
}

std::string CoverArtFetcher::download(const std::string &url) {
  std::string target = pathFor(url);
  auto cached = cache_.find(url);
  if (cached != cache_.end()) {
    struct stat info;
    if (stat(cached->second.path.c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
      lru_.remove(url);
      lru_.push_front(url);
      return cached->second.path;
    }
    cacheSize_ -= cached->second.size;
    lru_.remove(url);
    cache_.erase(cached);
  }

  for (int attemptNo = 1; attemptNo <= MAX_ATTEMPTS && !stopping_; attemptNo++) {
    bool transient = false;
    if (attempt(url, target, transient))
      return target;
    if (!transient)
      break;
    // a second, then two, unless the fetcher is going away
    std::unique_lock<std::mutex> lock(mutex_);
    wake_.wait_for(lock, std::chrono::seconds(attemptNo), [this] { return stopping_.load(); });
  }
  PMLOG_ERROR(CONST_MODULE_MCD, "%s %s could not be fetched", __FUNCTION__, redact(url).c_str());
  return std::string();
}

bool CoverArtFetcher::attempt(const std::string &url, const std::string &target, bool &transient) {
  std::string temporary = dir_ + "/.partXXXXXX";
  std::vector<char> name(temporary.begin(), temporary.end());
  name.push_back('\0');
  int fd = mkostemp(name.data(), O_CLOEXEC);
  if (fd < 0) {
    PMLOG_ERROR(CONST_MODULE_MCD, "%s no temporary file in %s: %s", __FUNCTION__, dir_.c_str(), strerror(errno));
    return false;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    close(fd);
    unlink(name.data());
    return false;
  }

  Sink sink = {fd, 0, false};
  Transfer transfer = {&stopping_, anyAddress_.load(), false};
  bool set =
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str()) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https") == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_PROXY, "") == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(MAX_FILE_SIZE)) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2)) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeData) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, &openSocket) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, &transfer) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &progress) == CURLE_OK &&
      curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer) == CURLE_OK;

  CURLcode rc = set ? curl_easy_perform(curl) : CURLE_FAILED_INIT;
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(curl);

  bool ok = rc == CURLE_OK && status >= 200 && status < 300 && sink.written > 0;
  if (close(fd) != 0)
    ok = false;
  if (ok && rename(name.data(), target.c_str()) != 0) {
    PMLOG_ERROR(CONST_MODULE_MCD, "%s rename failed: %s", __FUNCTION__, strerror(errno));
    ok = false;
  }
  if (!ok) {
    unlink(name.data());
    transient = !sink.tooLarge && !transfer.refused && transientError(rc, status);
    PMLOG_ERROR(CONST_MODULE_MCD, "%s %s: %s, HTTP %ld%s", __FUNCTION__, redact(url).c_str(),
                curl_easy_strerror(rc), status, sink.tooLarge ? ", too large" : "");
    return false;
  }

  remember(url, target, sink.written);
  PMLOG_INFO(CONST_MODULE_MCD, "%s %s: %zu bytes", __FUNCTION__, redact(url).c_str(), sink.written);
  return true;
}

void CoverArtFetcher::remember(const std::string &url, const std::string &path, size_t size) {
  auto known = cache_.find(url);
  if (known != cache_.end()) {
    cacheSize_ -= known->second.size;
    lru_.remove(url);
  }
  cache_[url] = {path, size};
  lru_.push_front(url);
  cacheSize_ += size;

  // the least recently used go until it fits, never the one just fetched
  while (cacheSize_ > MAX_CACHE_SIZE && lru_.size() > 1) {
    std::string oldest = lru_.back();
    lru_.pop_back();
    auto entry = cache_.find(oldest);
    if (entry == cache_.end())
      continue;
    unlink(entry->second.path.c_str());
    cacheSize_ -= entry->second.size;
    cache_.erase(entry);
  }
}
