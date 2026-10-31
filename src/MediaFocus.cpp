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

#include <algorithm>
#include <utility>

bool MediaFocus::update(const std::string& mediaId, const std::string& playStatus, std::int64_t nowMs) {
  State& state = states_[mediaId];
  state.playOnItsWay = false;
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

bool MediaFocus::repause(const std::string& mediaId, std::int64_t nowMs) {
  std::vector<std::int64_t>& at = states_[mediaId].repausedAt;
  std::erase_if(at, [nowMs](std::int64_t t) { return nowMs - t >= kCarryOnMs; });
  if (at.size() >= kRepauseLimit)
    return false;
  at.push_back(nowMs);
  return true;
}

void MediaFocus::deferPlay(const std::string& mediaId, std::int64_t nowMs) {
  State& state = states_[mediaId];
  state.playDeferredAt = nowMs;
  state.playDeferredOrder = ++deferred_;
}

std::vector<std::string> MediaFocus::playsAfterCall(std::int64_t nowMs) {
  std::vector<std::pair<std::uint64_t, std::string>> held;
  for (auto& [mediaId, state] : states_) {
    if (state.playDeferredAt && nowMs - *state.playDeferredAt < kPlayAfterCallMs)
      held.emplace_back(state.playDeferredOrder, mediaId);
    state.playDeferredAt.reset();
  }
  std::sort(held.begin(), held.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<std::string> plays(held.size());
  std::transform(held.begin(), held.end(), plays.begin(), [](auto& h) { return std::move(h.second); });
  return plays;
}

void MediaFocus::playSent(const std::string& mediaId) {
  states_[mediaId].playOnItsWay = true;
}

void MediaFocus::keySent(const std::string& mediaId) {
  auto state = states_.find(mediaId);
  if (state != states_.end())
    state->second.playOnItsWay = false;
}

bool MediaFocus::playOnItsWay(const std::string& mediaId) const {
  auto state = states_.find(mediaId);
  return state != states_.end() && state->second.playOnItsWay;
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

std::string MediaFocus::resolveKey(const std::string& keyEvent, const std::string& playStatus) {
  if (keyEvent != "playPause")
    return keyEvent;
  return isPlaying(playStatus) ? "pause" : "play";
}

bool MediaFocus::takesBluetoothKey(const Audio& audio) {
  if (audio.inCallKnown && audio.inCall)
    return false;
  return !audio.bluetoothAudioKnown || audio.bluetoothAudio;
}

bool MediaFocus::takesKey(const Audio& audio, const std::string& key) {
  if (!audio.inCallKnown || !audio.inCall)
    return true;
  return key == "pause" || key == "stop";
}

bool MediaFocus::isCallKey(const std::string& keyEvent) {
  return keyEvent == "playPause" || keyEvent == "hold";
}

MediaFocus::Audio MediaFocus::withCall(Audio audio, bool callKeysHeld) {
  if (callKeysHeld) {
    audio.inCallKnown = true;
    audio.inCall = true;
  }
  return audio;
}

bool MediaFocus::refusesStart(const Audio& audio) {
  return audio.inCallKnown && audio.inCall;
}

bool MediaFocus::callEnded(const Audio& before, const Audio& after) {
  return refusesStart(before) && !refusesStart(after);
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
