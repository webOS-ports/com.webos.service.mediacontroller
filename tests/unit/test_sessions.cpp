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

namespace {

typedef McsTest Sessions;

fakebus::Caller app(const char* sender)
{
    fakebus::Caller c;
    c.sender = sender;
    return c;
}

std::vector<std::string> activeSessions(McsTest* t)
{
    (void)t;
    fakebus::callMethod("getActiveMediaSessions", "{}");
    pbnjson::JValue reply = json(fakebus::repliesTo(fakebus::lastToken()).at(0));
    std::vector<std::string> ids;
    for (int i = 0; reply["sessionList"].isArray() && i < reply["sessionList"].arraySize(); i++)
        ids.push_back(reply["sessionList"][i].asString());
    return ids;
}

//! The key events the call with token was sent, in order.
std::vector<std::string> keyEvents(const std::string& token)
{
    std::vector<std::string> keys;
    for (const std::string& r : fakebus::repliesTo(token)) {
        pbnjson::JValue v = json(r);
        if (v.hasKey("keyEvent"))
            keys.push_back(v["mediaId"].asString() + ":" + v["keyEvent"].asString());
    }
    return keys;
}

TEST_F(Sessions, RegisterRepliesSubscribedAndHoldsTheCall)
{
    start();
    std::string reply = call("registerMediaSession",
                             "{\"mediaId\":\"m1\",\"appId\":\"com.app\",\"subscribe\":true}");
    EXPECT_TRUE(returnValue(reply));
    // Chromium takes a reply without subscribed:true as a refusal
    EXPECT_TRUE(json(reply)["subscribed"].asBool());
    EXPECT_EQ(fakebus::subscribers("registerMediaSession").size(), 1u);
}

TEST_F(Sessions, EmptyIdsAreRefused)
{
    start();
    EXPECT_EQ(errorCode(call("registerMediaSession", "{\"mediaId\":\"\",\"appId\":\"a\",\"subscribe\":true}")),
              MCS_ERROR_INVALID_MEDIAID);
    EXPECT_EQ(errorCode(call("registerMediaSession", "{\"mediaId\":\"m\",\"appId\":\"\",\"subscribe\":true}")),
              MCS_ERROR_INVALID_APPID);
    EXPECT_TRUE(fakebus::subscribers("registerMediaSession").empty());
}

TEST_F(Sessions, SecondRegistrationOfATakenIdIsNotSubscribed)
{
    start();
    std::string first = session("m1", "com.one", app(":1.1"));
    std::string reply = call("registerMediaSession",
                             "{\"mediaId\":\"m1\",\"appId\":\"com.two\",\"subscribe\":true}", app(":1.2"));
    EXPECT_EQ(errorCode(reply), MCS_ERROR_SESSION_ALREADY_REGISTERED);
    EXPECT_FALSE(json(reply)["subscribed"].asBool());
    EXPECT_EQ(fakebus::subscribers("registerMediaSession"), std::vector<std::string>{first});
}

TEST_F(Sessions, ClientGoingAwayEndsItsSession)
{
    start();
    session("m1", "com.one", app(":1.1"));
    session("m2", "com.two", app(":1.2"));
    EXPECT_EQ(fakebus::disconnect(":1.1"), 1);
    EXPECT_EQ(errorCode(call("getMediaSessionInfo", "{\"mediaId\":\"m1\"}")), MCS_ERROR_INVALID_MEDIAID);
    EXPECT_EQ(activeSessions(this), std::vector<std::string>{"m2"});
    // the other session is untouched
    EXPECT_TRUE(returnValue(call("getMediaSessionInfo", "{\"mediaId\":\"m2\"}")));
}

TEST_F(Sessions, CancellingTheCallEndsTheSession)
{
    start();
    std::string token = session("m1", "com.one");
    EXPECT_TRUE(fakebus::cancel(token));
    EXPECT_EQ(errorCode(call("getMediaPlayStatus", "{\"mediaId\":\"m1\"}")), MCS_ERROR_INVALID_MEDIAID);
    EXPECT_EQ(errorCode(call("getActiveMediaSessions", "{}")), MCS_ERROR_NO_ACTIVE_SESSION);
}

TEST_F(Sessions, UnregisterReleasesTheCallAndAnOldCancelIsHarmless)
{
    start();
    std::string old = session("m1", "com.one", app(":1.1"));
    EXPECT_TRUE(returnValue(call("unregisterMediaSession", "{\"mediaId\":\"m1\"}")));
    // the id taken again by someone else; the first caller's call ending
    // must not end the new session
    session("m1", "com.two", app(":1.2"));
    fakebus::cancel(old);
    EXPECT_TRUE(returnValue(call("getMediaSessionInfo", "{\"mediaId\":\"m1\"}")));
    EXPECT_EQ(json(call("getMediaSessionInfo", "{\"mediaId\":\"m1\"}"))["sessionInfo"]["appId"].asString(),
              "com.two");
}

TEST_F(Sessions, KeyEventGoesToTheLastActivatedSessionOnly)
{
    start();
    std::string one = session("m1", "com.one", app(":1.1"));
    std::string two = session("m2", "com.two", app(":1.2"));
    EXPECT_TRUE(returnValue(call("injectMediaKeyEvent", "{\"displayId\":0,\"keyEvent\":\"pause\"}")));
    EXPECT_EQ(keyEvents(two), std::vector<std::string>{"m2:pause"});
    EXPECT_TRUE(keyEvents(one).empty()) << "a session was told another session's keys";

    // activating the first again brings it to the front
    EXPECT_TRUE(returnValue(call("activateMediaSession", "{\"mediaId\":\"m1\"}")));
    call("injectMediaKeyEvent", "{\"displayId\":0,\"keyEvent\":\"play\"}");
    EXPECT_EQ(keyEvents(one), std::vector<std::string>{"m1:play"});
    EXPECT_EQ(keyEvents(two).size(), 1u);
}

TEST_F(Sessions, KeyEventWithNoActiveSessionFails)
{
    start();
    EXPECT_EQ(errorCode(call("injectMediaKeyEvent", "{\"displayId\":0,\"keyEvent\":\"play\"}")),
              MCS_ERROR_NO_ACTIVE_SESSION);
    session("m1", "com.one", fakebus::Caller(), false);
    EXPECT_EQ(errorCode(call("injectMediaKeyEvent", "{\"displayId\":0,\"keyEvent\":\"play\"}")),
              MCS_ERROR_NO_ACTIVE_SESSION) << "registered but never activated";
    EXPECT_EQ(errorCode(call("injectMediaKeyEvent", "{\"displayId\":0,\"keyEvent\":\"\"}")),
              MCS_ERROR_INVALID_EVENT);
}

TEST_F(Sessions, DeactivatedSessionGetsNoKeys)
{
    start();
    std::string one = session("m1", "com.one", app(":1.1"));
    std::string two = session("m2", "com.two", app(":1.2"));
    call("deactivateMediaSession", "{\"mediaId\":\"m2\"}");
    call("injectMediaKeyEvent", "{\"displayId\":0,\"keyEvent\":\"next\"}");
    EXPECT_EQ(keyEvents(one), std::vector<std::string>{"m1:next"});
    EXPECT_TRUE(keyEvents(two).empty());
}

TEST_F(Sessions, ActivatingTwiceAndDeactivatingWhatIsNotActiveIsHarmless)
{
    start();
    session("m1", "com.one", fakebus::Caller(), false);
    // the first activation is onto an empty list
    EXPECT_TRUE(returnValue(call("activateMediaSession", "{\"mediaId\":\"m1\"}")));
    EXPECT_TRUE(returnValue(call("activateMediaSession", "{\"mediaId\":\"m1\"}")));
    EXPECT_EQ(activeSessions(this), std::vector<std::string>{"m1"});
    EXPECT_TRUE(returnValue(call("deactivateMediaSession", "{\"mediaId\":\"m1\"}")));
    EXPECT_TRUE(returnValue(call("deactivateMediaSession", "{\"mediaId\":\"m1\"}")));
    EXPECT_EQ(errorCode(call("activateMediaSession", "{\"mediaId\":\"nope\"}")), MCS_ERROR_INVALID_MEDIAID);
}

TEST_F(Sessions, MetadataIsReplacedWhole)
{
    start();
    session("m1", "com.one");
    call("setMediaMetaData", "{\"mediaId\":\"m1\",\"mediaMetaData\":{\"title\":\"A\",\"album\":\"First\",\"trackNumber\":3}}");
    call("setMediaMetaData", "{\"mediaId\":\"m1\",\"mediaMetaData\":{\"title\":\"B\"}}");
    pbnjson::JValue meta = json(call("getMediaMetaData", "{\"mediaId\":\"m1\"}"))["metaData"];
    EXPECT_EQ(meta["title"].asString(), "B");
    EXPECT_EQ(meta["album"].asString(), "") << "the last track's album was kept";
    EXPECT_EQ(meta["trackNumber"].asNumber<int>(), 0);
}

TEST_F(Sessions, SessionIdsByApp)
{
    start();
    session("m1", "com.one");
    session("m2", "com.one");
    session("m3", "com.two");
    pbnjson::JValue ids = json(call("getMediaSessionId", "{\"appId\":\"com.one\"}"))["mediaId"];
    ASSERT_EQ(ids.arraySize(), 2);
    EXPECT_EQ(errorCode(call("getMediaSessionId", "{\"appId\":\"com.none\"}")), MCS_ERROR_INVALID_APPID);
}

} // namespace
