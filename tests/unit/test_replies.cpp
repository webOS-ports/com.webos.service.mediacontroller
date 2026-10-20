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

typedef McsTest Replies;

//! Event payloads the call with token was sent after its first reply.
std::vector<pbnjson::JValue> events(const std::string& token)
{
    std::vector<pbnjson::JValue> out;
    std::vector<std::string> all = fakebus::repliesTo(token);
    for (size_t i = 1; i < all.size(); i++)
        out.push_back(json(all[i]));
    return out;
}

std::string subscribePlayback(McsTest* t, const std::string& eventType, const fakebus::Caller& caller)
{
    (void)t;
    std::string payload = "{\"displayId\":0,\"subscribe\":true" +
        (eventType.empty() ? std::string() : ",\"eventType\":\"" + eventType + "\"") + "}";
    EXPECT_TRUE(fakebus::callMethod("receiveMediaPlaybackInfo", payload, caller));
    return fakebus::lastToken();
}

fakebus::Caller app(const char* sender)
{
    fakebus::Caller c;
    c.sender = sender;
    return c;
}

// luna-service2 sends a second error, "not handled", for a handler that
// returns false; every method must reply once and return true, whatever
// it is sent.
TEST_F(Replies, EveryMethodRepliesOnceToAnyPayload)
{
    start();
    session("m1", "com.one");
    for (const std::string& method : fakebus::methods()) {
        for (const char* payload : {"", "not json", "[]", "{}", "{\"mediaId\":5}", "{\"mediaId\":\"m1\"}",
                                    "{\"mediaId\":\"m1\",\"extra\":true}", "{\"displayId\":0}",
                                    "{\"displayId\":7,\"subscribe\":true}", "null"}) {
            std::string reply = call(method, payload);
            EXPECT_TRUE(json(reply).isObject()) << method << " " << payload << ": " << reply;
        }
    }
}

TEST_F(Replies, MalformedPayloadIsAParsingError)
{
    start();
    std::string reply = call("getMediaPlayStatus", "{\"mediaId\":1}");
    EXPECT_FALSE(returnValue(reply));
    EXPECT_EQ(errorCode(reply), MCS_ERROR_PARSING_FAILED);
    EXPECT_EQ(json(reply)["errorText"].asString(), "Parsing Error");
}

TEST_F(Replies, UnknownSessionIsInvalidMediaId)
{
    start();
    for (const char* method : {"getMediaPlayStatus", "getMediaMetaData", "getMediaSessionInfo",
                               "unregisterMediaSession", "activateMediaSession", "deactivateMediaSession"})
        EXPECT_EQ(errorCode(call(method, "{\"mediaId\":\"nope\"}")), MCS_ERROR_INVALID_MEDIAID) << method;
    EXPECT_EQ(errorCode(call("setMediaPlayStatus", "{\"mediaId\":\"nope\",\"playStatus\":\"PLAYSTATE_PLAYING\"}")),
              MCS_ERROR_INVALID_MEDIAID);
}

TEST_F(Replies, PlayStatusIsValidated)
{
    start();
    session("m1", "com.one");
    EXPECT_EQ(errorCode(call("setMediaPlayStatus", "{\"mediaId\":\"m1\",\"playStatus\":\"playing\"}")),
              MCS_ERROR_SESSION_INVALID_PLAY_STATE);
    EXPECT_TRUE(returnValue(call("setMediaPlayStatus", "{\"mediaId\":\"m1\",\"playStatus\":\"PLAYSTATE_PAUSED\"}")));
    EXPECT_EQ(json(call("getMediaPlayStatus", "{\"mediaId\":\"m1\"}"))["playStatus"].asString(), "PLAYSTATE_PAUSED");
}

TEST_F(Replies, MuteStatusIsNormalised)
{
    start();
    session("m1", "com.one");
    std::string watcher = subscribePlayback(this, "muteStatus", app(":1.9"));
    for (const char* given : {"MUTESTATE_MUTED", "MUTE", "mute"}) {
        EXPECT_TRUE(returnValue(call("setMediaMuteStatus",
            std::string("{\"mediaId\":\"m1\",\"muteStatus\":\"") + given + "\"}")));
        EXPECT_EQ(events(watcher).back()["muteStatus"].asString(), "mute") << given;
    }
    for (const char* given : {"MUTESTATE_UNMUTED", "UNMUTE", "unmute"}) {
        call("setMediaMuteStatus", std::string("{\"mediaId\":\"m1\",\"muteStatus\":\"") + given + "\"}");
        EXPECT_EQ(events(watcher).back()["muteStatus"].asString(), "unmute") << given;
    }
    std::string reply = call("setMediaMuteStatus", "{\"mediaId\":\"m1\",\"muteStatus\":\"loud\"}");
    EXPECT_EQ(errorCode(reply), MCS_ERROR_SESSION_INVALID_MUTE_STATUS);
    EXPECT_EQ(json(reply)["errorText"].asString(), "Invalid Mute Status");
}

TEST_F(Replies, EachSubscriberGetsOnlyWhatItAskedFor)
{
    start();
    session("m1", "com.one");
    std::string status = subscribePlayback(this, "playStatus", app(":1.7"));
    std::string all = subscribePlayback(this, "", app(":1.8"));
    // a later caller asking for something else changes nothing for the first
    subscribePlayback(this, "muteStatus", app(":1.9"));

    call("setMediaMuteStatus", "{\"mediaId\":\"m1\",\"muteStatus\":\"MUTESTATE_MUTED\"}");
    call("setMediaPlayPosition", "{\"mediaId\":\"m1\",\"playPosition\":\"12.5\"}");
    call("setMediaPlayStatus", "{\"mediaId\":\"m1\",\"playStatus\":\"PLAYSTATE_PLAYING\"}");

    ASSERT_EQ(events(status).size(), 1u) << "the playStatus subscriber was sent other events";
    EXPECT_EQ(events(status)[0]["eventType"].asString(), "playStatus");
    EXPECT_EQ(events(status)[0]["playStatus"].asString(), "PLAYSTATE_PLAYING");
    EXPECT_EQ(events(all).size(), 3u);
}

TEST_F(Replies, EveryEventCarriesTheSameFields)
{
    start();
    session("m1", "com.one");
    std::string all = subscribePlayback(this, "", app(":1.8"));
    call("setMediaPlayStatus", "{\"mediaId\":\"m1\",\"playStatus\":\"PLAYSTATE_PLAYING\"}");
    call("setMediaMuteStatus", "{\"mediaId\":\"m1\",\"muteStatus\":\"MUTESTATE_MUTED\"}");
    call("setMediaPlayPosition", "{\"mediaId\":\"m1\",\"playPosition\":\"1.0\"}");
    call("setMediaMetaData", "{\"mediaId\":\"m1\",\"mediaMetaData\":{\"title\":\"T\"}}");
    call("setMediaCoverArt", "{\"mediaId\":\"m1\",\"coverArt\":[{\"src\":\"a.png\"}]}");
    call("setSupportedActions", "{\"mediaId\":\"m1\",\"supportedActions\":[\"play\"]}");
    std::vector<pbnjson::JValue> sent = events(all);
    ASSERT_EQ(sent.size(), 6u);
    for (pbnjson::JValue e : sent) {
        for (const char* field : {"displayId", "mediaId", "appId", "eventType", "returnValue", "subscribed"})
            EXPECT_TRUE(e.hasKey(field)) << field << " missing from " << e.stringify();
        EXPECT_EQ(e["mediaId"].asString(), "m1");
        EXPECT_EQ(e["appId"].asString(), "com.one");
        EXPECT_TRUE(e.hasKey(e["eventType"].asString())) << e.stringify();
    }
}

TEST_F(Replies, FirstPlaybackInfoReplyIsTheStateToTheCallerOnly)
{
    start();
    session("m1", "com.one");
    call("setMediaPlayStatus", "{\"mediaId\":\"m1\",\"playStatus\":\"PLAYSTATE_PLAYING\"}");
    std::string first = subscribePlayback(this, "", app(":1.7"));
    std::string second = subscribePlayback(this, "", app(":1.8"));

    std::vector<std::string> toFirst = fakebus::repliesTo(first);
    ASSERT_EQ(toFirst.size(), 1u) << "the second caller's subscribing was sent to the first";
    pbnjson::JValue state = json(toFirst[0]);
    EXPECT_TRUE(state["returnValue"].asBool());
    EXPECT_TRUE(state["subscribed"].asBool());
    EXPECT_EQ(state["playStatus"].asString(), "PLAYSTATE_PLAYING");
    EXPECT_EQ(state["mediaId"].asString(), "m1");
    EXPECT_EQ(fakebus::repliesTo(second).size(), 1u);
}

TEST_F(Replies, PlaybackInfoValidatesItsArguments)
{
    start();
    EXPECT_EQ(errorCode(call("receiveMediaPlaybackInfo", "{\"displayId\":2,\"subscribe\":true}")),
              MCS_ERROR_INVALID_DISPLAYID);
    EXPECT_EQ(errorCode(call("receiveMediaPlaybackInfo",
                             "{\"displayId\":0,\"subscribe\":true,\"eventType\":\"volume\"}")),
              MCS_ERROR_INVALID_EVENT);
    EXPECT_TRUE(fakebus::subscriptionKeys().empty());
    // without a session it still answers, with nothing in it
    std::string reply = call("receiveMediaPlaybackInfo", "{\"displayId\":0,\"subscribe\":false}");
    EXPECT_TRUE(returnValue(reply));
    EXPECT_FALSE(json(reply)["subscribed"].asBool());
    EXPECT_EQ(json(reply)["mediaId"].asString(), "");
}

TEST_F(Replies, SubscriberThatWentIsNotWrittenTo)
{
    start();
    session("m1", "com.one");
    std::string gone = subscribePlayback(this, "", app(":1.7"));
    fakebus::disconnect(":1.7");
    size_t before = fakebus::repliesTo(gone).size();
    call("setMediaPlayStatus", "{\"mediaId\":\"m1\",\"playStatus\":\"PLAYSTATE_PLAYING\"}");
    EXPECT_EQ(fakebus::repliesTo(gone).size(), before);
}

TEST_F(Replies, SupportedActionsAndCoverArtAreValidated)
{
    start();
    session("m1", "com.one");
    EXPECT_EQ(errorCode(call("setSupportedActions", "{\"mediaId\":\"m1\",\"supportedActions\":[]}")),
              MCS_ERROR_PARSING_FAILED);
    EXPECT_EQ(errorCode(call("setSupportedActions", "{\"mediaId\":\"m1\",\"supportedActions\":[\"\"]}")),
              MCS_ERROR_PARSING_FAILED);
    EXPECT_EQ(errorCode(call("setSupportedActions", "{\"mediaId\":\"m1\",\"supportedActions\":[1]}")),
              MCS_ERROR_PARSING_FAILED);
    EXPECT_EQ(errorCode(call("setMediaCoverArt", "{\"mediaId\":\"m1\",\"coverArt\":[{\"type\":\"png\"}]}")),
              MCS_ERROR_PARSING_FAILED);
    EXPECT_EQ(errorCode(call("setMediaCoverArt", "{\"mediaId\":\"m1\",\"coverArt\":[\"a.png\"]}")),
              MCS_ERROR_PARSING_FAILED);
    EXPECT_EQ(errorCode(call("setMediaCoverArt",
                             "{\"mediaId\":\"m1\",\"coverArt\":[{\"src\":\"a\",\"sizes\":[{\"width\":\"x\"}]}]}")),
              MCS_ERROR_PARSING_FAILED);
    EXPECT_TRUE(returnValue(call("setMediaCoverArt",
        "{\"mediaId\":\"m1\",\"coverArt\":[{\"src\":\"a\",\"sizes\":[{\"width\":10,\"height\":20}]}]}")));
}

} // namespace
