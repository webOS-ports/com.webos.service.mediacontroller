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
