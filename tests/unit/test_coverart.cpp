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

#include "fixture.h"

#if MCS_COVER_ART

#include "CoverArtFetcher.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace {

bool v4(const char* address)
{
    struct sockaddr_in in = {};
    in.sin_family = AF_INET;
    EXPECT_EQ(inet_pton(AF_INET, address, &in.sin_addr), 1) << address;
    return CoverArtFetcher::publicAddress(reinterpret_cast<struct sockaddr*>(&in), sizeof(in));
}

bool v6(const char* address)
{
    struct sockaddr_in6 in = {};
    in.sin6_family = AF_INET6;
    EXPECT_EQ(inet_pton(AF_INET6, address, &in.sin6_addr), 1) << address;
    return CoverArtFetcher::publicAddress(reinterpret_cast<struct sockaddr*>(&in), sizeof(in));
}

TEST(CoverArt, OnlyHttpAndHttpsUrlsWithAHostAndNoCredentials)
{
    for (const char* ok : {"http://example.com/a.png", "https://example.com/a/b.jpg?x=1", "HTTPS://Example.com/"})
        EXPECT_TRUE(CoverArtFetcher::validUrl(ok)) << ok;
    for (const char* bad : {"", "file:///etc/passwd", "ftp://example.com/a", "gopher://x/", "http://",
                            "https://user:pw@example.com/a.png", "https://user@example.com/a.png",
                            "/media/internal/a.png", "example.com/a.png", "javascript:alert(1)"})
        EXPECT_FALSE(CoverArtFetcher::validUrl(bad)) << bad;
    EXPECT_FALSE(CoverArtFetcher::validUrl("https://example.com/" + std::string(CoverArtFetcher::MAX_URL_LENGTH, 'a')));
}

TEST(CoverArt, FileNamesAreTheUrlsHashNotItsText)
{
    std::string a = CoverArtFetcher::fileNameFor("https://example.com/art/cover.PNG?size=1");
    std::string b = CoverArtFetcher::fileNameFor("https://example.com/art/cover.PNG?size=2");
    EXPECT_NE(a, b) << "two URLs share a file";
    EXPECT_EQ(a.size(), 64u + 4u);
    EXPECT_EQ(a.substr(64), ".png");
    for (const char* hostile : {"https://x/../../etc/passwd", "https://x/a.png%2F..%2F..%2Fb",
                                "https://x/a.ph/p", "https://x/a.toolongext", "https://x/a.p-g"}) {
        std::string name = CoverArtFetcher::fileNameFor(hostile);
        EXPECT_EQ(name.find('/'), std::string::npos) << hostile;
        EXPECT_EQ(name.find(".."), std::string::npos) << hostile;
        EXPECT_LE(name.size(), 64u + 6u) << hostile;
    }
}

TEST(CoverArt, LogsLeaveSecretsOut)
{
    EXPECT_EQ(CoverArtFetcher::redact("https://example.com:8443/a.png?token=secret#frag"),
              "https://example.com:8443/a.png");
    EXPECT_EQ(CoverArtFetcher::redact("http://u:p@example.com/a"), "http://example.com/a");
}

TEST(CoverArt, LocalAndPrivateAddressesAreRefused)
{
    for (const char* bad : {"127.0.0.1", "10.1.2.3", "172.16.0.1", "172.31.255.255", "192.168.1.1",
                            "169.254.1.1", "100.64.0.1", "0.0.0.0", "224.0.0.1", "255.255.255.255",
                            "192.0.2.1", "198.18.0.1"})
        EXPECT_FALSE(v4(bad)) << bad;
    for (const char* ok : {"8.8.8.8", "172.32.0.1", "100.128.0.1", "93.184.216.34"})
        EXPECT_TRUE(v4(ok)) << ok;
    for (const char* bad : {"::1", "::", "fe80::1", "fc00::1", "fd12::1", "ff02::1", "::ffff:127.0.0.1",
                            "::ffff:192.168.0.1", "64:ff9b::a00:1", "2001:db8::1"})
        EXPECT_FALSE(v6(bad)) << bad;
    for (const char* ok : {"2a00:1450:4001::1", "::ffff:8.8.8.8"})
        EXPECT_TRUE(v6(ok)) << ok;
}

typedef McsTest CoverArtMethod;

TEST_F(CoverArtMethod, RefusesWhatItWillNotFetch)
{
    start();
    session("m1", "com.one");
    for (const char* src : {"[\"file:///etc/shadow\"]", "[\"\"]", "[5]", "[]",
                            "[\"https://a/1\",\"https://a/2\",\"https://a/3\",\"https://a/4\","
                            "\"https://a/5\",\"https://a/6\",\"https://a/7\",\"https://a/8\",\"https://a/9\"]"})
        EXPECT_EQ(errorCode(call("getMediaCoverArtPath", std::string("{\"displayId\":0,\"src\":") + src + "}")),
                  MCS_ERROR_PARSING_FAILED) << src;
}

TEST_F(CoverArtMethod, NeedsAnActiveSession)
{
    start();
    EXPECT_EQ(errorCode(call("getMediaCoverArtPath", "{\"displayId\":0,\"src\":[\"https://example.com/a.png\"]}")),
              MCS_ERROR_NO_ACTIVE_SESSION);
}

/*
 * An HTTP server on loopback, for the fetcher to fetch from: each path is
 * one canned answer.
 *   /ok.png        200, "PNGDATA"
 *   /missing.png   404
 *   /large.png     200, a body over MAX_FILE_SIZE
 *   /file          302 to file:///etc/passwd
 *   /ftp           302 to ftp:// on this server
 *   /slow.png      200, one byte a second for a minute
 */
class Server
{
public:
    Server()
    {
        m_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        struct sockaddr_in in = {};
        in.sin_family = AF_INET;
        in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(m_fd, reinterpret_cast<struct sockaddr*>(&in), sizeof(in));
        socklen_t len = sizeof(in);
        getsockname(m_fd, reinterpret_cast<struct sockaddr*>(&in), &len);
        m_port = ntohs(in.sin_port);
        listen(m_fd, 8);
        m_thread = std::thread([this] { serve(); });
    }
    ~Server()
    {
        m_stop = true;
        shutdown(m_fd, SHUT_RDWR);
        close(m_fd);
        m_thread.join();
    }
    int connections() const { return m_connections; }
    std::string url(const std::string& path) const
    {
        return "http://127.0.0.1:" + std::to_string(m_port) + path;
    }

private:
    void serve()
    {
        while (!m_stop) {
            int c = accept4(m_fd, nullptr, nullptr, SOCK_CLOEXEC);
            if (c < 0)
                return;
            ++m_connections;
            // a client that sends nothing (FTP waits for the server to
            // speak first) must not hold the server forever
            struct timeval timeout = {2, 0};
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            std::string request;
            char buf[1024];
            while (request.find("\r\n\r\n") == std::string::npos) {
                ssize_t n = recv(c, buf, sizeof(buf), 0);
                if (n <= 0)
                    break;
                request.append(buf, static_cast<size_t>(n));
            }
            std::string path = request.size() > 4 ? request.substr(4, request.find(' ', 4) - 4) : "";
            path = path.substr(0, path.find('?')); // the query only makes URLs differ
            if (path == "/ok.png") {
                std::string r = "HTTP/1.1 200 OK\r\nContent-Length: 7\r\nConnection: close\r\n\r\nPNGDATA";
                send(c, r.data(), r.size(), MSG_NOSIGNAL);
            } else if (path == "/large.png") {
                std::string head = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n";
                send(c, head.data(), head.size(), MSG_NOSIGNAL);
                std::string chunk(64 * 1024, 'x');
                for (size_t sent = 0; sent <= CoverArtFetcher::MAX_FILE_SIZE && !m_stop; sent += chunk.size())
                    if (send(c, chunk.data(), chunk.size(), MSG_NOSIGNAL) < 0)
                        break;
            } else if (path == "/file") {
                std::string r = "HTTP/1.1 302 Found\r\nLocation: file:///etc/passwd\r\nContent-Length: 0\r\n"
                                "Connection: close\r\n\r\n";
                send(c, r.data(), r.size(), MSG_NOSIGNAL);
            } else if (path == "/ftp") {
                std::string r = "HTTP/1.1 302 Found\r\nLocation: ftp://127.0.0.1:" + std::to_string(m_port) +
                                "/x.png\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send(c, r.data(), r.size(), MSG_NOSIGNAL);
            } else if (path == "/slow.png") {
                std::string head = "HTTP/1.1 200 OK\r\nContent-Length: 60\r\nConnection: close\r\n\r\n";
                send(c, head.data(), head.size(), MSG_NOSIGNAL);
                for (int i = 0; i < 60 && !m_stop; i++) {
                    if (send(c, "x", 1, MSG_NOSIGNAL) < 0)
                        break;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            } else {
                std::string r = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nnot found";
                send(c, r.data(), r.size(), MSG_NOSIGNAL);
            }
            close(c);
        }
    }

    int m_fd = -1;
    int m_port = 0;
    std::atomic<int> m_connections{0};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

class Fetcher : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // a cache of the test's own, under $TMPDIR; removed in TearDown
        gchar* dir = g_dir_make_tmp("mcs-coverart-XXXXXX", nullptr);
        ASSERT_NE(dir, nullptr);
        m_dir = dir;
        g_free(dir);
        m_loop = g_main_loop_new(nullptr, FALSE);
    }
    void TearDown() override
    {
        for (const std::string& f : files())
            unlink((m_dir + "/" + f).c_str());
        rmdir(m_dir.c_str());
        g_main_loop_unref(m_loop);
    }

    std::vector<std::string> files()
    {
        std::vector<std::string> out;
        DIR* d = opendir(m_dir.c_str());
        while (struct dirent* e = d ? readdir(d) : nullptr) {
            if (std::string(e->d_name) != "." && std::string(e->d_name) != "..")
                out.push_back(e->d_name);
        }
        if (d)
            closedir(d);
        return out;
    }

    //! Fetches url; the path it reports (empty when it failed).
    std::string fetch(CoverArtFetcher& fetcher, const std::string& url)
    {
        m_results.clear();
        unsigned id = fetcher.fetch(url);
        EXPECT_NE(id, 0u) << url;
        if (!id)
            return std::string();
        guint timeout = g_timeout_add_seconds(60, [](gpointer loop) {
            g_main_loop_quit(static_cast<GMainLoop*>(loop));
            return G_SOURCE_REMOVE;
        }, m_loop);
        g_main_loop_run(m_loop);
        g_source_remove(timeout);
        EXPECT_EQ(m_results.size(), 1u);
        EXPECT_EQ(m_results.empty() ? 0u : m_results[0].id, id);
        return m_results.empty() ? std::string() : m_results[0].path;
    }

    CoverArtFetcher::Done done()
    {
        return [this](const CoverArtFetcher::Result& r) {
            m_results.push_back(r);
            g_main_loop_quit(m_loop);
        };
    }

    std::string m_dir;
    GMainLoop* m_loop = nullptr;
    std::vector<CoverArtFetcher::Result> m_results;
};

TEST_F(Fetcher, FetchesIntoTheCacheUnderTheUrlsHash)
{
    Server server;
    CoverArtFetcher fetcher(m_dir, done());
    fetcher.allowAnyAddress(true);
    std::string path = fetch(fetcher, server.url("/ok.png"));
    EXPECT_EQ(path, fetcher.pathFor(server.url("/ok.png")));
    std::ifstream in(path);
    std::stringstream body;
    body << in.rdbuf();
    EXPECT_EQ(body.str(), "PNGDATA");
    EXPECT_EQ(files().size(), 1u) << "a temporary file was left";
    // fetched again, it comes from the cache
    EXPECT_EQ(fetch(fetcher, server.url("/ok.png")), path);
}

TEST_F(Fetcher, FailuresLeaveNothingBehind)
{
    Server server;
    CoverArtFetcher fetcher(m_dir, done());
    fetcher.allowAnyAddress(true);
    EXPECT_EQ(fetch(fetcher, server.url("/missing.png")), "") << "an HTTP error was stored as the image";
    EXPECT_EQ(fetch(fetcher, server.url("/large.png")), "") << "a file over the limit was kept";
    EXPECT_EQ(fetch(fetcher, server.url("/file")), "") << "a redirect to file:// was followed";
    EXPECT_TRUE(files().empty());
}

// curl's own defaults refuse file://, but would follow a redirect to ftp:
// a followed redirect is a second connection to the server.
TEST_F(Fetcher, RedirectsGoNowhereButHttpAndHttps)
{
    Server server;
    CoverArtFetcher fetcher(m_dir, done());
    fetcher.allowAnyAddress(true);
    EXPECT_EQ(fetch(fetcher, server.url("/ftp")), "");
    EXPECT_EQ(server.connections(), 1) << "a redirect to ftp:// was followed";
    EXPECT_TRUE(files().empty());
}

TEST_F(Fetcher, LoopbackIsRefusedUnlessAllowed)
{
    Server server;
    CoverArtFetcher fetcher(m_dir, done());
    auto started = std::chrono::steady_clock::now();
    EXPECT_EQ(fetch(fetcher, server.url("/ok.png")), "");
    EXPECT_TRUE(files().empty());
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(1))
        << "a refused address was tried again";
}

TEST_F(Fetcher, RefusesWhatItDoesNotFetchAndTooMuchWaiting)
{
    CoverArtFetcher fetcher(m_dir, done());
    EXPECT_EQ(fetcher.fetch("file:///etc/passwd"), 0u);
    EXPECT_EQ(fetcher.pathFor("file:///etc/passwd"), "");
    Server server;
    fetcher.allowAnyAddress(true);
    // the first is taken and held by the slow server; the rest wait
    ASSERT_NE(fetcher.fetch(server.url("/slow.png?first")), 0u);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    size_t accepted = 0;
    for (size_t i = 0; i < CoverArtFetcher::MAX_PENDING + 5; i++)
        accepted += fetcher.fetch(server.url("/slow.png?" + std::to_string(i))) != 0;
    EXPECT_EQ(accepted, CoverArtFetcher::MAX_PENDING);
}

TEST_F(Fetcher, EmptiesTheCacheAtStartAndStopsATransferWhenDestroyed)
{
    std::ofstream(m_dir + "/stale.png") << "old";
    Server server;
    auto started = std::chrono::steady_clock::now();
    {
        CoverArtFetcher fetcher(m_dir, done());
        EXPECT_TRUE(files().empty()) << "what an earlier run left was kept";
        fetcher.allowAnyAddress(true);
        fetcher.fetch(server.url("/slow.png"));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(10))
        << "the destructor waited for the transfer";
    EXPECT_TRUE(files().empty()) << "a partial file was left";
    // nothing is delivered for a fetcher that is gone
    while (g_main_context_iteration(nullptr, FALSE)) {}
    EXPECT_TRUE(m_results.empty());
}

} // namespace

#endif
