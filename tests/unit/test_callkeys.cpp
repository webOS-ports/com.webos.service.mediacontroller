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

// The wired headset's button during a call (registerCallKeys), and media
// that starts playing during a call.

#include "fixture.h"
#include "MediaFocus.h"

#include <set>

namespace {

const char* const kPlaying = "PLAYSTATE_PLAYING";
const char* const kPaused = "PLAYSTATE_PAUSED";

fakebus::Caller app(const char* sender)
{
    fakebus::Caller c;
    c.sender = sender;
    return c;
}

std::vector<std::string> keyEvents(const std::string& token)
{
    std::vector<std::string> keys;
    for (const std::string& r : fakebus::repliesTo(token)) {
        pbnjson::JValue v = json(r);
        if (v.hasKey("keyEvent"))
            keys.push_back(v["keyEvent"].asString());
    }
    return keys;
}

TEST(CallKeys, OnlyTheWiredButtonIsTheCalls)
{
    EXPECT_TRUE(MediaFocus::isCallKey("playPause"));
    EXPECT_TRUE(MediaFocus::isCallKey("hold"));
    // audiod's pause and play, and every other key, are not
    for (const char* key : {"play", "pause", "stop", "next", "previous", "", "PlayPause"})
        EXPECT_FALSE(MediaFocus::isCallKey(key)) << key;
}

TEST(CallKeys, HeldCallKeysAreACall)
{
    MediaFocus::Audio none;
    EXPECT_FALSE(MediaFocus::refusesStart(none));
    EXPECT_FALSE(MediaFocus::refusesStart(MediaFocus::withCall(none, false)));
    MediaFocus::Audio ringing = MediaFocus::withCall(none, true);
    EXPECT_TRUE(ringing.inCallKnown && ringing.inCall);
    EXPECT_TRUE(MediaFocus::refusesStart(ringing));
    EXPECT_FALSE(MediaFocus::takesBluetoothKey(ringing));
    EXPECT_FALSE(MediaFocus::takesKey(ringing, "play"));
    EXPECT_TRUE(MediaFocus::takesKey(ringing, "pause"));

    // what audiod says of the output is kept
    MediaFocus::Audio bt;
    bt.bluetoothAudioKnown = true;
    bt.bluetoothAudio = true;
    MediaFocus::Audio held = MediaFocus::withCall(bt, true);
    EXPECT_TRUE(held.bluetoothAudioKnown && held.bluetoothAudio);
    // and audiod's call is not ended by the keys not being held
    MediaFocus::Audio call;
    call.inCallKnown = true;
    call.inCall = true;
    EXPECT_TRUE(MediaFocus::refusesStart(MediaFocus::withCall(call, false)));
}

class CallKeysService : public McsTest
{
protected:
    //! The Phone app takes the call keys; the token of its call.
    std::string phone()
    {
        std::string reply = call("registerCallKeys", "{\"subscribe\":true}", app(":1.50"));
        EXPECT_TRUE(returnValue(reply)) << reply;
        EXPECT_TRUE(json(reply)["subscribed"].asBool()) << reply;
        return fakebus::lastToken();
    }

    std::string key(const char* keyEvent)
    {
        return call("injectMediaKeyEvent", std::string("{\"displayId\":0,\"keyEvent\":\"") + keyEvent + "\"}",
                    app(":1.9"));
    }

    void status(const std::string& mediaId, const char* playStatus)
    {
        EXPECT_TRUE(returnValue(call("setMediaPlayStatus",
            "{\"mediaId\":\"" + mediaId + "\",\"playStatus\":\"" + playStatus + "\"}", app(":1.1"))));
    }
};

TEST_F(CallKeysService, OnlyASubscriptionTakesThem)
{
    start();
    for (const char* payload : {"{}", "{\"subscribe\":false}", "{\"subscribe\":1}", "{\"subscribe\":true,\"x\":1}"}) {
        std::string reply = call("registerCallKeys", payload);
        EXPECT_FALSE(returnValue(reply)) << payload;
        EXPECT_EQ(errorCode(reply), MCS_ERROR_PARSING_FAILED) << payload;
    }
    // none of those took the button from the music
    std::string music = session("music", "com.music", app(":1.1"));
    status("music", kPlaying);
    EXPECT_TRUE(returnValue(key("playPause")));
    EXPECT_EQ(keyEvents(music), std::vector<std::string>{"pause"});
}

TEST_F(CallKeysService, WhileHeldTheButtonGoesToTheCallAndNotToTheMusic)
{
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    status("music", kPlaying);
    std::string calls = phone();
    EXPECT_TRUE(returnValue(key("playPause")));
    EXPECT_TRUE(returnValue(key("hold")));
    EXPECT_TRUE(returnValue(key("playPause")));
    EXPECT_EQ(keyEvents(calls), (std::vector<std::string>{"playPause", "hold", "playPause"}));
    EXPECT_TRUE(keyEvents(music).empty()) << "the button reached the music during the call";

    // the Phone app lets go when the last call ends: the button is the music's again
    EXPECT_TRUE(fakebus::cancel(calls));
    EXPECT_TRUE(returnValue(key("playPause")));
    EXPECT_EQ(keyEvents(music), std::vector<std::string>{"pause"});
    EXPECT_EQ(keyEvents(calls).size(), 3u);
}

TEST_F(CallKeysService, TheButtonAnswersWithNoMediaSessionAtAll)
{
    // the usual case: a call rings with nothing playing, nothing registered
    start();
    std::string calls = phone();
    EXPECT_TRUE(returnValue(key("playPause")));
    EXPECT_EQ(keyEvents(calls), std::vector<std::string>{"playPause"});
    // other keys still find no session
    EXPECT_EQ(errorCode(key("next")), MCS_ERROR_NO_ACTIVE_SESSION);
}

TEST_F(CallKeysService, ALongPressWithNoCallDoesNothing)
{
    start();
    EXPECT_TRUE(returnValue(key("hold")));
    std::string music = session("music", "com.music", app(":1.1"));
    status("music", kPlaying);
    EXPECT_TRUE(returnValue(key("hold")));
    status("music", kPaused);
    EXPECT_TRUE(returnValue(key("hold")));
    EXPECT_TRUE(keyEvents(music).empty()) << "a long press reached a session";
}

TEST_F(CallKeysService, ThePhoneAppGoingAwayLetsGo)
{
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    status("music", kPaused);
    std::string calls = phone();
    EXPECT_EQ(errorCode(key("play")), MCS_ERROR_IN_CALL) << "nothing starts while a call rings";
    EXPECT_EQ(fakebus::disconnect(":1.50"), 1);
    EXPECT_TRUE(returnValue(key("playPause")));
    EXPECT_EQ(keyEvents(music), std::vector<std::string>{"play"});
}

TEST_F(CallKeysService, TwoHoldersBothHearAndTheLastToGoEndsIt)
{
    start();
    std::string a = phone();
    std::string b = phone();
    EXPECT_TRUE(returnValue(key("playPause")));
    EXPECT_EQ(keyEvents(a), std::vector<std::string>{"playPause"});
    EXPECT_EQ(keyEvents(b), std::vector<std::string>{"playPause"});
    EXPECT_TRUE(fakebus::cancel(a));
    EXPECT_TRUE(returnValue(key("hold")));
    EXPECT_EQ(keyEvents(a).size(), 1u);
    EXPECT_EQ(keyEvents(b), (std::vector<std::string>{"playPause", "hold"}));
    std::string music = session("music", "com.music", app(":1.1"));
    status("music", kPlaying);  // started with b still held: paused again
    status("music", kPaused);
    EXPECT_TRUE(fakebus::cancel(b));
    EXPECT_TRUE(returnValue(key("playPause")));
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play"}));
}

TEST_F(CallKeysService, AFailedSubscriptionHoldsNothing)
{
    start();
    fakebus::failNext("LSSubscriptionAdd");
    std::string reply = call("registerCallKeys", "{\"subscribe\":true}");
    EXPECT_FALSE(returnValue(reply));
    EXPECT_EQ(errorCode(reply), MCS_ERROR_SUBSCRIPTION_FAILED);
    std::string music = session("music", "com.music", app(":1.1"));
    status("music", kPaused);
    EXPECT_TRUE(returnValue(key("playPause")));
    EXPECT_EQ(keyEvents(music), std::vector<std::string>{"play"});
}

TEST_F(CallKeysService, ASessionStartingDuringACallIsPausedAndTakesNothing)
{
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    std::string video = session("video", "com.atlas", app(":1.2"), false);
    status("music", kPlaying);
    audiod(1, 0);
    // audiod pauses the music for the call
    EXPECT_TRUE(returnValue(key("pause")));
    status("music", kPaused);

    // a video started during the call is paused again
    status("video", kPlaying);
    EXPECT_EQ(keyEvents(video), std::vector<std::string>{"pause"});
    status("video", kPaused);
    // and it took neither the keys nor the music's place: after the call
    // audiod's play resumes the music, not the video
    audiod(0, 0);
    EXPECT_TRUE(returnValue(key("play")));
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play"}));
    EXPECT_EQ(keyEvents(video).size(), 1u);
    EXPECT_EQ(json(call("getActiveMediaSessions", "{}"))["sessionList"][0].asString(), "music");

    // with the call over, a start is a start again
    std::string radio = session("radio", "com.radio", app(":1.3"), false);
    status("music", kPlaying);
    status("radio", kPlaying);
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play", "pause"}));
    EXPECT_TRUE(keyEvents(radio).empty());
    EXPECT_EQ(keyEvents(video).size(), 1u);
}

TEST_F(CallKeysService, ASessionStartingWhileACallRingsIsPaused)
{
    // audiod says nothing of a call that only rings; the Phone app's hold does
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    std::string calls = phone();
    status("music", kPlaying);
    EXPECT_EQ(keyEvents(music), std::vector<std::string>{"pause"});
    status("music", kPaused);
    EXPECT_TRUE(fakebus::cancel(calls));
    status("music", kPlaying);
    EXPECT_EQ(keyEvents(music).size(), 1u);
}

TEST_F(CallKeysService, AVoipCallsOwnSoundIsLeftAlone)
{
    // A VoIP call (WhatsApp and the like, through the libpurple plugin)
    // plays on audiod's voipcall/webcall streams, which are no media
    // session. audiod says inCall for it. Media starting during it is
    // paused - by telling that session, and nothing else: no call goes to
    // audiod or anywhere but bluetooth2's play status, and no subscriber
    // but the session started hears a thing.
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    std::string other = session("radio", "com.radio", app(":1.3"));
    status("radio", kPlaying);
    std::string calls = phone();
    audiod(1, 0);
    EXPECT_TRUE(returnValue(key("pause")));  // audiod's pause for the call
    status("radio", kPaused);

    size_t made = fakebus::callsMade().size();
    size_t replied = fakebus::replies().size();
    std::set<std::string> told;
    status("music", kPlaying);
    for (size_t i = made; i < fakebus::callsMade().size(); i++)
        EXPECT_EQ(fakebus::callsMade()[i].uri, uri::notifyPlayStatus)
            << "called during a VoIP call: " << fakebus::callsMade()[i].uri;
    for (size_t i = replied; i < fakebus::replies().size(); i++)
        told.insert(fakebus::replies()[i].to);
    told.erase(fakebus::lastToken());  // setMediaPlayStatus's own reply
    EXPECT_EQ(told, std::set<std::string>{music}) << "someone other than the session started was told";
    EXPECT_EQ(keyEvents(music), std::vector<std::string>{"pause"});
    EXPECT_EQ(keyEvents(other), std::vector<std::string>{"pause"}) << "only audiod's pause";
    EXPECT_TRUE(keyEvents(calls).empty()) << "the call was sent a key";
    for (const fakebus::OutCall& c : fakebus::callsMade())
        EXPECT_FALSE(c.uri.rfind("luna://com.palm.audio/", 0) == 0 && c.uri != uri::audiodStatus)
            << "audiod was asked to do something: " << c.uri;
}

} // namespace
