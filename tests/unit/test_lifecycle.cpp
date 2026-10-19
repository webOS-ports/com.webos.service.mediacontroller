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

#include <glib-unix.h>
#include <signal.h>

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

int runMediaControlService();

namespace {

typedef McsTest Lifecycle;

std::string sourceDir()
{
    const char* dir = g_getenv("MCS_SOURCE_DIR");
    return dir ? dir : MCS_SOURCE_DIR;
}

std::string readFile(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

//! The methods an api.json.in lists, as this build configures it.
std::set<std::string> apiMethods(const std::string& path)
{
    std::string text = readFile(path);
    EXPECT_FALSE(text.empty()) << path;
    text = std::regex_replace(text, std::regex("@MCS_API_COVER_ART_PATH@"),
        MCS_COVER_ART ? ",\"com.webos.service.mediacontroller/getMediaCoverArtPath\"" : "");
#if USE_TEST_METHOD
    text = std::regex_replace(text, std::regex("@MCS_API_TEST_KEY_EVENT@"),
        ",\"com.webos.service.mediacontroller/testKeyEvent\"");
#else
    text = std::regex_replace(text, std::regex("@MCS_API_TEST_KEY_EVENT@"), "");
#endif
    pbnjson::JValue api = json(text);
    EXPECT_TRUE(api.isObject()) << path << " is not JSON";
    std::set<std::string> methods;
    for (auto group : api.children()) {
        for (int i = 0; i < group.second.arraySize(); i++) {
            std::string m = group.second[i].asString();
            EXPECT_TRUE(methods.insert(m.substr(m.find('/') + 1)).second) << m << " is in two groups";
        }
    }
    return methods;
}

TEST_F(Lifecycle, RegistersTheServiceAndItsMethods)
{
    start();
    EXPECT_EQ(fakebus::registeredName(), "com.webos.service.mediacontroller");
    EXPECT_TRUE(fakebus::cancelFunctionSet());
    EXPECT_EQ(fakebus::methods().size(), 17u + MCS_COVER_ART
#if USE_TEST_METHOD
              + 1
#endif
    );
    // follows bluetooth2 from the start, and nothing else yet
    ASSERT_EQ(fakebus::openCalls().size(), 1u);
    EXPECT_EQ(fakebus::openCalls()[0].uri, uri::serverStatus);
    EXPECT_EQ(json(fakebus::openCalls()[0].payload)["serviceName"].asString(), "com.webos.service.bluetooth2");
}

TEST_F(Lifecycle, ApiFilesListWhatIsRegistered)
{
    start();
    std::set<std::string> registered(fakebus::methods().begin(), fakebus::methods().end());
    for (const char* acg : {"sysbus", "sysbus_new"}) {
        std::string path = sourceDir() + "/files/" + acg + "/com.webos.service.mediacontroller.api.json.in";
        EXPECT_EQ(apiMethods(path), registered) << path;
    }
}

TEST_F(Lifecycle, PermissionsCoverTheCallsMade)
{
    for (const char* acg : {"sysbus", "sysbus_new"}) {
        std::string dir = sourceDir() + "/files/" + acg + "/com.webos.service.mediacontroller.";
        pbnjson::JValue perm = json(readFile(dir + "perm.json.in"));
        std::set<std::string> groups;
        for (int i = 0; i < perm["com.webos.service.mediacontroller"].arraySize(); i++)
            groups.insert(perm["com.webos.service.mediacontroller"][i].asString());
        EXPECT_EQ(groups, (std::set<std::string>{"bluetooth.query", "bluetooth.management"})) << acg;

        std::string role = readFile(dir + "role.json.in");
        EXPECT_NE(role.find("com.webos.service.bluetooth2"), std::string::npos) << acg;
        EXPECT_EQ(role.find("com.webos.service.account"), std::string::npos) << acg;
    }
}

TEST_F(Lifecycle, EveryStartStepFailingLeavesNothingBehind)
{
    for (const char* step : {"LSRegister", "LSRegisterCategory", "LSCategorySetData",
                             "LSSubscriptionSetCancelFunction", "LSGmainAttach", "LSCall"}) {
        fakebus::reset();
        MediaControlService service;
        fakebus::failNext(step);
        EXPECT_FALSE(service.start(m_loop)) << step;
        EXPECT_EQ(fakebus::handlesOutstanding(), 0) << step;
        EXPECT_EQ(fakebus::errorsOutstanding(), 0) << step;
        EXPECT_TRUE(fakebus::openCalls().empty()) << step;
    }
}

TEST_F(Lifecycle, StopsOnceAndCanStartAgain)
{
    start();
    session("m1", "com.app");
    m_service->stop();
    m_service->stop();
    EXPECT_EQ(fakebus::handlesOutstanding(), 0);
    EXPECT_EQ(fakebus::messagesAlive(), 0) << "a session's call is still held";
    EXPECT_EQ(fakebus::calls("LSUnregister"), 1);

    // the sessions went with the handle
    start();
    EXPECT_EQ(errorCode(call("getMediaSessionInfo", "{\"mediaId\":\"m1\"}")), MCS_ERROR_INVALID_MEDIAID);
}

TEST_F(Lifecycle, DestroyingTheServiceReleasesHeldCalls)
{
    start();
    session("m1", "com.app");
    session("m2", "com.app");
    call("receiveMediaPlaybackInfo", "{\"displayId\":0,\"subscribe\":true}");
    m_service.reset();
    EXPECT_EQ(fakebus::messagesAlive(), 0);
    EXPECT_EQ(fakebus::handlesOutstanding(), 0);
}

typedef McsTest Daemon;

gboolean raiseSignal(gpointer signal)
{
    raise(GPOINTER_TO_INT(signal));
    return G_SOURCE_REMOVE;
}

TEST_F(Daemon, ServesUntilSigtermThenLeavesTheBus)
{
    m_service.reset();
    for (int signal : {SIGTERM, SIGINT}) {
        fakebus::reset();
        g_timeout_add(10, raiseSignal, GINT_TO_POINTER(signal));
        EXPECT_EQ(runMediaControlService(), 0);
        EXPECT_EQ(fakebus::handlesOutstanding(), 0);
        // no signal source is left to fire on a freed loop
        EXPECT_FALSE(g_main_context_pending(nullptr));
    }
}

TEST_F(Daemon, ExitsOneWhenItCannotStart)
{
    m_service.reset();
    fakebus::failNext("LSRegister");
    EXPECT_EQ(runMediaControlService(), 1);
    EXPECT_EQ(fakebus::handlesOutstanding(), 0);
}

} // namespace
