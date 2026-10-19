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

#ifndef MEDIA_CONTROL_SERVICE_H_
#define MEDIA_CONTROL_SERVICE_H_

/*-----------------------------------------------------------------------------
 (File Inclusions)
 ------------------------------------------------------------------------------*/
#include <luna-service2/lunaservice.h>
#include <glib.h>
#include <pbnjson.hpp>

#include <map>
#include <string>

#include "MediaSessionManager.h"
#include "MediaControlPrivate.h"

#if MCS_COVER_ART
#include <memory>
#include "CoverArtFetcher.h"
#endif

/*
 * The service. start() registers it on the bus and attaches it to the main
 * loop given; running the loop is the caller's. The destructor, or stop(),
 * takes it off the bus again.
 */
class MediaControlService
{
public:
  MediaControlService();
  ~MediaControlService();

  MediaControlService(MediaControlService const &) = delete;
  MediaControlService(MediaControlService &&) = delete;
  MediaControlService &operator=(MediaControlService const &) = delete;
  MediaControlService &operator=(MediaControlService &&) = delete;

  bool start(GMainLoop *loop);
  void stop();

  bool registerMediaSession(LSMessage &);
  bool unregisterMediaSession(LSMessage &);
  bool activateMediaSession(LSMessage &);
  bool deactivateMediaSession(LSMessage &);
  bool getMediaMetaData(LSMessage &);
  bool getMediaPlayStatus(LSMessage &);
  bool getMediaSessionInfo(LSMessage &);
  bool getMediaSessionId(LSMessage &);
  bool getActiveMediaSessions(LSMessage &);
#if MCS_COVER_ART
  //! At most this many sources in one getMediaCoverArtPath call.
  static constexpr size_t MAX_COVER_ART_SOURCES = 8;
  bool getMediaCoverArtPath(LSMessage &);
#endif
  bool setSupportedActions(LSMessage &);
  bool setMediaMetaData(LSMessage &);
  bool setMediaPlayStatus(LSMessage &);
  bool setMediaMuteStatus (LSMessage &);
  bool setMediaPlayPosition (LSMessage &);
  bool setMediaCoverArt (LSMessage &);
  bool receiveMediaPlaybackInfo (LSMessage &);
  bool injectMediaKeyEvent (LSMessage &);
#if USE_TEST_METHOD
  bool testKeyEvent(LSMessage &);
#endif
  static bool onSubscriptionCancel(LSHandle *lshandle, LSMessage *message, void *ctx);
  static bool onBTServerStatusCb(LSHandle *lshandle, LSMessage *message, void *ctx);
  static bool onBTAdapterQueryCb(LSHandle *lshandle, LSMessage *message, void *ctx);
  static bool onBTDeviceGetStatusCb(LSHandle *lshandle, LSMessage *message, void *ctx);
  static bool onBTAvrcpGetStatusCb(LSHandle *lshandle, LSMessage *message, void *ctx);
  static bool onBTAvrcpKeyEventsCb(LSHandle *lshandle, LSMessage *message, void *ctx);
  int updateMetaDataResponse(const std::string &,  pbnjson::JObject &);

private:
  void subscribeToBTAdapterGetStatus();
  void dropBluetooth();
  void dropBTDevice(const std::string& address, const std::string& adapterAddress);
  void cancelCall(LSMessageToken& token);
  void notifyBTPlayStatus(const std::string& mediaId, const std::string& playStatus);
  bool sendKeyEvent(const std::string& mediaId, const std::string& keyEvent);
  void broadcastPlaybackInfo(const std::string& mediaId, const std::string& eventType,
                             pbnjson::JValue payload);
  void releaseSessionClient(const std::string& mediaId);

  LSHandle *lsHandle_ = nullptr;
  MediaSessionManager mediaSessionMgr_;
  MediaControlPrivate mediaControlPrivate_;

  /* The registerMediaSession call of each session that subscribed, held
   * (LSMessageRef) until the session ends. */
  std::map<std::string, LSMessage *> sessionClients_;

#if MCS_COVER_ART
  void onCoverArtFetched(const CoverArtFetcher::Result &result);
  std::unique_ptr<CoverArtFetcher> coverArt_;
  /* The getMediaCoverArtPath call waiting for each fetch, held until its
   * result is sent or the caller goes. */
  std::map<unsigned, LSMessage *> coverArtCallers_;
#endif

  /* com.webos.service.bus/signal/registerServerStatus for bluetooth2. */
  LSMessageToken btServerStatusToken_ = LSMESSAGE_TOKEN_INVALID;

  /* What is held open on bluetooth2, so that each is opened once and
   * cancelled when what it watches goes: device/getStatus repeats every
   * device on every change, so without this a new avrcp/getStatus
   * subscription was opened each time and every AVRCP change came back once
   * per update seen so far - hundreds of times within a minute of a headset
   * connecting.
   *   btAdapterToken_     adapter/getStatus
   *   btDeviceTokens_     device/getStatus, per adapter address
   *   avrcpStatusTokens_  avrcp/getStatus, per "<adapter>/<device>" with
   *                       AVRCP connected
   * The avrcp/receivePassThroughCommand subscription of each connected
   * device is in its BTDeviceInfo. */
  LSMessageToken btAdapterToken_ = LSMESSAGE_TOKEN_INVALID;
  std::map<std::string, LSMessageToken> btDeviceTokens_;
  std::map<std::string, LSMessageToken> avrcpStatusTokens_;
};

#endif /*MEDIA_CONTROL_SERVICE_H_*/
