// Copyright (c) 2020-2025 LG Electronics, Inc.
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

/*-----------------------------------------------------------------------------
 (File Inclusions)
 ------------------------------------------------------------------------------*/
#include "MediaControlService.h"
#include "Lsutils.h"

#include <string>

static constexpr const char *cstrMediaControlService = "com.webos.service.mediacontroller";
static constexpr const char *cstrBTAdapterGetStatus = "luna://com.webos.service.bluetooth2/adapter/getStatus";
static constexpr const char *cstrBTDeviceGetStatus = "luna://com.webos.service.bluetooth2/device/getStatus";
static constexpr const char *cstrBTAvrcpGetStatus = "luna://com.webos.service.bluetooth2/avrcp/getStatus";
static constexpr const char *cstrBTAvrcpReceivePassThroughCommand = "luna://com.webos.service.bluetooth2/avrcp/receivePassThroughCommand";
static constexpr const char *cstrSubscribe = "{\"subscribe\":true}";
static constexpr const char *cstrBTNotifyMediaPlayStatus = "luna://com.webos.service.bluetooth2/avrcp/notifyMediaPlayStatus";

static void respond(LSMessage &message, const std::string &payload) {
  CLSError lserror;
  if (!LSMessageRespond(&message, payload.c_str(), &lserror))
    PMLOG_ERROR(CONST_MODULE_MCS, "LSMessageRespond failed: %s", lserror.text());
}

/* A method of the service as luna-service2 calls it. */
template <bool (MediaControlService::*method)(LSMessage &)>
static bool dispatch(LSHandle *, LSMessage *message, void *ctx) {
  return (static_cast<MediaControlService *>(ctx)->*method)(*message);
}

#define MCS_METHOD(name) {#name, &dispatch<&MediaControlService::name>, LUNA_METHOD_FLAGS_NONE}

static LSMethod mediaControlMethods[] = {
  MCS_METHOD(registerMediaSession),
  MCS_METHOD(unregisterMediaSession),
  MCS_METHOD(activateMediaSession),
  MCS_METHOD(deactivateMediaSession),
  MCS_METHOD(getMediaMetaData),
  MCS_METHOD(getMediaPlayStatus),
  MCS_METHOD(getMediaSessionInfo),
  MCS_METHOD(getMediaSessionId),
  MCS_METHOD(getActiveMediaSessions),
#if MCS_COVER_ART
  MCS_METHOD(getMediaCoverArtPath),
#endif
  MCS_METHOD(setMediaMetaData),
  MCS_METHOD(setMediaPlayStatus),
  MCS_METHOD(setMediaMuteStatus),
  MCS_METHOD(setMediaPlayPosition),
  MCS_METHOD(receiveMediaPlaybackInfo),
  MCS_METHOD(injectMediaKeyEvent),
  MCS_METHOD(setMediaCoverArt),
  MCS_METHOD(setSupportedActions),
#if USE_TEST_METHOD
  MCS_METHOD(testKeyEvent),
#endif
  {nullptr, nullptr, LUNA_METHOD_FLAGS_NONE}
};

#undef MCS_METHOD

MediaControlService::MediaControlService() {
}

MediaControlService::~MediaControlService() {
  stop();
}

bool MediaControlService::start(GMainLoop *loop) {
  PMLOG_INFO(CONST_MODULE_MCS, "%s IN", __FUNCTION__);
  if (lsHandle_)
    return true;

  CLSError lserror;
  if (!LSRegister(cstrMediaControlService, &lsHandle_, &lserror)) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s LSRegister failed: %s", __FUNCTION__, lserror.text());
    lsHandle_ = nullptr;
    return false;
  }
  if (!LSRegisterCategory(lsHandle_, "/", mediaControlMethods, nullptr, nullptr, &lserror) ||
      !LSCategorySetData(lsHandle_, "/", this, &lserror) ||
      !LSSubscriptionSetCancelFunction(lsHandle_, &MediaControlService::onSubscriptionCancel,
                                       this, &lserror) ||
      !LSGmainAttach(lsHandle_, loop, &lserror)) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s registering the methods failed: %s", __FUNCTION__, lserror.text());
    stop();
    return false;
  }

  // follow bluetooth2 coming and going
  pbnjson::JValue payload = pbnjson::Object();
  payload.put("serviceName", "com.webos.service.bluetooth2");
  if (!LSCall(lsHandle_, "luna://com.webos.service.bus/signal/registerServerStatus",
              payload.stringify().c_str(), &MediaControlService::onBTServerStatusCb,
              this, &btServerStatusToken_, &lserror)) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s registerServerStatus failed: %s", __FUNCTION__, lserror.text());
    stop();
    return false;
  }

#if MCS_COVER_ART
  coverArt_ = std::make_unique<CoverArtFetcher>(MCS_COVER_ART_DIR,
      [this](const CoverArtFetcher::Result &result) { onCoverArtFetched(result); });
#endif
  return true;
}

void MediaControlService::stop() {
  if (!lsHandle_)
    return;

#if MCS_COVER_ART
  /* No result may come back for a caller that is no longer held. */
  coverArt_.reset();
  for (auto& caller : coverArtCallers_)
    LSMessageUnref(caller.second);
  coverArtCallers_.clear();
#endif

  /* The sessions' calls are held until the handle goes; its cancel
   * function is not called for them then. */
  for (auto& client : sessionClients_)
    LSMessageUnref(client.second);
  sessionClients_.clear();
  // and the sessions with them: none of their clients is held any more
  mediaSessionMgr_.clear();

  /* LSUnregister() ends every call and subscription on the handle; what is
   * recorded of them goes with it. */
  CLSError lserror;
  if (!LSUnregister(lsHandle_, &lserror))
    PMLOG_ERROR(CONST_MODULE_MCS, "%s LSUnregister failed: %s", __FUNCTION__, lserror.text());
  lsHandle_ = nullptr;
  btServerStatusToken_ = LSMESSAGE_TOKEN_INVALID;
  btAdapterToken_ = LSMESSAGE_TOKEN_INVALID;
  btDeviceTokens_.clear();
  avrcpStatusTokens_.clear();
  mediaControlPrivate_.takeBTDevices();
}

/* A reply from bluetooth2, as JSON; null if it is not JSON at all. Only the
 * fields used are read, each checked for its type, so a field bluetooth2
 * adds later does not make a whole reply unreadable. */
static pbnjson::JValue btReply(LSMessage *message) {
  const char *payload = LSMessageGetPayload(message);
  if (!payload)
    return pbnjson::JValue();
  pbnjson::JValue reply = pbnjson::JDomParser::fromString(payload);
  if (!reply.isObject()) {
    PMLOG_ERROR(CONST_MODULE_MCS, "bluetooth2 reply is not a JSON object");
    return pbnjson::JValue();
  }
  return reply;
}

static bool btBool(const pbnjson::JValue& reply, const char *name) {
  return reply[name].isBoolean() && reply[name].asBool();
}

static std::string btString(const pbnjson::JValue& reply, const char *name) {
  return reply[name].isString() ? reply[name].asString() : std::string();
}

/* The key of the entry in tokens that a reply belongs to, empty if none. */
static std::string keyOfCall(const std::map<std::string, LSMessageToken>& tokens,
                             LSMessage *reply) {
  LSMessageToken token = LSMessageGetResponseToken(reply);
  for (const auto& held : tokens) {
    if (held.second == token)
      return held.first;
  }
  return std::string();
}

void MediaControlService::cancelCall(LSMessageToken& token) {
  if (token == LSMESSAGE_TOKEN_INVALID)
    return;
  CLSError lserror;
  if (!LSCallCancel(lsHandle_, token, &lserror))
    PMLOG_INFO(CONST_MODULE_MCS, "%s call %lu was already over", __FUNCTION__,
               static_cast<unsigned long>(token));
  token = LSMESSAGE_TOKEN_INVALID;
}

bool MediaControlService::onBTServerStatusCb(LSHandle *, LSMessage *message, void *ctx) {
  MediaControlService *obj = static_cast<MediaControlService *>(ctx);
  pbnjson::JValue payload = btReply(message);
  if (!obj || payload.isNull())
    return true;

  if (btBool(payload, "connected")) {
    PMLOG_INFO(CONST_MODULE_MCS, "%s bluetooth2 is up", __FUNCTION__);
    obj->subscribeToBTAdapterGetStatus();
  } else {
    /* Every subscription on it ended with it; a new instance knows nothing
     * of them. Start again from adapter/getStatus when it is back. */
    PMLOG_INFO(CONST_MODULE_MCS, "%s bluetooth2 is down", __FUNCTION__);
    obj->dropBluetooth();
  }
  return true;
}

void MediaControlService::subscribeToBTAdapterGetStatus() {
  if (btAdapterToken_ != LSMESSAGE_TOKEN_INVALID)
    return;

  CLSError lserror;
  if (!LSCall(lsHandle_,
              cstrBTAdapterGetStatus,
              cstrSubscribe,
              &MediaControlService::onBTAdapterQueryCb,
              this, &btAdapterToken_, &lserror)) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s LSCall failed for adapter/getStatus", __FUNCTION__);
    btAdapterToken_ = LSMESSAGE_TOKEN_INVALID;
  }
}

void MediaControlService::dropBluetooth() {
  cancelCall(btAdapterToken_);
  for (auto& held : btDeviceTokens_)
    cancelCall(held.second);
  btDeviceTokens_.clear();
  for (auto& held : avrcpStatusTokens_)
    cancelCall(held.second);
  avrcpStatusTokens_.clear();
  for (auto& device : mediaControlPrivate_.takeBTDevices())
    cancelCall(device.passThroughToken_);
}

void MediaControlService::dropBTDevice(const std::string& address,
                                       const std::string& adapterAddress) {
  BTDeviceInfo device;
  if (mediaControlPrivate_.removeBTDevice(address, adapterAddress, &device))
    cancelCall(device.passThroughToken_);
}

bool MediaControlService::onBTAdapterQueryCb(LSHandle *, LSMessage *message, void *ctx) {
  MediaControlService *obj = static_cast<MediaControlService *>(ctx);
  pbnjson::JValue payload = btReply(message);
  if (!obj || payload.isNull())
    return true;

  if (!btBool(payload, "returnValue")) {
    /* The subscription is over: bluetooth2 went, or refused it. */
    PMLOG_ERROR(CONST_MODULE_MCS, "%s adapter/getStatus failed: %s", __FUNCTION__,
                btString(payload, "errorText").c_str());
    if (LSMessageGetResponseToken(message) == obj->btAdapterToken_)
      obj->btAdapterToken_ = LSMESSAGE_TOKEN_INVALID;
    return true;
  }

  /* One device/getStatus subscription per adapter there is now. */
  std::map<std::string, LSMessageToken> adapters;
  pbnjson::JValue list = payload["adapters"];
  for (int i = 0; list.isArray() && i < list.arraySize(); i++) {
    std::string adapterAddress = btString(list[i], "adapterAddress");
    if (adapterAddress.empty() || adapters.count(adapterAddress))
      continue;

    auto held = obj->btDeviceTokens_.find(adapterAddress);
    if (held != obj->btDeviceTokens_.end()) {
      adapters.insert(*held);
      obj->btDeviceTokens_.erase(held);
      continue;
    }

    pbnjson::JValue request = pbnjson::Object();
    request.put("adapterAddress", adapterAddress);
    request.put("subscribe", true);
    PMLOG_INFO(CONST_MODULE_MCS, "%s device/getStatus for adapter %s", __FUNCTION__,
               adapterAddress.c_str());
    CLSError lserror;
    LSMessageToken token = LSMESSAGE_TOKEN_INVALID;
    if (LSCall(obj->lsHandle_, cstrBTDeviceGetStatus, request.stringify().c_str(),
               &MediaControlService::onBTDeviceGetStatusCb, obj, &token, &lserror))
      adapters[adapterAddress] = token;
    else
      PMLOG_ERROR(CONST_MODULE_MCS, "%s LSCall failed for device/getStatus", __FUNCTION__);
  }

  /* What is left is adapters that went, and the devices on them. */
  for (auto& gone : obj->btDeviceTokens_) {
    obj->cancelCall(gone.second);
    std::string prefix = gone.first + "/";
    for (auto itr = obj->avrcpStatusTokens_.begin(); itr != obj->avrcpStatusTokens_.end();) {
      if (itr->first.compare(0, prefix.size(), prefix) == 0) {
        obj->cancelCall(itr->second);
        obj->dropBTDevice(itr->first.substr(prefix.size()), gone.first);
        itr = obj->avrcpStatusTokens_.erase(itr);
      } else {
        ++itr;
      }
    }
  }
  obj->btDeviceTokens_.swap(adapters);
  return true;
}

bool MediaControlService::onBTDeviceGetStatusCb(LSHandle *, LSMessage *message, void *ctx) {
  MediaControlService *obj = static_cast<MediaControlService *>(ctx);
  pbnjson::JValue payload = btReply(message);
  if (!obj || payload.isNull())
    return true;

  std::string adapterAddress = btString(payload, "adapterAddress");
  if (!btBool(payload, "returnValue")) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s device/getStatus failed: %s", __FUNCTION__,
                btString(payload, "errorText").c_str());
    std::string key = keyOfCall(obj->btDeviceTokens_, message);
    if (!key.empty())
      obj->btDeviceTokens_.erase(key);
    return true;
  }
  if (adapterAddress.empty())
    adapterAddress = keyOfCall(obj->btDeviceTokens_, message);
  if (adapterAddress.empty()) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s reply for no adapter we watch", __FUNCTION__);
    return true;
  }

  /* Devices on this adapter that have AVRCP connected now. One
   * avrcp/getStatus subscription each, opened when AVRCP appears and
   * cancelled when it goes: this callback sees every device again on every
   * change. An empty list is all of them gone. */
  std::string prefix = adapterAddress + "/";
  std::map<std::string, LSMessageToken> connected;
  pbnjson::JValue devices = payload["devices"];
  for (int i = 0; devices.isArray() && i < devices.arraySize(); i++) {
    pbnjson::JValue profiles = devices[i]["connectedProfiles"];
    bool avrcp = false;
    for (int j = 0; profiles.isArray() && j < profiles.arraySize(); j++)
      avrcp = avrcp || (profiles[j].isString() && profiles[j].asString() == "avrcp");
    std::string address = btString(devices[i], "address");
    if (!avrcp || address.empty())
      continue;

    std::string key = prefix + address;
    if (connected.count(key))
      continue;
    auto held = obj->avrcpStatusTokens_.find(key);
    if (held != obj->avrcpStatusTokens_.end()) {
      connected.insert(*held);
      obj->avrcpStatusTokens_.erase(held);
      continue;
    }

    pbnjson::JValue request = pbnjson::Object();
    request.put("adapterAddress", adapterAddress);
    request.put("address", address);
    request.put("subscribe", true);
    PMLOG_INFO(CONST_MODULE_MCS, "%s avrcp/getStatus for %s", __FUNCTION__, key.c_str());
    CLSError lserror;
    LSMessageToken token = LSMESSAGE_TOKEN_INVALID;
    if (LSCall(obj->lsHandle_, cstrBTAvrcpGetStatus, request.stringify().c_str(),
               &MediaControlService::onBTAvrcpGetStatusCb, obj, &token, &lserror))
      connected[key] = token;
    else
      PMLOG_ERROR(CONST_MODULE_MCS, "%s LSCall failed for avrcp/getStatus", __FUNCTION__);
  }

  /* AVRCP has gone from these devices of this adapter: drop their
   * subscriptions, or a reconnect would find the key held and the next one
   * would leak, and forget the device. Other adapters' are left alone. */
  for (auto itr = obj->avrcpStatusTokens_.begin(); itr != obj->avrcpStatusTokens_.end();) {
    if (itr->first.compare(0, prefix.size(), prefix) != 0) {
      ++itr;
      continue;
    }
    obj->cancelCall(itr->second);
    obj->dropBTDevice(itr->first.substr(prefix.size()), adapterAddress);
    itr = obj->avrcpStatusTokens_.erase(itr);
  }
  obj->avrcpStatusTokens_.insert(connected.begin(), connected.end());
  return true;
}

bool MediaControlService::onBTAvrcpGetStatusCb(LSHandle *, LSMessage *message, void *ctx) {
  MediaControlService *obj = static_cast<MediaControlService *>(ctx);
  pbnjson::JValue payload = btReply(message);
  if (!obj || payload.isNull())
    return true;

  std::string key = keyOfCall(obj->avrcpStatusTokens_, message);
  if (!btBool(payload, "returnValue")) {
    /* The subscription is over; so is what it told us about the device. */
    PMLOG_ERROR(CONST_MODULE_MCS, "%s avrcp/getStatus failed: %s", __FUNCTION__,
                btString(payload, "errorText").c_str());
    if (!key.empty()) {
      std::string::size_type slash = key.find('/');
      obj->dropBTDevice(key.substr(slash + 1), key.substr(0, slash));
      obj->avrcpStatusTokens_.erase(key);
    }
    return true;
  }

  std::string address = btString(payload, "address");
  std::string adapterAddress = btString(payload, "adapterAddress");
  if (address.empty() || adapterAddress.empty()) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s reply without the device or adapter address", __FUNCTION__);
    return true;
  }
  bool connected = btBool(payload, "connected");
  PMLOG_INFO(CONST_MODULE_MCS, "%s device %s adapter %s AVRCP connected %d", __FUNCTION__,
             address.c_str(), adapterAddress.c_str(), connected);

  if (!connected) {
    obj->dropBTDevice(address, adapterAddress);
    return true;
  }
  if (obj->mediaControlPrivate_.isDeviceRegistered(address, adapterAddress))
    return true;

  /* There is one display without FEATURE_DUAL_DISPLAY, and nothing tells
   * which display an adapter belongs to with it: bluetooth goes to 0. */
  BTDeviceInfo device(address, adapterAddress, 0);

  pbnjson::JValue request = pbnjson::Object();
  request.put("address", address);
  request.put("adapterAddress", adapterAddress);
  request.put("subscribe", true);
  CLSError lserror;
  if (!LSCall(obj->lsHandle_, cstrBTAvrcpReceivePassThroughCommand,
              request.stringify().c_str(), &MediaControlService::onBTAvrcpKeyEventsCb,
              obj, &device.passThroughToken_, &lserror)) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s LSCall failed for avrcp/receivePassThroughCommand", __FUNCTION__);
    device.passThroughToken_ = LSMESSAGE_TOKEN_INVALID;
  }
  obj->mediaControlPrivate_.setBTDeviceInfo(device);
  return true;
}

bool MediaControlService::onBTAvrcpKeyEventsCb(LSHandle *, LSMessage *message, void *ctx) {
  MediaControlService *obj = static_cast<MediaControlService *>(ctx);
  pbnjson::JValue payload = btReply(message);
  if (!obj || payload.isNull())
    return true;

  if (!btBool(payload, "returnValue")) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s avrcp/receivePassThroughCommand failed: %s", __FUNCTION__,
                btString(payload, "errorText").c_str());
    return true;
  }

  std::string address = btString(payload, "address");
  std::string adapterAddress = btString(payload, "adapterAddress");
  std::string keyCode = btString(payload, "keyCode");
  std::string keyStatus = btString(payload, "keyStatus");
  PMLOG_INFO(CONST_MODULE_MCS, "%s address = %s, adapterAddress = %s, keyCode = %s, keyStatus = %s",
             __FUNCTION__, address.c_str(), adapterAddress.c_str(), keyCode.c_str(), keyStatus.c_str());
  if (keyStatus != "released" || keyCode.empty())
    return true;
  if (!obj->mediaControlPrivate_.isDeviceRegistered(address, adapterAddress))
    return true;

  //post key event to the session last activated, on bluetooth's display (0)
  std::string mediaId = obj->mediaSessionMgr_.getCurrentActiveSession();
  int displayIdForMedia = obj->mediaSessionMgr_.getDisplayIdForMedia(mediaId);
  PMLOG_INFO(CONST_MODULE_MCS, "%s mediaId for sending BT key event : %s", __FUNCTION__, mediaId.c_str());
  if (mediaId.empty() || displayIdForMedia != 0)
    return true;

  obj->sendKeyEvent(mediaId, keyCode);
  return true;
}

void MediaControlService::notifyBTPlayStatus(const std::string& mediaId,
                                             const std::string& playStatus) {
  /*The below code will be removed once BT service will subscribe to MCS API
    for recieving the mediaPlayback status information. This is a temporary fix, until BT
    service implements the subscription call */

  /* Only the session the headset's keys go to speaks for the phone: a
   * second player in the background must not tell it "paused" while the
   * first plays. */
  if (mediaId != mediaSessionMgr_.getCurrentActiveSession())
    return;

  /* AVRCP has stopped, playing, paused, fwd_seek, rev_seek and error.
   * A session that has not started is stopped; buffering says nothing new. */
  static const std::map<std::string, std::string> playStatusMap = {
    {"PLAYSTATE_NONE", "stopped"},
    {"PLAYSTATE_STOPPED", "stopped"},
    {"PLAYSTATE_PAUSED", "paused"},
    {"PLAYSTATE_PLAYING", "playing"},
    {"PLAYSTATE_FAST_FORWARDING", "fwd_seek"},
    {"PLAYSTATE_REWINDING", "rev_seek"},
    {"PLAYSTATE_ERROR", "error"}
  };
  auto status = playStatusMap.find(playStatus);
  if (status == playStatusMap.end())
    return;

  int displayIdForMedia = mediaSessionMgr_.getDisplayIdForMedia(mediaId);
  for (const auto& device : mediaControlPrivate_.getBTDevices(displayIdForMedia)) {
    PMLOG_INFO(CONST_MODULE_MCS, "%s adapterAddress = %s, address = %s status = %s", __FUNCTION__,
               device.adapterAddress_.c_str(), device.deviceAddress_.c_str(), status->second.c_str());

    pbnjson::JObject playStatusObj;
    playStatusObj.put("duration", 0);
    playStatusObj.put("position", 0);
    playStatusObj.put("status", status->second);

    pbnjson::JObject request;
    request.put("adapterAddress", device.adapterAddress_);
    request.put("address", device.deviceAddress_);
    request.put("playbackStatus", playStatusObj);

    CLSError lserror;
    if (!LSCallOneReply(lsHandle_, cstrBTNotifyMediaPlayStatus, request.stringify().c_str(),
                        NULL, NULL, NULL, &lserror))
      PMLOG_ERROR(CONST_MODULE_MCS, "%s LSCall failed to avrcp/notifyMediaPlayStatus", __FUNCTION__);
  }
}

/* The reply to a call that failed with errorCode. */
static bool replyError(LSMessage &message, int errorCode) {
  PMLOG_ERROR(CONST_MODULE_MCS, "%s: %s", LSMessageGetMethod(&message),
              getErrorTextFromErrorCode(errorCode).c_str());
  respond(message, createJsonReplyString(false, errorCode, getErrorTextFromErrorCode(errorCode)));
  return true;
}

/* The reply to a call that did what it was asked. */
static bool replySuccess(LSMessage &message) {
  respond(message, createJsonReplyString(true));
  return true;
}

static bool replyResult(LSMessage &message, int errorCode) {
  if (errorCode != MCS_ERROR_NO_ERROR)
    return replyError(message, errorCode);
  return replySuccess(message);
}

/* What receiveMediaPlaybackInfo can be asked for; "" is all of them. */
static const char *const playbackEvents[] = {
  "playStatus", "muteStatus", "playPosition", "mediaMetaData", "coverArt", "supportedActions"
};

static bool isPlaybackEvent(const std::string &eventType) {
  for (const char *event : playbackEvents) {
    if (eventType == event)
      return true;
  }
  return false;
}

/* The subscription key of the receiveMediaPlaybackInfo callers that asked
 * for eventType ("" for all events) on displayId. Each caller is told what
 * it asked for, and only that. */
static std::string playbackInfoKey(int displayId, const std::string &eventType) {
  return "receiveMediaPlaybackInfo/" + std::to_string(displayId) + "/" +
         (eventType.empty() ? std::string("all") : eventType);
}

/* The canonical mute status: Chromium sends MUTESTATE_MUTED and
 * MUTESTATE_UNMUTED, others MUTE and UNMUTE, and a session starts as
 * "unmute"; empty for anything else. */
static std::string muteStatusOf(const std::string &muteStatus) {
  if (muteStatus == "mute" || muteStatus == "MUTE" || muteStatus == "MUTESTATE_MUTED")
    return "mute";
  if (muteStatus == "unmute" || muteStatus == "UNMUTE" || muteStatus == "MUTESTATE_UNMUTED")
    return "unmute";
  return std::string();
}

/* The display a call is for: the one it names with FEATURE_DUAL_DISPLAY,
 * else the only one there is. */
static int displayOf(pbnjson::JValue &payload) {
#if defined(FEATURE_DUAL_DISPLAY)
  return payload["displayId"].asNumber<int>();
#else
  (void)payload;
  return 0;
#endif
}

/* A mediaId the caller gave, from a payload validated as having one. */
static std::string mediaIdOf(LSMessageJsonParser &msg) {
  std::string mediaId;
  msg.get("mediaId", mediaId);
  return mediaId;
}

void MediaControlService::broadcastPlaybackInfo(const std::string &mediaId,
                                                const std::string &eventType,
                                                pbnjson::JValue payload) {
  mediaSession objMediaSession;
  if (mediaSessionMgr_.getMediaSessionInfo(mediaId, objMediaSession) != MCS_ERROR_NO_ERROR)
    return;

  int displayId = mediaSessionMgr_.getDisplayIdForMedia(mediaId);
  payload.put("displayId", displayId);
  payload.put("mediaId", mediaId);
  payload.put("appId", objMediaSession.getAppId());
  payload.put("eventType", eventType);
  payload.put("returnValue", true);
  payload.put("subscribed", true);
  std::string reply = payload.stringify();
  PMLOG_INFO(CONST_MODULE_MCS, "%s send subscription response :%s", __FUNCTION__, reply.c_str());

  for (const std::string &key : {playbackInfoKey(displayId, eventType), playbackInfoKey(displayId, "")}) {
    CLSError lserror;
    if (!LSSubscriptionReply(lsHandle_, key.c_str(), reply.c_str(), &lserror))
      PMLOG_ERROR(CONST_MODULE_MCS, "%s LSSubscriptionReply failed for %s: %s", __FUNCTION__,
                  key.c_str(), lserror.text());
  }
}

static pbnjson::JValue coverArtJson(const std::vector<mediaCoverArt> &coverArt) {
  pbnjson::JValue coverArtArray = pbnjson::Array();
  for (const auto &element : coverArt) {
    pbnjson::JValue coverArtItem = pbnjson::Object();
    coverArtItem.put("src", element.getSource());
    coverArtItem.put("type", element.getType());
    pbnjson::JValue coverArtSizes = pbnjson::Array();
    for (const auto &size : element.getSize()) {
      pbnjson::JValue sizesObj = pbnjson::Object();
      sizesObj.put("width", size.width);
      sizesObj.put("height", size.height);
      coverArtSizes.append(sizesObj);
    }
    coverArtItem.put("sizes", coverArtSizes);
    coverArtArray.append(coverArtItem);
  }
  return coverArtArray;
}

static pbnjson::JValue actionsJson(const std::vector<std::string> &actions) {
  pbnjson::JValue actionListArray = pbnjson::Array();
  for (const auto &action : actions)
    actionListArray.append(pbnjson::JValue(action));
  return actionListArray;
}

bool MediaControlService::registerMediaSession(LSMessage& message) {
  PMLOG_INFO(CONST_MODULE_MCS, "%s IN", __FUNCTION__);

  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_3(REQUIRED(mediaId, string),REQUIRED( \
  appId, string),REQUIRED(subscribe, boolean)) REQUIRED_3(mediaId, appId, subscribe)));

  pbnjson::JObject responseObj;
  int errorCode = MCS_ERROR_NO_ERROR;
  bool subscribed = false;
  std::string mediaId;
  std::string appId;

  if (!msg.parse(__FUNCTION__)) {
    errorCode = MCS_ERROR_PARSING_FAILED;
  } else {
    msg.get("mediaId", mediaId);
    msg.get("appId", appId);
    PMLOG_INFO(CONST_MODULE_MCS, "%s mediaId : %s appId : %s", __FUNCTION__,
               mediaId.c_str(), appId.c_str());

    if (mediaId.empty())
      errorCode = MCS_ERROR_INVALID_MEDIAID;
    else if (appId.empty())
      errorCode = MCS_ERROR_INVALID_APPID;
    else
      errorCode = mediaSessionMgr_.addMediaSession(mediaId, appId);
  }

  /* The session is the caller's for as long as its call is open: the
   * subscription is how key events reach it, and its cancel - the caller
   * unsubscribing, or going away - ends the session. Added only once the
   * session is, so that a second registration of a mediaId that is taken
   * is not tied to the first one's session. */
  if (errorCode == MCS_ERROR_NO_ERROR && LSMessageIsSubscription(&message)) {
    CLSError lserror;
    if (LSSubscriptionAdd(lsHandle_, "registerMediaSession", &message, &lserror)) {
      LSMessageRef(&message);
      sessionClients_[mediaId] = &message;
      subscribed = true;
    } else {
      PMLOG_ERROR(CONST_MODULE_MCS, "%s LSSubscriptionAdd failed: %s", __FUNCTION__, lserror.text());
      mediaSessionMgr_.removeMediaSession(mediaId);
      errorCode = MCS_ERROR_REGISTERSESSION_FAILED;
    }
  }

  responseObj.put("subscribed", subscribed);
  responseObj.put("returnValue", errorCode == MCS_ERROR_NO_ERROR);
  if (errorCode != MCS_ERROR_NO_ERROR) {
    responseObj.put("errorCode", errorCode);
    responseObj.put("errorText", getErrorTextFromErrorCode(errorCode));
  }

  PMLOG_INFO(CONST_MODULE_MCS, "%s response : %s", __FUNCTION__, responseObj.stringify().c_str());
  respond(message, responseObj.stringify());
  return true;
}

void MediaControlService::releaseSessionClient(const std::string& mediaId) {
  auto client = sessionClients_.find(mediaId);
  if (client == sessionClients_.end())
    return;
  LSMessageUnref(client->second);
  sessionClients_.erase(client);
}

bool MediaControlService::onSubscriptionCancel(LSHandle *, LSMessage *message, void *ctx) {
  MediaControlService *obj = static_cast<MediaControlService *>(ctx);
  if (!obj)
    return true;
#if MCS_COVER_ART
  for (auto caller = obj->coverArtCallers_.begin(); caller != obj->coverArtCallers_.end();) {
    if (caller->second == message) {
      LSMessageUnref(caller->second);
      caller = obj->coverArtCallers_.erase(caller);
    } else {
      ++caller;
    }
  }
#endif
  for (const auto& client : obj->sessionClients_) {
    if (client.second != message)
      continue;
    std::string mediaId = client.first;
    PMLOG_INFO(CONST_MODULE_MCS, "%s session %s: its client has gone", __FUNCTION__, mediaId.c_str());
    obj->mediaSessionMgr_.removeMediaSession(mediaId);
    obj->releaseSessionClient(mediaId);
    break;
  }
  return true;
}

bool MediaControlService::sendKeyEvent(const std::string& mediaId, const std::string& keyEvent) {
  auto client = sessionClients_.find(mediaId);
  if (client == sessionClients_.end()) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s session %s has no subscription to send %s on", __FUNCTION__,
                mediaId.c_str(), keyEvent.c_str());
    return false;
  }

  /* To that session's own call: every session is told only its own key
   * events, not those of every other session as well. */
  pbnjson::JValue responseObj = pbnjson::Object();
  responseObj.put("keyEvent", keyEvent);
  responseObj.put("mediaId", mediaId);
  responseObj.put("returnValue", true);
  responseObj.put("subscribed", true);
  PMLOG_INFO(CONST_MODULE_MCS, "%s send subscription response :%s", __FUNCTION__, responseObj.stringify().c_str());
  CLSError lserror;
  if (!LSMessageRespond(client->second, responseObj.stringify().c_str(), &lserror)) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s LSMessageRespond failed: %s", __FUNCTION__, lserror.text());
    return false;
  }
  return true;
}

bool MediaControlService::unregisterMediaSession(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_1(REQUIRED(mediaId, string)) REQUIRED_1(mediaId)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::string mediaId = mediaIdOf(msg);
  PMLOG_INFO(CONST_MODULE_MCS, "%s mediaId : %s", __FUNCTION__, mediaId.c_str());
  int errorCode = mediaSessionMgr_.removeMediaSession(mediaId);
  if (errorCode == MCS_ERROR_NO_ERROR)
    releaseSessionClient(mediaId);
  return replyResult(message, errorCode);
}

bool MediaControlService::activateMediaSession(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_1(REQUIRED(mediaId, string)) REQUIRED_1(mediaId)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::string mediaId = mediaIdOf(msg);
  PMLOG_INFO(CONST_MODULE_MCS, "%s mediaId : %s", __FUNCTION__, mediaId.c_str());
  return replyResult(message, mediaSessionMgr_.activateMediaSession(mediaId));
}

bool MediaControlService::deactivateMediaSession(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_1(REQUIRED(mediaId, string)) REQUIRED_1(mediaId)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::string mediaId = mediaIdOf(msg);
  PMLOG_INFO(CONST_MODULE_MCS, "%s mediaId : %s", __FUNCTION__, mediaId.c_str());
  return replyResult(message, mediaSessionMgr_.deactivateMediaSession(mediaId));
}

static pbnjson::JObject metaDataJson(const mediaMetaData &objMetaData) {
  pbnjson::JObject metaDataObj;
  metaDataObj.put("title", objMetaData.getTitle());
  metaDataObj.put("artist", objMetaData.getArtist());
  metaDataObj.put("totalDuration", objMetaData.getDuration());
  metaDataObj.put("album", objMetaData.getAlbum());
  metaDataObj.put("genre", objMetaData.getGenre());
  metaDataObj.put("trackNumber", objMetaData.getTrackNumber());
  metaDataObj.put("volume", objMetaData.getVolume());
  return metaDataObj;
}

bool MediaControlService::getMediaMetaData(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_1(REQUIRED(mediaId, string)) REQUIRED_1(mediaId)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::string mediaId = mediaIdOf(msg);
  mediaMetaData objMetaData;
  int errorCode = mediaSessionMgr_.getMediaMetaData(mediaId, objMetaData);
  if (errorCode != MCS_ERROR_NO_ERROR)
    return replyError(message, errorCode);

  pbnjson::JObject responseObj;
  responseObj.put("returnValue", true);
  responseObj.put("metaData", metaDataJson(objMetaData));
  respond(message, responseObj.stringify());
  return true;
}

bool MediaControlService::setSupportedActions(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_2(REQUIRED(mediaId, string), \
  REQUIRED(supportedActions, array)) REQUIRED_2(mediaId, supportedActions)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  pbnjson::JValue payload = msg.get();
  std::string mediaId = mediaIdOf(msg);
  pbnjson::JValue actions = payload["supportedActions"];
  if (actions.arraySize() <= 0)
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::vector<std::string> enableActions;
  for (int i = 0; i < actions.arraySize(); i++) {
    if (!actions[i].isString() || actions[i].asString().empty())
      return replyError(message, MCS_ERROR_PARSING_FAILED);
    enableActions.push_back(actions[i].asString());
  }

  int errorCode = mediaSessionMgr_.setMediaAction(mediaId, enableActions);
  if (errorCode != MCS_ERROR_NO_ERROR)
    return replyError(message, errorCode);

  pbnjson::JValue event = pbnjson::Object();
  event.put("supportedActions", actionsJson(enableActions));
  broadcastPlaybackInfo(mediaId, "supportedActions", event);
  return replySuccess(message);
}

bool MediaControlService::getMediaPlayStatus(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_1(REQUIRED(mediaId, string)) REQUIRED_1(mediaId)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::string playStatus;
  int errorCode = mediaSessionMgr_.getMediaPlayStatus(mediaIdOf(msg), playStatus);
  if (errorCode != MCS_ERROR_NO_ERROR)
    return replyError(message, errorCode);

  pbnjson::JObject responseObj;
  responseObj.put("returnValue", true);
  responseObj.put("playStatus", playStatus);
  respond(message, responseObj.stringify());
  return true;
}

bool MediaControlService::getMediaSessionInfo(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_1(REQUIRED(mediaId, string)) REQUIRED_1(mediaId)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  mediaSession objMediaSession;
  int errorCode = mediaSessionMgr_.getMediaSessionInfo(mediaIdOf(msg), objMediaSession);
  if (errorCode != MCS_ERROR_NO_ERROR)
    return replyError(message, errorCode);

  pbnjson::JObject sessionInfoObj;
  sessionInfoObj.put("mediaId", objMediaSession.getMediaId());
  sessionInfoObj.put("appId", objMediaSession.getAppId());
  sessionInfoObj.put("metaData", metaDataJson(objMediaSession.getMediaMetaDataObj()));

  pbnjson::JObject responseObj;
  responseObj.put("returnValue", true);
  responseObj.put("sessionInfo", sessionInfoObj);
  respond(message, responseObj.stringify());
  return true;
}

bool MediaControlService::getMediaSessionId(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_1(REQUIRED(appId, string)) REQUIRED_1(appId)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::string appId;
  msg.get("appId", appId);
  std::vector<std::string> mediaIds = mediaSessionMgr_.getMediaSessionList(appId);
  if (mediaIds.empty())
    return replyError(message, MCS_ERROR_INVALID_APPID);

  pbnjson::JArray mediaSessionIdArray;
  for (const auto& mediaId : mediaIds)
    mediaSessionIdArray.append(mediaId);

  pbnjson::JObject responseObj;
  responseObj.put("returnValue", true);
  responseObj.put("mediaId", mediaSessionIdArray);
  respond(message, responseObj.stringify());
  return true;
}

bool MediaControlService::getActiveMediaSessions(LSMessage& message) {
  LSMessageJsonParser msg(&message,SCHEMA_0);
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::vector<std::string> active = mediaSessionMgr_.getActiveMediaSessionList();
  if (active.empty())
    return replyError(message, MCS_ERROR_NO_ACTIVE_SESSION);

  pbnjson::JArray activeMediaSessionArray;
  for (const auto& mediaId : active)
    activeMediaSessionArray.append(mediaId);

  pbnjson::JObject responseObj;
  responseObj.put("returnValue", true);
  responseObj.put("sessionList", activeMediaSessionArray);
  respond(message, responseObj.stringify());
  return true;
}

#if MCS_COVER_ART
bool MediaControlService::getMediaCoverArtPath(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_3(REQUIRED(displayId, integer), \
    OPTIONAL(subscribe, boolean),REQUIRED(src, array)) REQUIRED_2(displayId, src)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  pbnjson::JValue payload = msg.get();
  int displayId = displayOf(payload);
  pbnjson::JValue srcs = payload["src"];
  if (srcs.arraySize() <= 0 || static_cast<size_t>(srcs.arraySize()) > MAX_COVER_ART_SOURCES)
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::vector<std::string> sources;
  for (int i = 0; i < srcs.arraySize(); i++) {
    if (!srcs[i].isString() || !CoverArtFetcher::validUrl(srcs[i].asString())) {
      PMLOG_ERROR(CONST_MODULE_MCS, "%s source %d is not an http or https URL", __FUNCTION__, i);
      return replyError(message, MCS_ERROR_PARSING_FAILED);
    }
    sources.push_back(srcs[i].asString());
  }

  if (mediaSessionMgr_.getMediaIdFromDisplayId(displayId).empty())
    return replyError(message, MCS_ERROR_NO_ACTIVE_SESSION);

  /* Each result goes to this caller alone, if it subscribed: it used to go
   * to every caller subscribed to any cover art. */
  bool subscribed = false;
  if (LSMessageIsSubscription(&message)) {
    CLSError lserror;
    if (!LSSubscriptionAdd(lsHandle_, "getMediaCoverArtPath", &message, &lserror)) {
      PMLOG_ERROR(CONST_MODULE_MCS, "%s LSSubscriptionAdd failed: %s", __FUNCTION__, lserror.text());
      return replyError(message, MCS_ERROR_SUBSCRIPTION_FAILED);
    }
    subscribed = true;
  }

  pbnjson::JValue coverArtArray = pbnjson::Array();
  for (const std::string &src : sources) {
    pbnjson::JValue coverArtSrcInfo = pbnjson::Object();
    coverArtSrcInfo.put("src", src);
    coverArtSrcInfo.put("srcPath", coverArt_->pathFor(src));
    coverArtArray.append(coverArtSrcInfo);

    unsigned id = coverArt_->fetch(src);
    if (id && subscribed) {
      LSMessageRef(&message);
      coverArtCallers_.emplace(id, &message);
    }
  }

  pbnjson::JObject responseObj;
  responseObj.put("coverArtPathInfo", coverArtArray);
  responseObj.put("subscribed", subscribed);
  responseObj.put("returnValue", true);
  respond(message, responseObj.stringify());
  return true;
}

void MediaControlService::onCoverArtFetched(const CoverArtFetcher::Result &result) {
  auto caller = coverArtCallers_.find(result.id);
  if (caller == coverArtCallers_.end())
    return;

  pbnjson::JValue responsePayload = pbnjson::Object();
  responsePayload.put("src", result.url);
  responsePayload.put("srcPath", result.path);
  responsePayload.put("returnValue", !result.path.empty());
  responsePayload.put("subscribed", true);
  CLSError lserror;
  if (!LSMessageRespond(caller->second, responsePayload.stringify().c_str(), &lserror))
    PMLOG_ERROR(CONST_MODULE_MCS, "%s LSMessageRespond failed: %s", __FUNCTION__, lserror.text());
  LSMessageUnref(caller->second);
  coverArtCallers_.erase(caller);
}
#endif

bool MediaControlService::setMediaMetaData(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_2(OBJECT(mediaMetaData, OBJSCHEMA_7( \
  OPTIONAL(title, string),OPTIONAL(artist, string),OPTIONAL(totalDuration, string),OPTIONAL( \
  album, string),OPTIONAL(genre, string),OPTIONAL(trackNumber, integer),OPTIONAL(volume, integer) \
  )),REQUIRED(mediaId, string)) REQUIRED_2(mediaMetaData, mediaId)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  pbnjson::JValue payload = msg.get();
  std::string mediaId = mediaIdOf(msg);
  pbnjson::JValue metaData = payload["mediaMetaData"];
  mediaMetaData objMetaData(metaData["title"].asString(), metaData["artist"].asString(),
                            metaData["totalDuration"].asString(), metaData["album"].asString(),
                            metaData["genre"].asString(), metaData["trackNumber"].asNumber<int>(),
                            metaData["volume"].asNumber<int>());
  PMLOG_INFO(CONST_MODULE_MCS, "%s mediaId : %s title : %s", __FUNCTION__, mediaId.c_str(),
             objMetaData.getTitle().c_str());

  int errorCode = mediaSessionMgr_.setMediaMetaData(mediaId, objMetaData);
  if (errorCode != MCS_ERROR_NO_ERROR)
    return replyError(message, errorCode);

  pbnjson::JObject metaDataObj;
  updateMetaDataResponse(mediaId, metaDataObj);
  pbnjson::JValue event = pbnjson::Object();
  event.put("mediaMetaData", metaDataObj);
  broadcastPlaybackInfo(mediaId, "mediaMetaData", event);
  return replySuccess(message);
}

bool MediaControlService::setMediaPlayStatus(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_2(REQUIRED(mediaId, string), \
  REQUIRED(playStatus, string)) REQUIRED_2(mediaId, playStatus)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::string mediaId = mediaIdOf(msg);
  std::string playStatus;
  msg.get("playStatus", playStatus);
  int errorCode = mediaSessionMgr_.setMediaPlayStatus(mediaId, playStatus);
  if (errorCode != MCS_ERROR_NO_ERROR)
    return replyError(message, errorCode);

  notifyBTPlayStatus(mediaId, playStatus);

  pbnjson::JValue event = pbnjson::Object();
  event.put("playStatus", playStatus);
  broadcastPlaybackInfo(mediaId, "playStatus", event);
  return replySuccess(message);
}

bool MediaControlService::setMediaMuteStatus(LSMessage &message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_2(REQUIRED(mediaId, string), \
  REQUIRED(muteStatus, string)) REQUIRED_2(mediaId, muteStatus)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::string mediaId = mediaIdOf(msg);
  std::string given;
  msg.get("muteStatus", given);
  std::string muteStatus = muteStatusOf(given);
  PMLOG_INFO(CONST_MODULE_MCS, "%s mediaId : %s muteStatus : %s", __FUNCTION__,
             mediaId.c_str(), given.c_str());
  if (muteStatus.empty())
    return replyError(message, MCS_ERROR_SESSION_INVALID_MUTE_STATUS);

  int errorCode = mediaSessionMgr_.setMediaMuteStatus(mediaId, muteStatus);
  if (errorCode != MCS_ERROR_NO_ERROR)
    return replyError(message, errorCode);

  pbnjson::JValue event = pbnjson::Object();
  event.put("muteStatus", muteStatus);
  broadcastPlaybackInfo(mediaId, "muteStatus", event);
  return replySuccess(message);
}

bool MediaControlService::setMediaPlayPosition(LSMessage &message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_2(REQUIRED(mediaId, string), \
  REQUIRED(playPosition, string)) REQUIRED_2(mediaId, playPosition)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::string mediaId = mediaIdOf(msg);
  std::string playPosition;
  msg.get("playPosition", playPosition);
  int errorCode = mediaSessionMgr_.setMediaPlayPosition(mediaId, playPosition);
  if (errorCode != MCS_ERROR_NO_ERROR)
    return replyError(message, errorCode);

  pbnjson::JValue event = pbnjson::Object();
  event.put("playPosition", playPosition);
  broadcastPlaybackInfo(mediaId, "playPosition", event);
  return replySuccess(message);
}

bool MediaControlService::receiveMediaPlaybackInfo(LSMessage &message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_3(REQUIRED(displayId, integer), \
    OPTIONAL(eventType, string),REQUIRED(subscribe, boolean)) REQUIRED_2(displayId, subscribe)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  pbnjson::JValue payload = msg.get();
  int displayId = payload["displayId"].asNumber<int>();
  if (displayId != 0 && displayId != 1)
    return replyError(message, MCS_ERROR_INVALID_DISPLAYID);
#if !defined(FEATURE_DUAL_DISPLAY)
  displayId = 0;
#endif
  std::string eventType;
  msg.get("eventType", eventType);
  if (!eventType.empty() && !isPlaybackEvent(eventType))
    return replyError(message, MCS_ERROR_INVALID_EVENT);

  PMLOG_INFO(CONST_MODULE_MCS, "%s displayId : %d eventType : %s", __FUNCTION__,
             displayId, eventType.c_str());

  /* The caller's own subscription, for what it asked for. It used to set a
   * flag for all callers instead, so that one caller asking for an event
   * made every caller get it - and one asking for all, all of them - for
   * as long as the service ran. */
  bool subscribed = false;
  if (LSMessageIsSubscription(&message)) {
    CLSError lserror;
    if (!LSSubscriptionAdd(lsHandle_, playbackInfoKey(displayId, eventType).c_str(), &message, &lserror)) {
      PMLOG_ERROR(CONST_MODULE_MCS, "%s LSSubscriptionAdd failed: %s", __FUNCTION__, lserror.text());
      return replyError(message, MCS_ERROR_SUBSCRIPTION_FAILED);
    }
    subscribed = true;
  }

  /* What there is now, to this caller only: it used to go to every caller
   * subscribed, as if it had changed. */
  std::string mediaId = mediaSessionMgr_.getMediaIdFromDisplayId(displayId);
  pbnjson::JValue responsePayload = pbnjson::Object();
  if (eventType == "playPosition" || eventType.empty()) {
    std::string playPosition;
    mediaSessionMgr_.getMediaPlayPosition(mediaId, playPosition);
    responsePayload.put("playPosition", playPosition);
  }
  if (eventType == "playStatus" || eventType.empty()) {
    std::string playStatus;
    mediaSessionMgr_.getMediaPlayStatus(mediaId, playStatus);
    responsePayload.put("playStatus", playStatus);
  }
  if (eventType == "muteStatus" || eventType.empty()) {
    std::string muteStatus;
    mediaSessionMgr_.getMediaMuteStatus(mediaId, muteStatus);
    responsePayload.put("muteStatus", muteStatus);
  }
  if (eventType == "mediaMetaData" || eventType.empty()) {
    pbnjson::JObject metaDataObj;
    updateMetaDataResponse(mediaId, metaDataObj);
    responsePayload.put("mediaMetaData", metaDataObj);
  }
  if (eventType == "coverArt" || eventType.empty()) {
    std::vector<mediaCoverArt> objCoverArt;
    mediaSessionMgr_.getMediaCoverArt(mediaId, objCoverArt);
    responsePayload.put("coverArt", coverArtJson(objCoverArt));
  }
  if (eventType == "supportedActions" || eventType.empty()) {
    std::vector<std::string> objActionList;
    mediaSessionMgr_.getActionList(mediaId, objActionList);
    responsePayload.put("supportedActions", actionsJson(objActionList));
  }
  if (!eventType.empty())
    responsePayload.put("eventType", eventType);

  mediaSession objMediaSession;
  if (mediaSessionMgr_.getMediaSessionInfo(mediaId, objMediaSession) != MCS_ERROR_NO_ERROR)
    PMLOG_INFO(CONST_MODULE_MCS, "%s no session on display %d yet", __FUNCTION__, displayId);
  responsePayload.put("mediaId", mediaId);
  responsePayload.put("appId", objMediaSession.getAppId());
  responsePayload.put("displayId", displayId);
  responsePayload.put("returnValue", true);
  responsePayload.put("subscribed", subscribed);

  PMLOG_INFO(CONST_MODULE_MCS, "%s response : %s", __FUNCTION__, responsePayload.stringify().c_str());
  respond(message, responsePayload.stringify());
  return true;
}

bool MediaControlService::injectMediaKeyEvent(LSMessage &message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_2(REQUIRED(displayId, integer), \
  REQUIRED(keyEvent, string)) REQUIRED_2(displayId, keyEvent)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  pbnjson::JValue payload = msg.get();
  int displayId = displayOf(payload);
  std::string keyEvent;
  msg.get("keyEvent", keyEvent);
  PMLOG_INFO(CONST_MODULE_MCS, "%s displayId : %d keyEvent : %s", __FUNCTION__,
             displayId, keyEvent.c_str());
  if (keyEvent.empty())
    return replyError(message, MCS_ERROR_INVALID_EVENT);

  std::string mediaId = mediaSessionMgr_.getMediaIdFromDisplayId(displayId);
  if (mediaId.empty())
    return replyError(message, MCS_ERROR_NO_ACTIVE_SESSION);
  if (!sendKeyEvent(mediaId, keyEvent))
    return replyError(message, MCS_ERROR_SUBSCRIPTION_REPLY_FAILED);
  return replySuccess(message);
}

bool MediaControlService::setMediaCoverArt(LSMessage& message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_2(REQUIRED(mediaId, string), \
  REQUIRED(coverArt, array)) REQUIRED_2(mediaId, coverArt)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  pbnjson::JValue payload = msg.get();
  std::string mediaId = mediaIdOf(msg);
  pbnjson::JValue coverArt = payload["coverArt"];
  if (coverArt.arraySize() <= 0)
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::vector<mediaCoverArt> coverArtData;
  for (int i = 0; i < coverArt.arraySize(); i++) {
    pbnjson::JValue item = coverArt[i];
    if (!item.isObject() || !item["src"].isString())
      return replyError(message, MCS_ERROR_PARSING_FAILED);
    if (item.hasKey("type") && !item["type"].isString())
      return replyError(message, MCS_ERROR_PARSING_FAILED);
    pbnjson::JValue coverArtSizes = item["sizes"];
    if (item.hasKey("sizes") && !coverArtSizes.isArray())
      return replyError(message, MCS_ERROR_PARSING_FAILED);

    std::vector<coverArtSize> sizes;
    for (int j = 0; coverArtSizes.isArray() && j < coverArtSizes.arraySize(); j++) {
      pbnjson::JValue size = coverArtSizes[j];
      if (!size.isObject() ||
          (size.hasKey("width") && !size["width"].isNumber()) ||
          (size.hasKey("height") && !size["height"].isNumber()))
        return replyError(message, MCS_ERROR_PARSING_FAILED);
      sizes.push_back({size["width"].asNumber<int>(), size["height"].asNumber<int>()});
    }
    coverArtData.emplace_back(item["src"].asString(), item["type"].asString(), std::move(sizes));
  }

  int errorCode = mediaSessionMgr_.setMediaCoverArt(mediaId, coverArtData);
  if (errorCode != MCS_ERROR_NO_ERROR)
    return replyError(message, errorCode);

  pbnjson::JValue event = pbnjson::Object();
  event.put("coverArt", coverArtJson(coverArtData));
  broadcastPlaybackInfo(mediaId, "coverArt", event);
  return replySuccess(message);
}

#if USE_TEST_METHOD
bool MediaControlService::testKeyEvent(LSMessage &message) {
  LSMessageJsonParser msg(&message,STRICT_SCHEMA(PROPS_2(REQUIRED(mediaId, string), \
  REQUIRED(keyEvent, string)) REQUIRED_2(mediaId, keyEvent)));
  if (!msg.parse(__FUNCTION__))
    return replyError(message, MCS_ERROR_PARSING_FAILED);

  std::string keyEvent;
  msg.get("keyEvent", keyEvent);
  if (!sendKeyEvent(mediaIdOf(msg), keyEvent))
    return replyError(message, MCS_ERROR_SUBSCRIPTION_REPLY_FAILED);
  return replySuccess(message);
}
#endif

int MediaControlService::updateMetaDataResponse(const std::string &mediaId, pbnjson::JObject &metaDataObj) {
  mediaMetaData objMetaData;
  int errorCode = mediaSessionMgr_.getMediaMetaData(mediaId, objMetaData);
  if (errorCode != MCS_ERROR_NO_ERROR)
    return errorCode;

  if (!objMetaData.getTitle().empty())
    metaDataObj.put("title", objMetaData.getTitle());
  if (!objMetaData.getArtist().empty())
    metaDataObj.put("artist", objMetaData.getArtist());
  if (!objMetaData.getDuration().empty())
    metaDataObj.put("totalDuration", objMetaData.getDuration());
  if (!objMetaData.getAlbum().empty())
    metaDataObj.put("album", objMetaData.getAlbum());
  if (!objMetaData.getGenre().empty())
    metaDataObj.put("genre", objMetaData.getGenre());
  if (objMetaData.getTrackNumber())
    metaDataObj.put("trackNumber", objMetaData.getTrackNumber());
  if (objMetaData.getVolume())
    metaDataObj.put("volume", objMetaData.getVolume());
  return errorCode;
}
