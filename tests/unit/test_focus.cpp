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
#include "MediaFocus.h"

namespace {

const char* const kPlaying = "PLAYSTATE_PLAYING";
const char* const kPaused = "PLAYSTATE_PAUSED";

// The rules on their own, with the clock in the test's hands.

TEST(Focus, TheFirstPlayIsAStart)
{
    MediaFocus focus;
    EXPECT_FALSE(focus.update("m1", "PLAYSTATE_NONE", 0));
    EXPECT_TRUE(focus.update("m1", kPlaying, 10));
}

TEST(Focus, PlayingAgainWhilePlayingIsNotAStart)
{
    MediaFocus focus;
    EXPECT_TRUE(focus.update("m1", kPlaying, 0));
    EXPECT_FALSE(focus.update("m1", kPlaying, 50000));
}

TEST(Focus, PausedAndPlayingWithinASecondCarriesOn)
{
    // The Music app, 30 October 14:30:24: PAUSED and PLAYING in the same
    // second, the music never stopping.
    MediaFocus focus;
    EXPECT_TRUE(focus.update("music", kPlaying, 0));
    EXPECT_FALSE(focus.update("music", kPaused, 60000));
    EXPECT_FALSE(focus.update("music", kPlaying, 60300));
}

TEST(Focus, PlayingAgainAfterTheCarryOnTimeIsAStart)
{
    MediaFocus focus;
    focus.update("m1", kPlaying, 0);
    focus.update("m1", kPaused, 10000);
    EXPECT_FALSE(focus.update("m1", kPlaying, 10000 + MediaFocus::kCarryOnMs - 1));
    focus.update("m1", kPaused, 20000);
    EXPECT_TRUE(focus.update("m1", kPlaying, 20000 + MediaFocus::kCarryOnMs));
}

TEST(Focus, EveryWayOfStoppingCounts)
{
    for (const char* stop : {"PLAYSTATE_NONE", "PLAYSTATE_STOPPED", kPaused, "PLAYSTATE_ERROR"}) {
        MediaFocus focus;
        focus.update("m1", kPlaying, 0);
        focus.update("m1", stop, 10000);
        EXPECT_FALSE(focus.update("m1", kPlaying, 10100)) << stop;
        focus.update("m1", stop, 20000);
        EXPECT_TRUE(focus.update("m1", kPlaying, 30000)) << stop;
    }
}

TEST(Focus, SeekingAndBufferingNeitherStartNorStop)
{
    MediaFocus focus;
    focus.update("m1", kPlaying, 0);
    for (const char* s : {"PLAYSTATE_BUFFERING", "PLAYSTATE_FAST_FORWARDING", "PLAYSTATE_REWINDING"})
        EXPECT_FALSE(focus.update("m1", s, 1000)) << s;
    EXPECT_FALSE(focus.update("m1", kPlaying, 60000)) << "buffering for a minute is not a new start";

    // a video buffering before it first plays starts when it plays
    MediaFocus fresh;
    EXPECT_FALSE(fresh.update("v", "PLAYSTATE_BUFFERING", 0));
    EXPECT_TRUE(fresh.update("v", kPlaying, 500));
}

TEST(Focus, AForgottenSessionStartsAfresh)
{
    MediaFocus focus;
    focus.update("m1", kPlaying, 0);
    focus.update("m1", kPaused, 1000);
    focus.forget("m1");
    EXPECT_TRUE(focus.update("m1", kPlaying, 1100));
    focus.update("m1", kPaused, 2000);
    focus.clear();
    EXPECT_TRUE(focus.update("m1", kPlaying, 2100));
}

TEST(Focus, SessionsAreKeptApart)
{
    MediaFocus focus;
    focus.update("m1", kPlaying, 0);
    focus.update("m1", kPaused, 1000);
    EXPECT_TRUE(focus.update("m2", kPlaying, 1100));
}

TEST(Focus, AStartPausesOtherAppsPlayingOnItsDisplay)
{
    const MediaFocus::Session video{"video", "com.atlas", 0, kPlaying};
    const std::vector<MediaFocus::Session> sessions = {
        video,
        {"music", "com.music", 0, kPlaying},
        {"podcast", "com.podcast", 0, "PLAYSTATE_BUFFERING"},
        {"seeking", "com.seek", 0, "PLAYSTATE_FAST_FORWARDING"},
        {"paused", "com.paused", 0, kPaused},
        {"stopped", "com.stopped", 0, "PLAYSTATE_STOPPED"},
        {"tab2", "com.atlas", 0, kPlaying},      // its own app's other tab
        {"rear", "com.rear", 1, kPlaying},       // another display
    };
    EXPECT_EQ(MediaFocus::toPause(video, sessions),
              (std::vector<std::string>{"music", "podcast", "seeking"}));
    EXPECT_TRUE(MediaFocus::toPause(video, {video}).empty());
}

TEST(Focus, TheHeadsetsPlayPauseIsPauseWhilePlayingAndPlayOtherwise)
{
    for (const char* sounding : {kPlaying, "PLAYSTATE_FAST_FORWARDING", "PLAYSTATE_REWINDING",
                                 "PLAYSTATE_BUFFERING"})
        EXPECT_EQ(MediaFocus::resolveKey("playPause", sounding), "pause") << sounding;
    for (const char* quiet : {kPaused, "PLAYSTATE_STOPPED", "PLAYSTATE_NONE", ""})
        EXPECT_EQ(MediaFocus::resolveKey("playPause", quiet), "play") << quiet;
}

TEST(Focus, OtherKeysGoAsTheyAre)
{
    for (const char* key : {"play", "pause", "stop", "next", "previous", "PlayPause", "toggle"})
        EXPECT_EQ(MediaFocus::resolveKey(key, kPlaying), key);
}

// Through the service.

typedef McsTest FocusService;

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

void status(McsTest* t, const std::string& mediaId, const char* playStatus)
{
    (void)t;
    fakebus::callMethod("setMediaPlayStatus",
                        "{\"mediaId\":\"" + mediaId + "\",\"playStatus\":\"" + playStatus + "\"}");
}

std::string keyTarget()
{
    fakebus::callMethod("injectMediaKeyEvent", "{\"displayId\":0,\"keyEvent\":\"pause\"}");
    return fakebus::repliesTo(fakebus::lastToken()).at(0);
}

TEST_F(FocusService, StartingToPlayTakesTheKeysWithoutActivating)
{
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    std::string video = session("video", "com.atlas", app(":1.2"), false);
    status(this, "music", kPlaying);
    status(this, "video", kPlaying);
    keyTarget();
    EXPECT_EQ(keyEvents(video), (std::vector<std::string>{"pause"}))
        << "the key did not go to the video, which started last";
}

TEST_F(FocusService, TheLastOneStartedWins)
{
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    std::string video = session("video", "com.atlas", app(":1.2"));
    status(this, "music", kPlaying);
    EXPECT_TRUE(keyEvents(video).empty()) << "the video was not playing";
    status(this, "video", kPlaying);
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause"}));
    EXPECT_TRUE(keyEvents(video).empty()) << "the starter paused itself";
}

TEST_F(FocusService, ThePlayStatusStillRepliesOnceAndIsBroadcast)
{
    start();
    session("music", "com.music", app(":1.1"));
    session("video", "com.atlas", app(":1.2"));
    status(this, "music", kPlaying);
    EXPECT_TRUE(returnValue(call("setMediaPlayStatus", "{\"mediaId\":\"video\",\"playStatus\":\"PLAYSTATE_PLAYING\"}")));
    EXPECT_EQ(json(call("getMediaPlayStatus", "{\"mediaId\":\"video\"}"))["playStatus"].asString(), kPlaying);
}

TEST_F(FocusService, TheMusicAppsFlickerTakesNeitherTheKeysNorTheVideo)
{
    // 30 October, mindphone: Spotify in the Music app, then a web video in
    // Atlas. With "last one wins" the music is paused for the video; the
    // Music app then reports a short PLAYING after its pause. That must not
    // pause the video, nor take its keys.
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    std::string video = session("video", "com.atlas", app(":1.2"), false);
    status(this, "music", kPlaying);
    status(this, "video", kPlaying);
    ASSERT_EQ(keyEvents(music), (std::vector<std::string>{"pause"}));
    status(this, "music", kPaused);
    status(this, "music", kPlaying);   // the flicker
    status(this, "music", kPaused);
    EXPECT_TRUE(keyEvents(video).empty()) << "the flicker paused the video";
    keyTarget();
    EXPECT_EQ(keyEvents(video), (std::vector<std::string>{"pause"})) << "the flicker took the keys";
    EXPECT_EQ(keyEvents(music).size(), 1u);
}

TEST_F(FocusService, PausedSessionsAndTheSameAppAreLeftAlone)
{
    start();
    std::string paused = session("paused", "com.paused", app(":1.1"));
    std::string tab1 = session("tab1", "com.atlas", app(":1.2"));
    std::string tab2 = session("tab2", "com.atlas", app(":1.2"));
    status(this, "paused", kPlaying);
    status(this, "paused", kPaused);
    status(this, "tab1", kPlaying);
    status(this, "tab2", kPlaying);
    EXPECT_TRUE(keyEvents(paused).empty()) << "a paused session was paused again";
    EXPECT_TRUE(keyEvents(tab1).empty()) << "a tab paused its own app's other tab";
}

TEST_F(FocusService, AStartTellsTheHeadsetItsState)
{
    // The headset hears only from the session its keys go to; a session
    // that starts is that one now.
    start();
    session("music", "com.music", app(":1.1"));
    session("video", "com.atlas", app(":1.2"), false);
    std::string active = json(call("getActiveMediaSessions", "{}"))["sessionList"][0].asString();
    EXPECT_EQ(active, "music");
    status(this, "video", kPlaying);
    active = json(call("getActiveMediaSessions", "{}"))["sessionList"][0].asString();
    EXPECT_EQ(active, "video");
}

TEST_F(FocusService, AnUnregisteredSessionStartsAfresh)
{
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    session("video", "com.atlas", app(":1.2"));
    status(this, "video", kPlaying);
    status(this, "video", kPaused);
    EXPECT_TRUE(returnValue(call("unregisterMediaSession", "{\"mediaId\":\"video\"}")));
    status(this, "music", kPlaying);
    std::string again = session("video", "com.atlas", app(":1.2"), false);
    status(this, "video", kPlaying);
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause"}))
        << "the new session was taken for the old one carrying on";
}

TEST_F(FocusService, TheHeadsetButtonPausesWhatPlaysAndPlaysWhatIsPaused)
{
    // The shell sends the wired headset's button as "playPause"; the Music
    // app knows only play and pause.
    start();
    std::string music = session("music", "com.music", app(":1.1"));
    status(this, "music", kPlaying);
    EXPECT_TRUE(returnValue(call("injectMediaKeyEvent", "{\"displayId\":0,\"keyEvent\":\"playPause\"}")));
    status(this, "music", kPaused);
    EXPECT_TRUE(returnValue(call("injectMediaKeyEvent", "{\"displayId\":0,\"keyEvent\":\"playPause\"}")));
    EXPECT_TRUE(returnValue(call("injectMediaKeyEvent", "{\"displayId\":0,\"keyEvent\":\"next\"}")));
    EXPECT_EQ(keyEvents(music), (std::vector<std::string>{"pause", "play", "next"}));
}

} // namespace
