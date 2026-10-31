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
    //! The service's clock, which only later() moves.
    static inline std::int64_t s_now = 0;
    static std::int64_t now() { return s_now; }

    void SetUp() override
    {
        McsTest::SetUp();
        s_now = 1000000;
        m_service->setClock(&now);
    }

    //! ms go by.
    static void later(std::int64_t ms) { s_now += ms; }

    //! audiod says a call is up, or that none is.
    void audiodCall(bool up) { audiod(up ? 1 : 0, 0); }

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
    later(MediaFocus::kPlayAfterCallMs);  // nor after it, so long after
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
    later(MediaFocus::kPlayAfterCallMs);  // long before the call ends
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
    // audiod's play resumes the music, not the video, which was played
    // well before the call ended
    later(MediaFocus::kPlayAfterCallMs);
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

TEST_F(CallKeysService, EveryPlayDuringACallIsPausedNotJustTheFirst)
{
    // MK-2, 31 October 10:02:47: the Music app paused for the call was
    // played again from its own controls 0.8 s after the pause, and again
    // 0.1 s after the user paused it: within the carry-on time, so not a
    // start, and it played on through the call.
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    audiod(1, 0);
    // kept below kRepauseLimit, which would hide a pause too many
    static_assert(MediaFocus::kRepauseLimit >= 5);
    for (int i = 0; i < 3; i++) {
        status("music", kPlaying);
        status("music", kPaused);
    }
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "pause", "pause"}));
    // buffering is about to play: paused as well
    call("setMediaPlayStatus", "{\"mediaId\":\"music\",\"playStatus\":\"PLAYSTATE_BUFFERING\"}", app(":1.1"));
    EXPECT_EQ(keyEvents(music).size(), 4u);
    // what it reports while it already plays is not another play
    status("music", kPlaying);
    status("music", kPlaying);
    EXPECT_EQ(keyEvents(music).size(), 4u);
}

TEST_F(CallKeysService, AnAppThatPlaysOnAfterEveryPauseIsNotLooped)
{
    // An app answering each pause by playing again would be paused back
    // and forth without end; after kRepauseLimit pauses within the
    // carry-on time it is let play.
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    std::string calls = phone();
    for (unsigned i = 0; i < MediaFocus::kRepauseLimit + 3; i++) {
        status("music", kPlaying);
        status("music", kPaused);
    }
    EXPECT_EQ(keyEvents(music).size(), MediaFocus::kRepauseLimit);
    // and the limit is its own: another session is still paused
    std::string video = session("video", "com.atlas", app(":1.2"), false);
    status("video", kPlaying);
    EXPECT_EQ(keyEvents(video), std::vector<std::string>{"pause"});
}

TEST(CallKeys, RepausesAreCountedPerSessionWithinTheCarryOnTime)
{
    MediaFocus focus;
    for (unsigned i = 0; i < MediaFocus::kRepauseLimit; i++)
        EXPECT_TRUE(focus.repause("m", 1000 + i)) << i;
    EXPECT_FALSE(focus.repause("m", 1100));
    EXPECT_TRUE(focus.repause("other", 1100));
    // the oldest out of the window makes room again
    EXPECT_FALSE(focus.repause("m", 1000 + MediaFocus::kCarryOnMs - 1));
    EXPECT_TRUE(focus.repause("m", 1000 + MediaFocus::kCarryOnMs));
    EXPECT_FALSE(focus.repause("m", 1000 + MediaFocus::kCarryOnMs));
    // a session gone starts afresh
    focus.forget("m");
    EXPECT_TRUE(focus.repause("m", 1100));
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
    later(MediaFocus::kPlayAfterCallMs);
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

// --- a play held back for the end of the call --------------------------------

TEST(CallKeys, APlayHeldBackIsCarriedOutOnlyIfTheCallEndsSoonAfter)
{
    MediaFocus focus;
    EXPECT_TRUE(focus.playsAfterCall(5000).empty());
    focus.deferPlay("late", 5000 - MediaFocus::kPlayAfterCallMs + 1);
    focus.deferPlay("early", 5000 - MediaFocus::kPlayAfterCallMs);
    focus.deferPlay("gone", 4999);
    focus.forget("gone");
    EXPECT_EQ(focus.playsAfterCall(5000), std::vector<std::string>{"late"});
    // each is dropped once the call has ended, carried out or not
    EXPECT_TRUE(focus.playsAfterCall(5000).empty());
    // the first held back first, whenever in the time
    focus.deferPlay("b", 4000);
    focus.deferPlay("a", 4000);
    focus.deferPlay("c", 3999);
    EXPECT_EQ(focus.playsAfterCall(5000), (std::vector<std::string>{"b", "a", "c"}));
    // the last ask counts
    focus.deferPlay("m", 1000);
    focus.deferPlay("m", 9000);
    EXPECT_EQ(focus.playsAfterCall(9000 + MediaFocus::kPlayAfterCallMs - 1), std::vector<std::string>{"m"});
    focus.deferPlay("m", 1000);
    focus.clear();
    EXPECT_TRUE(focus.playsAfterCall(1000).empty());
}

TEST(CallKeys, ACallEndsWhenTheLastOfAudiodsCallAndTheHeldKeysGoes)
{
    MediaFocus::Audio none;
    MediaFocus::Audio call;
    call.inCallKnown = true;
    call.inCall = true;
    MediaFocus::Audio ringing = MediaFocus::withCall(none, true);
    EXPECT_TRUE(MediaFocus::callEnded(call, none));
    EXPECT_TRUE(MediaFocus::callEnded(ringing, none));
    EXPECT_FALSE(MediaFocus::callEnded(none, none));
    EXPECT_FALSE(MediaFocus::callEnded(none, call));
    EXPECT_FALSE(MediaFocus::callEnded(ringing, call));
    EXPECT_FALSE(MediaFocus::callEnded(call, ringing));
    MediaFocus::Audio unknown;
    unknown.inCallKnown = false;
    unknown.inCall = true;
    EXPECT_TRUE(MediaFocus::callEnded(call, unknown)) << "audiod gone: its call with it";
}

TEST_F(CallKeysService, MusicPlaysOnAfterARejectedCall)
{
    // 31 October 13:23: the Music app, playing, paused itself for the call
    // (its CallWatcher, from oFono). The user rejected it. oFono removed
    // the call at 13:23:35.875 and the app played again at once - while
    // the Phone app still held the call keys, which it let go 404 ms later.
    // So that play was paused for the call, and nothing played after it.
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    status("music", kPlaying);
    std::string calls = phone();
    status("music", kPaused);          // its own pause for the call
    later(7000);                       // it rings; it is rejected
    status("music", kPlaying);         // oFono: the call is gone
    EXPECT_EQ(keyEvents(music), std::vector<std::string>{"pause"});
    status("music", kPaused);
    later(404);
    EXPECT_TRUE(fakebus::cancel(calls));
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play"}));
    // and it plays on: no call is up to pause it
    status("music", kPlaying);
    EXPECT_EQ(keyEvents(music).size(), 2u);
}

TEST_F(CallKeysService, AfterAnAnsweredCallTheMusicIsPlayedOnce)
{
    // An answered call: audiod's call ends some 330 ms before the Phone app
    // lets go of the keys (13:05, 13:07 and 13:20 on 31 October). The
    // Music app plays again in between, as after a rejected call; and
    // audiod's play, for a session it paused itself, comes then too. One
    // play after the call, not two.
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    status("music", kPlaying);
    std::string calls = phone();
    status("music", kPaused);
    later(2000);
    audiodCall(true);                  // answered
    later(60000);
    audiodCall(false);                 // hung up
    status("music", kPlaying);         // the app's own resume
    status("music", kPaused);
    EXPECT_EQ(errorCode(key("play")), MCS_ERROR_IN_CALL);   // and audiod's
    EXPECT_EQ(keyEvents(music), std::vector<std::string>{"pause"});
    later(330);
    EXPECT_TRUE(fakebus::cancel(calls));
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play"}));
    status("music", kPlaying);
    EXPECT_EQ(keyEvents(music).size(), 2u);
}

TEST_F(CallKeysService, AfterAHangUpTheMusicPlaysWhicheverEndComesLast)
{
    // 31 October 13:38, Spotify in the Music app, answered at 13:38:17.17:
    // audiod left the call at 20.473, the app played again at 20.478 and
    // was paused, mediacontroller heard audiod's inCall false at 20.493 and
    // the Phone app let go of the keys at 20.608. The music stayed paused.
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    status("music", kPlaying);
    std::string calls = phone();
    status("music", kPaused);
    later(3800);
    audiodCall(true);
    later(3300);
    status("music", kPlaying);         // its CallWatcher, on oFono's word
    status("music", kPaused);
    later(15);
    audiodCall(false);                 // not the end: the keys are held
    EXPECT_EQ(keyEvents(music), std::vector<std::string>{"pause"});
    later(115);
    EXPECT_TRUE(fakebus::cancel(calls));
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play"}));
    status("music", kPlaying);

    // and the other way round: the keys first, audiod's call last
    calls = phone();
    status("music", kPaused);
    audiodCall(true);
    later(30000);
    EXPECT_TRUE(fakebus::cancel(calls));
    status("music", kPlaying);
    status("music", kPaused);
    EXPECT_EQ(keyEvents(music).size(), 3u);
    later(100);
    audiodCall(false);
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play", "pause", "play"}));
}

TEST_F(CallKeysService, OnlyTheFirstHeldBackPlaysNotItsEcho)
{
    // spotify-webos-service has a session of its own, which plays when the
    // Music app tells librespot to. Both paused for the call as the app
    // plays again: the app is played after it, and not its backend too.
    start();
    std::string music = session("music", "org.webosports.app.music", app(":1.1"));
    std::string spotify = session("spotify", "org.webosports.app.music.service", app(":1.4"), false);
    std::string calls = phone();
    later(5000);
    status("music", kPlaying);
    status("spotify", kPlaying);
    status("music", kPaused);
    call("setMediaPlayStatus", "{\"mediaId\":\"spotify\",\"playStatus\":\"PLAYSTATE_STOPPED\"}", app(":1.4"));
    later(400);
    EXPECT_TRUE(fakebus::cancel(calls));
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play"}));
    EXPECT_EQ(keyEvents(spotify), std::vector<std::string>{"pause"});
}

TEST_F(CallKeysService, AGoneSessionMakesWayForTheNext)
{
    start();
    std::string video = session("video", "com.atlas", app(":1.2"));
    std::string music = session("music", "com.music", app(":1.1"));
    std::string calls = phone();
    status("video", kPlaying);
    status("music", kPlaying);
    EXPECT_TRUE(fakebus::cancel(video));
    status("music", kPaused);
    EXPECT_TRUE(fakebus::cancel(calls));
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play"}));
}

TEST_F(CallKeysService, AudiodsPlayAfterTheCallIsCarriedOut)
{
    // A player with no call watcher of its own, playing when the call is
    // answered: audiod pauses it, and after the call sends "play" - before
    // the Phone app has let go of the keys, so it was refused and the
    // player stayed paused.
    start();
    std::string video = session("video", "com.atlas", app(":1.2"));
    status("video", kPlaying);
    std::string calls = phone();
    audiodCall(true);
    EXPECT_TRUE(returnValue(key("pause")));
    status("video", kPaused);
    later(60000);
    audiodCall(false);
    EXPECT_EQ(errorCode(key("play")), MCS_ERROR_IN_CALL);
    later(300);
    EXPECT_TRUE(fakebus::cancel(calls));
    EXPECT_EQ(keyEvents(video), (std::vector<std::string>{"pause", "play"}));
}

TEST_F(CallKeysService, APlayEarlierInTheCallIsNotCarriedOutAfterIt)
{
    // MK-2: the user plays the music in the middle of a call; it is paused
    // at once, and stays paused after the call - as on Android, where a
    // focus request during a call fails unless it accepts a delay. Only a
    // play within kPlayAfterCallMs of the end is taken for the end's own.
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    std::string radio = session("radio", "com.radio", app(":1.3"), false);
    std::string calls = phone();
    audiodCall(true);
    status("music", kPlaying);
    status("music", kPaused);
    later(MediaFocus::kPlayAfterCallMs - 1);
    status("radio", kPlaying);
    status("radio", kPaused);
    later(1);
    audiodCall(false);
    EXPECT_TRUE(fakebus::cancel(calls));
    EXPECT_EQ(keyEvents(music), std::vector<std::string>{"pause"});
    EXPECT_EQ(keyEvents(radio), (std::vector<std::string>{"pause", "play"}));
}

TEST_F(CallKeysService, MusicPausedBeforeTheCallStaysPaused)
{
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    status("music", kPlaying);
    status("music", kPaused);          // the user's
    std::string calls = phone();
    later(5000);
    EXPECT_TRUE(fakebus::cancel(calls));
    later(100);
    std::string again = phone();
    audiodCall(true);
    later(5000);
    audiodCall(false);
    EXPECT_TRUE(fakebus::cancel(again));
    EXPECT_TRUE(keyEvents(music).empty());
}

TEST_F(CallKeysService, OnlyTheEndOfTheLastCallPlays)
{
    // two holders, then audiod's call: none of them going alone ends it
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    std::string a = phone();
    std::string b = phone();
    audiodCall(true);
    status("music", kPlaying);
    status("music", kPaused);
    EXPECT_TRUE(fakebus::cancel(a));
    EXPECT_TRUE(fakebus::cancel(b));
    EXPECT_EQ(keyEvents(music), std::vector<std::string>{"pause"});
    audiodCall(false);
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play"}));
}

TEST_F(CallKeysService, AudiodGoingDownEndsItsCall)
{
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    audiodCall(true);
    status("music", kPlaying);
    status("music", kPaused);
    audiodUp(false);
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play"}));
}

TEST_F(CallKeysService, NothingIsPlayedThatPlaysAlreadyOrHasGone)
{
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    std::string radio = session("radio", "com.radio", app(":1.3"), false);
    std::string calls = phone();
    // the music plays on through the call after kRepauseLimit pauses
    for (unsigned i = 0; i <= MediaFocus::kRepauseLimit; i++) {
        status("music", kPlaying);
        if (i < MediaFocus::kRepauseLimit)
            status("music", kPaused);
    }
    EXPECT_EQ(keyEvents(music).size(), MediaFocus::kRepauseLimit);
    // the radio is paused, and its app goes
    status("radio", kPlaying);
    EXPECT_EQ(keyEvents(radio), std::vector<std::string>{"pause"});
    EXPECT_TRUE(fakebus::cancel(radio));
    size_t replied = fakebus::replies().size();
    EXPECT_TRUE(fakebus::cancel(calls));
    EXPECT_EQ(keyEvents(music).size(), MediaFocus::kRepauseLimit) << "a session playing was sent play";
    EXPECT_EQ(fakebus::replies().size(), replied) << "something was sent after the call";
}

TEST_F(CallKeysService, AudiodsPlayAfterTheEndIsNotSentTwice)
{
    // audiod's call ending before its own play comes: the play held back
    // goes as the call ends, and audiod's is not sent after it - until the
    // session says what it does, or is sent another key.
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    audiodCall(true);
    status("music", kPlaying);
    status("music", kPaused);
    audiodCall(false);
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play"}));
    EXPECT_TRUE(returnValue(key("play")));
    EXPECT_EQ(keyEvents(music).size(), 2u) << "play sent twice after the call";
    // another key in between: a play after it is the user's
    EXPECT_TRUE(returnValue(key("pause")));
    EXPECT_TRUE(returnValue(key("play")));
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play", "pause", "play"}));

    // and once it has said it plays, a play is sent as any other
    audiodCall(true);
    status("music", kPlaying);
    status("music", kPaused);
    audiodCall(false);
    EXPECT_EQ(keyEvents(music).size(), 6u);
    status("music", kPlaying);
    status("music", kPaused);
    EXPECT_TRUE(returnValue(key("play")));
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play", "pause", "play", "pause", "play",
                                                          "play"}));
}

TEST_F(CallKeysService, AKeyRefusedOtherThanPlayIsNotCarriedOut)
{
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    std::string calls = phone();
    EXPECT_EQ(errorCode(key("next")), MCS_ERROR_IN_CALL);
    EXPECT_EQ(errorCode(key("previous")), MCS_ERROR_IN_CALL);
    EXPECT_TRUE(fakebus::cancel(calls));
    EXPECT_TRUE(keyEvents(music).empty());
}

} // namespace
