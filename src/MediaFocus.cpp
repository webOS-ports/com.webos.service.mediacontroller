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

/*-----------------------------------------------------------------------------*/
#include "MediaFocus.h"

bool MediaFocus::update(const std::string& mediaId, const std::string& playStatus, std::int64_t nowMs) {
  State& state = states_[mediaId];
  if (playStatus == "PLAYSTATE_PLAYING") {
    if (state.playing)
      return false;
    state.playing = true;
    return !(state.stopped && nowMs - state.stoppedAt < kCarryOnMs);
  }
  //seeking and buffering neither start nor stop it
  if (isPlaying(playStatus))
    return false;
  if (state.playing) {
    state.playing = false;
    state.stopped = true;
    state.stoppedAt = nowMs;
  }
  return false;
}

void MediaFocus::forget(const std::string& mediaId) {
  states_.erase(mediaId);
}

void MediaFocus::clear() {
  states_.clear();
}

bool MediaFocus::isPlaying(const std::string& playStatus) {
  return playStatus == "PLAYSTATE_PLAYING" || playStatus == "PLAYSTATE_FAST_FORWARDING" ||
         playStatus == "PLAYSTATE_REWINDING" || playStatus == "PLAYSTATE_BUFFERING";
}

std::vector<std::string> MediaFocus::toPause(const Session& starter, const std::vector<Session>& sessions) {
  std::vector<std::string> paused;
  for (const auto& session : sessions) {
    if (session.mediaId == starter.mediaId || session.appId == starter.appId ||
        session.displayId != starter.displayId || !isPlaying(session.playStatus))
      continue;
    paused.push_back(session.mediaId);
  }
  return paused;
}
