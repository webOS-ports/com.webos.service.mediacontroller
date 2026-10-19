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

#ifndef MEDIA_CONTROL_TYPES_H_
#define MEDIA_CONTROL_TYPES_H_

/*-----------------------------------------------------------------------------
 (File Inclusions)
 ------------------------------------------------------------------------------*/
#include <string>
#include <vector>
#include "PmLogLib.h"
#include <luna-service2/lunaservice.h>

const std::string CSTR_INVALID_MEDIAID = "Invalid mediaId";
const std::string CSTR_REGISTERSESSION_FAILED = "MediaId registeration failed";
const std::string CSTR_PARSING_ERROR = "Parsing Error";
const std::string CSTR_INVALID_APPID = "Invalid appId";
const std::string CSTR_NO_ACTIVE_SESSION = "No session is active";
const std::string CSTR_SESSION_ALREADY_REGISTERED = "Media session already registered";
const std::string CSTR_SESSION_INVALID_PLAY_STATE = "Invalid Play State";
const std::string CSTR_SESSION_INVALID_MUTE_STATUS = "Invalid Mute Status";
const std::string CSTR_SUBSCRIPTION_FAILED = "LSSubscriptionAdd failed";
const std::string CSTR_SUBSCRIPTION_REPLY_FAILED = "LSSubscriptionReply failed";
const std::string CSTR_EMPTY = "";
const std::string CTSR_INVALID_DISPLAYID = "Invalid displayId";
const std::string CTSR_INVALID_EVENT = "Invalid Event";

#define CONST_MODULE_MCS "MediaControlService"
#define CONST_MODULE_MCP "MediaControlPrivate"
#define CONST_MODULE_MSM "MediaSessionManager"
#define CONST_MODULE_RR  "RequestReceiver"
#define CONST_MODULE_MCD  "MediaControlDownloader"
#define CONST_MODULE_MCFM  "MediaControlFileManager"

#define SET 1
#define RESET 0

#define PMLOG_ERROR(module, args...) PmLogMsg(getLunaPmLogContext(), Error, module, 0, ##args)
#define PMLOG_INFO(module, args...) PmLogMsg(getLunaPmLogContext(), Info, module, 0, ##args)
#define PMLOG_DEBUG(args...) PmLogMsg(getLunaPmLogContext(), Debug, NULL, 0, ##args)

enum MCSErrorCode {
  MCS_ERROR_INVALID_MEDIAID = 0,
  MCS_ERROR_REGISTERSESSION_FAILED,
  MCS_ERROR_PARSING_FAILED,
  MCS_ERROR_INVALID_APPID,
  MCS_ERROR_NO_ACTIVE_SESSION,
  MCS_ERROR_SESSION_ALREADY_REGISTERED,
  MCS_ERROR_SUBSCRIPTION_FAILED,
  MCS_ERROR_SUBSCRIPTION_REPLY_FAILED,
  MCS_ERROR_SESSION_INVALID_PLAY_STATE,
  MCS_ERROR_SESSION_INVALID_MUTE_STATUS,
  MCS_ERROR_NO_ERROR,
  MCS_ERROR_INVALID_DISPLAYID,
  MCS_ERROR_INVALID_EVENT
};

enum MediaPlayState {
  PLAY_STATE_NONE = 0,
  PLAY_STATE_STOPPED,
  PLAY_STATE_PAUSED,
  PLAY_STATE_PLAYING,
  PLAY_STATE_FAST_FORWARDING,
  PLAY_STATE_REWINDING,
  PLAY_STATE_BUFFERING,
  PLAY_STATE_ERROR
};

class mediaMetaData {
private:
  std::string title_;
  std::string artist_;
  std::string totalDuration_;
  std::string album_;
  std::string genre_;
  int trackNumber_;
  int volume_;
public:
  mediaMetaData() :
    trackNumber_(0),
    volume_(0) {}
  mediaMetaData(const std::string& title, const std::string& artist,
                const std::string& duration, const std::string& album,
                const std::string& genre, const int& trackNumber, const int& volume) :
    title_(title),
    artist_(artist),
    totalDuration_(duration),
    album_(album),
    genre_(genre),
    trackNumber_(trackNumber),
    volume_(volume) {}

  std::string getTitle() const {return title_;}
  std::string getArtist() const {return artist_;}
  std::string getDuration() const {return totalDuration_;}
  std::string getAlbum() const {return album_;}
  std::string getGenre() const {return genre_;}
  int getTrackNumber() const {return trackNumber_;}
  int getVolume() const {return volume_;}

  void setTitle(const std::string& title) {
    if(!title.empty() && title_ != title)
      title_ = title;
  }
  void setArtist(const std::string& artist) {
    if(!artist.empty() && artist_ != artist)
      artist_ = artist;
  }
  void setDuration(const std::string& duration) {
    if(!duration.empty() && totalDuration_ != duration)
      totalDuration_ = duration;
  }
  void setAlbum(const std::string& album) {
    if(!album.empty() && album_ != album)
      album_ = album;
  }
  void setGenre(const std::string& genre) {
    if(!genre.empty() && genre_ != genre)
      genre_ = genre;
  }
  void setTrackNumber(const int& trackNum) {
    if(trackNum && trackNumber_ != trackNum)
      trackNumber_ = trackNum;
  }
  void setVolume(const int& volume) {
    if(volume && volume_ != volume)
      volume_ = volume;
  }
};

typedef struct {
  int width;
  int height;

  //TODO Overload the == operator to compare two coverArtSize struct
  //bool operator!=(const coverArtSize& other) const {
  //  return (width != other.width) || (height != other.height);
  //}

} coverArtSize;

class mediaCoverArt {
private:
  std::string src_;
  std::string type_;
  std::vector<coverArtSize> size_;
public:
  mediaCoverArt() :
    src_(""),
    type_("") {}
  mediaCoverArt(const std::string& src, const std::string& type,
                const std::vector<coverArtSize> size) :
    src_(src),
    type_(type),
    size_(std::move(size)) {}

  std::string getSource() const {return src_;}
  std::string getType() const {return type_;}
  std::vector<coverArtSize> getSize() const {return size_;}

  void setSource(const std::string& src) {
    if(!src.empty() && src_ != src)
      src_ = src;
  }
  void setType(const std::string& type) {
    if(!type.empty() && type_ != type)
      type_ = type;
  }
  void setSize(const std::vector<coverArtSize> size) {
    size_ = std::move(size);
  }
};

class mediaSession {
private:
  std::string mediaId_;
  std::string appId_;
  std::string playStatus_;
  std::string muteStatus_;
  std::string playPosition_;
  mediaMetaData objMetaData_;
  std::vector<mediaCoverArt> objCoverArt_;
  std::vector<std::string> enableActions_;

public:
  mediaSession() :
    playStatus_("PLAYSTATE_NONE"),
    muteStatus_("unmute"),
    playPosition_("0.0"){}
  mediaSession(const std::string& mediaId, const std::string& appId) :
    mediaId_(mediaId),
    appId_(appId),
    playStatus_("PLAYSTATE_NONE"),
    muteStatus_("unmute"),
    playPosition_("0.0"){}

  std::string getMediaId() const { return mediaId_; }
  std::string getAppId() const { return appId_; }
  std::string getPlayStatus() const {
    return playStatus_;
  }
  std::string getMuteStatus() const {
    return muteStatus_;
  }
  std::string getPlayposition() const {
    return playPosition_;
  }
  mediaMetaData getMediaMetaDataObj() const { return objMetaData_; }

  std::vector<mediaCoverArt> getMediaCoverArtObj() const { return objCoverArt_; }

  std::vector<std::string> getActionObj() const { return enableActions_; }

  void setMediaId(const std::string& mediaId) {
    mediaId_ = mediaId;
  }
  void setAppId(const std::string& appId) {
    appId_ = appId;
  }
  void setPlayStatus(const std::string& playStatus) {
    playStatus_ = playStatus;
  }
  void setMuteStatus(const std::string& muteStatus) {
    muteStatus_ = muteStatus;
  }
  void setPlayposition(const std::string& playPosition) {
    playPosition_ = playPosition;
  }
  void setMetaData(const mediaMetaData& objMetaData) {
    objMetaData_.setTitle(objMetaData.getTitle());
    objMetaData_.setArtist(objMetaData.getArtist());
    objMetaData_.setDuration(objMetaData.getDuration());
    objMetaData_.setAlbum(objMetaData.getAlbum());
    objMetaData_.setGenre(objMetaData.getGenre());
    objMetaData_.setTrackNumber(objMetaData.getTrackNumber());
    objMetaData_.setVolume(objMetaData.getVolume());
  }

  void setCoverArt(const std::vector<mediaCoverArt>& objCoverArt) {
    objCoverArt_ = objCoverArt;
  }

  void setAction(const std::vector<std::string>& enableActions) {
    enableActions_ = enableActions;
  }
};

struct BTDeviceInfo {
  std::string deviceAddress_;
  std::string adapterAddress_;
  int displayId_ = 0;
  /* The device's avrcp/receivePassThroughCommand subscription. */
  LSMessageToken passThroughToken_ = LSMESSAGE_TOKEN_INVALID;
  BTDeviceInfo() {}
  BTDeviceInfo(const std::string& address, const std::string &adapterAddress, int displayId = 0) :
    deviceAddress_(address),
    adapterAddress_(adapterAddress),
    displayId_(displayId)
  {}
};

static std::string getErrorTextFromErrorCode(const int& errorCode) {
  switch(errorCode) {
    case MCS_ERROR_INVALID_MEDIAID:
      return CSTR_INVALID_MEDIAID;
    case MCS_ERROR_REGISTERSESSION_FAILED:
      return CSTR_REGISTERSESSION_FAILED;
    case MCS_ERROR_PARSING_FAILED:
      return CSTR_PARSING_ERROR;
    case MCS_ERROR_INVALID_APPID:
      return CSTR_INVALID_APPID;
    case MCS_ERROR_NO_ACTIVE_SESSION:
      return CSTR_NO_ACTIVE_SESSION;
    case MCS_ERROR_SESSION_ALREADY_REGISTERED:
      return CSTR_SESSION_ALREADY_REGISTERED;
    case MCS_ERROR_SESSION_INVALID_PLAY_STATE:
      return CSTR_SESSION_INVALID_PLAY_STATE;
    case MCS_ERROR_SUBSCRIPTION_FAILED:
      return CSTR_SUBSCRIPTION_FAILED;
    case MCS_ERROR_SUBSCRIPTION_REPLY_FAILED:
      return CSTR_SUBSCRIPTION_REPLY_FAILED;
    case MCS_ERROR_INVALID_DISPLAYID:
      return CTSR_INVALID_DISPLAYID;
    case MCS_ERROR_INVALID_EVENT:
      return CTSR_INVALID_EVENT;
    default:
      return CSTR_EMPTY;
  }
}

static PmLogContext getLunaPmLogContext() {
  static PmLogContext logContext = 0;
  if (0 == logContext)
    PmLogGetContext("mediacontroller", &logContext);
  return logContext;
}

#endif /*MEDIA_CONTROL_SERVICE_H_*/
