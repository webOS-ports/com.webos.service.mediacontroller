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

#ifndef COVER_ART_FETCHER_H_
#define COVER_ART_FETCHER_H_

#include <glib.h>
#include <sys/socket.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/*
 * Fetches cover art over http and https into a cache directory, one URL at
 * a time on a worker thread of its own, and reports each result on the
 * main loop.
 *
 * What a caller can make it fetch is bounded: http and https only, also
 * after a redirect, at most five redirects, no proxy, nothing on a
 * loopback, private, link-local or multicast address (checked on the
 * address actually connected to, so a name that resolves there is refused
 * too), a connect timeout and an overall one, at most MAX_FILE_SIZE bytes a
 * file and MAX_CACHE_SIZE in all, MAX_PENDING URLs waiting. A file is
 * written under a temporary name and renamed into place once complete;
 * its name is the SHA-256 of the URL, so a URL cannot choose it.
 */
class CoverArtFetcher
{
public:
  static constexpr size_t MAX_FILE_SIZE = 4 * 1024 * 1024;
  static constexpr size_t MAX_CACHE_SIZE = 10 * 1024 * 1024;
  static constexpr size_t MAX_PENDING = 32;
  static constexpr size_t MAX_URL_LENGTH = 2048;
  static constexpr int MAX_ATTEMPTS = 3;

  struct Result {
    unsigned id;
    std::string url;
    std::string path; //!< empty when it failed
  };
  using Done = std::function<void(const Result &)>;

  //! dir: the cache, emptied of what an earlier run left there. done is
  //! called on the main loop of the default context, once per fetch().
  CoverArtFetcher(std::string dir, Done done);
  ~CoverArtFetcher();

  CoverArtFetcher(const CoverArtFetcher &) = delete;
  CoverArtFetcher &operator=(const CoverArtFetcher &) = delete;

  //! Queues url; its id, or 0 when it is refused (not a URL it fetches,
  //! too many waiting, or no usable cache directory).
  unsigned fetch(const std::string &url);

  //! Where url is, or will be, once fetched; empty if it is not a URL it
  //! fetches.
  std::string pathFor(const std::string &url) const;

  //! http or https, with a host, no user name or password, not too long.
  static bool validUrl(const std::string &url);
  //! The SHA-256 of url in hex, with the extension of its path's last
  //! segment if that is one to five letters and digits.
  static std::string fileNameFor(const std::string &url);
  //! url without what may be secret - user, password, query, fragment -
  //! for the logs.
  static std::string redact(const std::string &url);
  //! False for an address no fetch may connect to.
  static bool publicAddress(const struct sockaddr *address, socklen_t length);

  //! For tests: connect to loopback and private addresses as well.
  void allowAnyAddress(bool allow) { anyAddress_ = allow; }

private:
  struct Job {
    unsigned id;
    std::string url;
  };
  struct Cached {
    std::string path;
    size_t size;
  };

  void run();
  std::string download(const std::string &url);
  bool attempt(const std::string &url, const std::string &target, bool &transient);
  void remember(const std::string &url, const std::string &path, size_t size);
  static gboolean drain(gpointer self);

  std::string dir_;
  bool curlReady_ = false;
  bool usable_ = false;
  Done done_;
  std::atomic<bool> anyAddress_{false};

  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  std::vector<Result> results_;
  guint drainSource_ = 0;
  std::atomic<bool> stopping_{false};
  unsigned lastId_ = 0;

  // The worker's alone.
  std::map<std::string, Cached> cache_;
  std::list<std::string> lru_;
  size_t cacheSize_ = 0;

  std::thread worker_;
};

#endif /*COVER_ART_FETCHER_H_*/
