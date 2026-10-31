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

#ifndef MEDIA_FOCUS_H_
#define MEDIA_FOCUS_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/**
 * Who has the media keys, and who stops for whom.
 *
 * A session that starts playing takes the keys, as on Android and iOS: the
 * headset's play/pause goes to what the user last started, not to whatever
 * last called activateMediaSession. And the last one started wins: the
 * other apps' sessions on its display that are playing are paused, as
 * webOS 2.x's mediaserver and Ubuntu Touch's media-hub did.
 *
 * Only a start counts. A session that stopped playing and plays again
 * within kCarryOnMs carries on: the Music app reports PAUSED and PLAYING
 * within a second while the music never stops, and a short PLAYING after
 * a pause. Taken as starts, those would take the keys from the video the
 * user is watching and pause it.
 */
class MediaFocus
{
public:
  static constexpr std::int64_t kCarryOnMs = 2000;

  struct Session
  {
    std::string mediaId;
    std::string appId;
    int displayId;
    std::string playStatus;
  };

  //! Notes mediaId's playStatus, reported at nowMs (a monotonic clock in
  //! milliseconds); true when it is a start.
  bool update(const std::string& mediaId, const std::string& playStatus, std::int64_t nowMs);
  //! The session has gone.
  void forget(const std::string& mediaId);
  void clear();

  //! Sound is coming, or about to: playing, seeking or buffering.
  static bool isPlaying(const std::string& playStatus);

  //! What starter starting pauses: the sessions in sessions that are
  //! playing, on its display, of other apps. An app with two sessions sorts
  //! them out itself, as a browser does its tabs.
  static std::vector<std::string> toPause(const Session& starter, const std::vector<Session>& sessions);

  //! The key event to send for keyEvent to a session in playStatus.
  //! "playPause", a headset's one button, is "pause" while it plays and
  //! "play" otherwise: apps know play and pause, not a toggle. Any other
  //! key is sent as it is.
  static std::string resolveKey(const std::string& keyEvent, const std::string& playStatus);

  /**
   * What audiod (com.palm.audio/getStatus) says about calls and where the
   * sound goes. Each field counts only once audiod has said it: before
   * that, and while audiod is down, the keys go as they always did.
   */
  struct Audio
  {
    bool inCallKnown = false;
    bool inCall = false;               //!< a call is up (carrier or VoIP)
    bool bluetoothAudioKnown = false;
    bool bluetoothAudio = false;       //!< a Bluetooth headset is the output
  };

  //! Whether a Bluetooth headset's key (AVRCP passthrough) goes to a
  //! session. Not during a call: a tap on the headset is meant for the call,
  //! and on Android Telecom's exclusive session swallows every media key
  //! then. Not while the sound goes elsewhere (headphones in the jack, the
  //! speaker): a headset sends PAUSE by itself when it is taken off or its
  //! stream stops, and that must not pause music it no longer plays, as
  //! Android drops passthrough from a device that is not the active one.
  static bool takesBluetoothKey(const Audio& audio);

  //! Whether key, as resolved, goes to a session from injectMediaKeyEvent
  //! (the shell's hardware keys, audiod). During a call only "pause" and
  //! "stop": audiod pauses the media for the call with them, and nothing
  //! is started while the call is up.
  static bool takesKey(const Audio& audio, const std::string& key);

  //! The keys that are the call's while the Phone app holds
  //! registerCallKeys: the wired headset's button, which the shell sends
  //! as "playPause" (a press) and "hold" (a long press). Nothing else sends
  //! these two: audiod sends "pause" and "play", and a Bluetooth headset's
  //! keys come by AVRCP, not through injectMediaKeyEvent.
  static bool isCallKey(const std::string& keyEvent);

  //! audio, with a call taken as up while the Phone app holds
  //! registerCallKeys: it does from the moment a call rings until the last
  //! call ends, and audiod says nothing of a call that only rings.
  static Audio withCall(Audio audio, bool callKeysHeld);

  //! Whether a session that starts playing is paused again, and leaves
  //! the keys and the other sessions as they were: during a call, as on
  //! Android, where no media gets focus while a call holds it.
  static bool refusesStart(const Audio& audio);

private:
  struct State
  {
    bool playing = false;
    bool stopped = false;        //!< has played and stopped
    std::int64_t stoppedAt = 0;  //!< when, if it has
  };
  std::map<std::string, State> states_;
};

#endif /*MEDIA_FOCUS_H_*/
