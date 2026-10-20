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

/*-----------------------------------------------------------------------------*/
#include "MediaSessionManager.h"
#include "MediaControlTypes.h"

#include <algorithm>
#include <iterator>

MediaSessionManager::MediaSessionManager() :
  mapMediaSessionInfo_() {
}

MediaSessionManager::~MediaSessionManager() {
}

mediaSession *MediaSessionManager::find(const std::string& mediaId, const char *caller) {
  auto itr = mapMediaSessionInfo_.find(mediaId);
  if (itr != mapMediaSessionInfo_.end())
    return &itr->second;
  PMLOG_ERROR(CONST_MODULE_MSM, "%s no session %s", caller, mediaId.c_str());
  return nullptr;
}

int MediaSessionManager::addMediaSession(const std::string& mediaId,
                                         const std::string& appId) {
  PMLOG_INFO(CONST_MODULE_MSM, "%s mediaId : %s , appId : %s", __FUNCTION__, mediaId.c_str(), appId.c_str());
  if (mapMediaSessionInfo_.count(mediaId)) {
    PMLOG_ERROR(CONST_MODULE_MSM, "%s mediaId already registered", __FUNCTION__);
    return MCS_ERROR_SESSION_ALREADY_REGISTERED;
  }
  mapMediaSessionInfo_.emplace(mediaId, mediaSession(mediaId, appId));
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::activateMediaSession(const std::string& mediaId) {
  if (!find(mediaId, __FUNCTION__))
    return MCS_ERROR_INVALID_MEDIAID;
  objRequestRcvr_.addClient(mediaId);
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::deactivateMediaSession(const std::string& mediaId) {
  if (!find(mediaId, __FUNCTION__))
    return MCS_ERROR_INVALID_MEDIAID;
  objRequestRcvr_.removeClient(mediaId);
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::removeMediaSession(const std::string& mediaId) {
  PMLOG_INFO(CONST_MODULE_MSM, "%s mediaId : %s", __FUNCTION__, mediaId.c_str());
  if (mapMediaSessionInfo_.erase(mediaId) == 0) {
    PMLOG_ERROR(CONST_MODULE_MSM, "%s no session %s", __FUNCTION__, mediaId.c_str());
    return MCS_ERROR_INVALID_MEDIAID;
  }
  objRequestRcvr_.removeClient(mediaId);
  return MCS_ERROR_NO_ERROR;
}

void MediaSessionManager::clear() {
  for (const auto& session : mapMediaSessionInfo_)
    objRequestRcvr_.removeClient(session.first);
  mapMediaSessionInfo_.clear();
}

int MediaSessionManager::getMediaMetaData(const std::string& mediaId,
                                          mediaMetaData& objMetaData) {
  const mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  objMetaData = session->getMediaMetaDataObj();
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::getMediaCoverArt(const std::string& mediaId,
                                          std::vector<mediaCoverArt>& objCoverArt) {
  const mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  objCoverArt = session->getMediaCoverArtObj();
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::getActionList(const std::string& mediaId,
                                       std::vector<std::string>& objActionList) {
  const mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  objActionList = session->getActionObj();
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::getMediaSessionInfo(const std::string& mediaId,
                                             mediaSession& objMediaSession) {
  const mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  objMediaSession = *session;
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::getMediaPlayStatus(const std::string& mediaId,
                                            std::string& playStatus) {
  const mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  playStatus = session->getPlayStatus();
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::getMediaMuteStatus(const std::string& mediaId,
                                            std::string& muteStatus) {
  const mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  muteStatus = session->getMuteStatus();
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::getMediaPlayPosition(const std::string& mediaId,
                                              std::string& playPosition) {
  const mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  playPosition = session->getPlayposition();
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::setMediaMetaData(const std::string& mediaId,
                                          const mediaMetaData& objMetaData) {
  mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  session->setMetaData(objMetaData);
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::setMediaCoverArt(const std::string& mediaId,
                                          const std::vector<mediaCoverArt>& objCoverArt) {
  mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  session->setCoverArt(objCoverArt);
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::setMediaAction(const std::string& mediaId,
                                        const std::vector<std::string>& mediaAction) {
  mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  session->setAction(mediaAction);
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::setMediaPlayStatus(const std::string& mediaId,
                                            const std::string& playStatus) {
  PMLOG_INFO(CONST_MODULE_MSM, "%s mediaId : %s playStatus : %s", __FUNCTION__,
             mediaId.c_str(), playStatus.c_str());
  if (!validatePlayStatus(playStatus))
    return MCS_ERROR_SESSION_INVALID_PLAY_STATE;
  mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  session->setPlayStatus(playStatus);
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::setMediaMuteStatus(const std::string& mediaId,
                                            const std::string& muteStatus) {
  mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  session->setMuteStatus(muteStatus);
  return MCS_ERROR_NO_ERROR;
}

int MediaSessionManager::setMediaPlayPosition(const std::string& mediaId,
                                              const std::string& playPosition) {
  mediaSession *session = find(mediaId, __FUNCTION__);
  if (!session)
    return MCS_ERROR_INVALID_MEDIAID;
  session->setPlayposition(playPosition);
  return MCS_ERROR_NO_ERROR;
}

std::vector<std::string> MediaSessionManager::getActiveMediaSessionList() const {
  const auto& clients = objRequestRcvr_.getClientList();
  return std::vector<std::string>(clients.begin(), clients.end());
}

std::vector<std::string> MediaSessionManager::getMediaSessionList(const std::string& appId) const {
  std::vector<std::string> mediaSessionId;
  for (const auto& itr : mapMediaSessionInfo_) {
    if (appId == itr.second.getAppId())
      mediaSessionId.push_back(itr.first);
  }
  return mediaSessionId;
}

std::string MediaSessionManager::getCurrentActiveSession() const {
  return objRequestRcvr_.getLastActiveClient();
}

bool MediaSessionManager::validatePlayStatus(const std::string& playStatus) {
  static const char *const states[] = {
    "PLAYSTATE_NONE", "PLAYSTATE_STOPPED", "PLAYSTATE_PAUSED", "PLAYSTATE_PLAYING",
    "PLAYSTATE_FAST_FORWARDING", "PLAYSTATE_REWINDING", "PLAYSTATE_BUFFERING", "PLAYSTATE_ERROR"
  };
  return std::any_of(std::begin(states), std::end(states),
                     [&playStatus](const char *state) { return playStatus == state; });
}

int MediaSessionManager::displayIdFromAppId(const std::string& appId) {
#if defined(FEATURE_DUAL_DISPLAY)
  if (!appId.empty() && appId.back() >= '0' && appId.back() <= '9')
    return appId.back() - '0';
#else
  (void)appId;
#endif
  return 0;
}

int MediaSessionManager::getDisplayIdForMedia(const std::string& mediaId) const {
  const auto& itr = mapMediaSessionInfo_.find(mediaId);
  if (itr == mapMediaSessionInfo_.end())
    return 0;
  return displayIdFromAppId(itr->second.getAppId());
}

std::string MediaSessionManager::getMediaIdFromDisplayId(int displayId) const {
  //the session on that display that was activated last
  for (const auto& mediaId : objRequestRcvr_.getClientList()) {
    if (getDisplayIdForMedia(mediaId) == displayId)
      return mediaId;
  }
  return CSTR_EMPTY;
}
