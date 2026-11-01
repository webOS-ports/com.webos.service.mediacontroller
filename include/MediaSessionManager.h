// Copyright (c) 2020-2024 LG Electronics, Inc.
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

#ifndef MEDIA_SESSION_MANAGER_H_
#define MEDIA_SESSION_MANAGER_H_

/*-----------------------------------------------------------------------------
    (File Inclusions)
------------------------------------------------------------------------------*/
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "MediaControlTypes.h"
#include "MediaFocus.h"
#include "RequestReceiver.h"

class MediaSessionManager
{
private:
  std::map<std::string, mediaSession> mapMediaSessionInfo_;
  RequestReceiver objRequestRcvr_;
  MediaFocus focus_;

  //! The session, or null - logged as caller's - if there is none.
  mediaSession *find(const std::string& mediaId, const char *caller);

public:
  MediaSessionManager();
  ~MediaSessionManager();
  MediaSessionManager(const MediaSessionManager&) = delete;
  MediaSessionManager& operator=(const MediaSessionManager&) = delete;

  int addMediaSession (const std::string& mediaId,
                       const std::string& appId);
  int activateMediaSession (const std::string& mediaId);
  int deactivateMediaSession (const std::string& mediaId);
  int removeMediaSession (const std::string& mediaId);
  //! Every session gone, active or not.
  void clear();
  int getMediaMetaData(const std::string& mediaId,
                       mediaMetaData& objMetaData);
  int getMediaCoverArt(const std::string& mediaId,
                       std::vector<mediaCoverArt>& objMetaData);
  int getActionList(const std::string& mediaId,
                    std::vector<std::string>& objActionList);
  int getMediaSessionInfo(const std::string& mediaId,
                          mediaSession& objMediaSession);
  int getMediaPlayStatus(const std::string& mediaId,
                         std::string& playStatus);
  int getMediaMuteStatus(const std::string& mediaId,
                         std::string& muteStatus);
  int getMediaPlayPosition(const std::string& mediaId,
                           std::string& playPosition);
  int setMediaMetaData(const std::string& mediaId,
                       const mediaMetaData& objMetaData);
  int setMediaCoverArt(const std::string& mediaId,
                       const std::vector<mediaCoverArt>& objCoverArt);
  int setMediaAction(const std::string& mediaId,
                     const std::vector<std::string>& mediaAction);
  //! Records playStatus, reported at nowMs (monotonic, milliseconds).
  //! When it starts mediaId playing (see MediaFocus) the session becomes
  //! the active one, and toPause gets the sessions it stops - unless
  //! refuseStart (a call is up): then refused is set, and the start leaves
  //! the active session and the others as they were, and the play is held
  //! back for the call's end (MediaFocus::deferPlay).
  int setMediaPlayStatus(const std::string& mediaId,
                         const std::string& playStatus,
                         std::int64_t nowMs,
                         bool refuseStart,
                         std::vector<std::string>& toPause,
                         bool& refused);
  //! A key that would have played mediaId was refused for a call at
  //! nowMs: see MediaFocus::deferPlay.
  void deferPlay(const std::string& mediaId, std::int64_t nowMs);
  //! The call has ended at nowMs: the session to play now, or "". Of those
  //! whose play was held back within MediaFocus::kPlayAfterCallMs and that
  //! are there still, the first held back - one, as only one would play
  //! on: the last started wins. The first is the one that played on its
  //! own; one that played after it may be its echo (spotify-webos-service's
  //! session, which plays when the Music app tells it to). None if that
  //! one plays already - or, if it only still says so, its pause for the
  //! call not reported yet: then its play waits for that (MediaFocus::
  //! awaitPause), and playAfterPause says when.
  std::string playAfterCall(std::int64_t nowMs);
  //! mediaId has reported playStatus at nowMs: whether to play it now, as
  //! its play was waiting for this.
  bool playAfterPause(const std::string& mediaId, const std::string& playStatus, std::int64_t nowMs);
  //! See MediaFocus::playSent, keySent and playOnItsWay.
  void playSent(const std::string& mediaId) { focus_.playSent(mediaId); }
  void keySent(const std::string& mediaId) { focus_.keySent(mediaId); }
  bool playOnItsWay(const std::string& mediaId, std::int64_t nowMs) const {
    return focus_.playOnItsWay(mediaId, nowMs);
  }
  int setMediaMuteStatus(const std::string& mediaId,
                         const std::string& muteStatus);
  int setMediaPlayPosition(const std::string& mediaId,
                           const std::string& playPosition);
  std::vector<std::string> getMediaSessionList(const std::string& appId) const;
  //! Active sessions, the last activated first.
  std::vector<std::string> getActiveMediaSessionList() const;
  std::string getCurrentActiveSession() const;
  static bool validatePlayStatus(const std::string& playStatus);
  int getDisplayIdForMedia(const std::string& mediaId) const;
  std::string getMediaIdFromDisplayId(int displayId) const;
  //! The display an app is on: with FEATURE_DUAL_DISPLAY the last
  //! character of its appId, when that is a digit; else 0.
  static int displayIdFromAppId(const std::string& appId);
};

#endif /*MEDIA_SESSION_MANAGER_H_*/
