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

#ifndef MCS_TEST_FIXTURE_H
#define MCS_TEST_FIXTURE_H

#include <gtest/gtest.h>

#include <glib.h>
#include <pbnjson.hpp>

#include <memory>
#include <string>

#include "MediaControlService.h"
#include "fakebus.h"

inline pbnjson::JValue json(const std::string& text)
{
    return pbnjson::JDomParser::fromString(text);
}

inline bool returnValue(const std::string& reply)
{
    pbnjson::JValue v = json(reply);
    return v["returnValue"].isBoolean() && v["returnValue"].asBool();
}

inline int errorCode(const std::string& reply)
{
    pbnjson::JValue v = json(reply);
    return v["errorCode"].isNumber() ? v["errorCode"].asNumber<int>() : -1;
}

namespace uri {
const char* const serverStatus = "luna://com.webos.service.bus/signal/registerServerStatus";
const char* const adapterStatus = "luna://com.webos.service.bluetooth2/adapter/getStatus";
const char* const deviceStatus = "luna://com.webos.service.bluetooth2/device/getStatus";
const char* const avrcpStatus = "luna://com.webos.service.bluetooth2/avrcp/getStatus";
const char* const passThrough = "luna://com.webos.service.bluetooth2/avrcp/receivePassThroughCommand";
const char* const notifyPlayStatus = "luna://com.webos.service.bluetooth2/avrcp/notifyMediaPlayStatus";
}

/**
 * A clean bus for every test, and a check afterwards that the service gave
 * back every handle, LSError and message it took, and that GLib itself did
 * not complain.
 */
class McsTest : public ::testing::Test
{
protected:
    static void glibComplaint(const gchar* domain, GLogLevelFlags, const gchar* message, gpointer)
    {
        ADD_FAILURE() << domain << " complained: " << message;
    }

    void SetUp() override
    {
        m_glibHandler = g_log_set_handler("GLib",
            static_cast<GLogLevelFlags>(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING), glibComplaint, nullptr);
        fakebus::reset();
        m_loop = g_main_loop_new(nullptr, FALSE);
        m_service = std::make_unique<MediaControlService>();
    }

    void TearDown() override
    {
        m_service.reset();
        EXPECT_EQ(fakebus::handlesOutstanding(), 0) << "an LSHandle was not unregistered";
        EXPECT_EQ(fakebus::errorsOutstanding(), 0) << "an LSError was not freed";
        EXPECT_EQ(fakebus::messagesAlive(), 0) << "a message was held past the service";
        fakebus::reset();
        g_main_loop_unref(m_loop);
        g_log_remove_handler("GLib", m_glibHandler);
    }

    void start()
    {
        ASSERT_TRUE(m_service->start(m_loop));
    }

    //! Calls method; the one reply it must produce.
    std::string call(const std::string& method, const std::string& payload,
                     const fakebus::Caller& caller = fakebus::Caller())
    {
        size_t before = fakebus::replies().size();
        EXPECT_TRUE(fakebus::callMethod(method, payload, caller)) << method << " did not return true";
        std::vector<std::string> mine = fakebus::repliesTo(fakebus::lastToken());
        EXPECT_EQ(mine.size(), 1u) << method << " must reply exactly once to its caller";
        EXPECT_GE(fakebus::replies().size(), before + 1);
        return mine.empty() ? std::string() : mine.front();
    }

    //! Registers and activates a session for caller; the token of its
    //! registerMediaSession call.
    std::string session(const std::string& mediaId, const std::string& appId,
                        const fakebus::Caller& caller = fakebus::Caller(), bool activate = true)
    {
        std::string reply = call("registerMediaSession",
            "{\"mediaId\":\"" + mediaId + "\",\"appId\":\"" + appId + "\",\"subscribe\":true}", caller);
        EXPECT_TRUE(returnValue(reply)) << reply;
        std::string token = fakebus::lastToken();
        if (activate) {
            EXPECT_TRUE(returnValue(call("activateMediaSession", "{\"mediaId\":\"" + mediaId + "\"}")));
        }
        return token;
    }

    //! The token of the one open call to u.
    LSMessageToken openCall(const char* u)
    {
        std::vector<fakebus::OutCall> open = fakebus::openCalls(u);
        EXPECT_EQ(open.size(), 1u) << "calls open to " << u;
        return open.empty() ? LSMESSAGE_TOKEN_INVALID : open.front().token;
    }

    GMainLoop* m_loop = nullptr;
    std::unique_ptr<MediaControlService> m_service;
    guint m_glibHandler = 0;
};

#endif // MCS_TEST_FIXTURE_H
