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
#include <luna-service2/lunaservice.hpp>
#include <glib.h>

#include <map>
#include <string>

#include "MediaSessionManager.h"
#include "MediaControlPrivate.h"

class MediaControlService : public LS::Handle
{
private:
  using mainloop = std::unique_ptr<GMainLoop, void (*)(GMainLoop *)>;
  mainloop main_loop_ptr_ = {g_main_loop_new(nullptr, false), g_main_loop_unref};

public:
  MediaControlService();

  MediaControlService(MediaControlService const &) = delete;
  MediaControlService(MediaControlService &&) = delete;
  MediaControlService &operator=(MediaControlService const &) = delete;
  MediaControlService &operator=(MediaControlService &&) = delete;

  bool registerMediaSession(LSMessage &);
  bool unregisterMediaSession(LSMessage &);
  bool activateMediaSession(LSMessage &);
  bool deactivateMediaSession(LSMessage &);
  bool getMediaMetaData(LSMessage &);
  bool getMediaPlayStatus(LSMessage &);
  bool getMediaSessionInfo(LSMessage &);
  bool getMediaSessionId(LSMessage &);
  bool getActiveMediaSessions(LSMessage &);
  bool getMediaCoverArtPath(LSMessage &);
  bool setSupportedActions(LSMessage &);
  bool setMediaMetaData(LSMessage &);
  bool setMediaPlayStatus(LSMessage &);
  bool setMediaMuteStatus (LSMessage &);
  bool setMediaPlayPosition (LSMessage &);
  bool setMediaCoverArt (LSMessage &);
  bool receiveMediaPlaybackInfo (LSMessage &);
  bool injectMediaKeyEvent (LSMessage &);
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
#if USE_TEST_METHOD
  bool testKeyEvent(LSMessage &);
#endif

  LSHandle *lsHandle_;
  MediaSessionManager *ptrMediaSessionMgr_;
  MediaControlPrivate *ptrMediaControlPrivate_;

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
