// Copyright (c) 2020 LG Electronics, Inc.
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
#include "MediaControlPrivate.h"

MediaControlPrivate::MediaControlPrivate() :
  playStatus_(false), muteStatus_(false), playPosition_(false),
  mediaMetaData_(false), coverArt_(false), enableMediaAction_(false), mapDeviceInfo_() {
}

MediaControlPrivate& MediaControlPrivate::getInstance() {
  static MediaControlPrivate objMediaCtrlPrivate;
  return objMediaCtrlPrivate;
}

static std::string deviceKey(const std::string& address, const std::string& adapterAddress) {
  return adapterAddress + "/" + address;
}

void MediaControlPrivate::setBTDeviceInfo(const BTDeviceInfo& objDevInfo) {
  PMLOG_INFO(CONST_MODULE_MCP, "%s device %s on adapter %s", __FUNCTION__,
             objDevInfo.deviceAddress_.c_str(), objDevInfo.adapterAddress_.c_str());
  mapDeviceInfo_[deviceKey(objDevInfo.deviceAddress_, objDevInfo.adapterAddress_)] = objDevInfo;
}

bool MediaControlPrivate::removeBTDevice(const std::string& address, const std::string& adapterAddress,
                                         BTDeviceInfo *removed) {
  auto itr = mapDeviceInfo_.find(deviceKey(address, adapterAddress));
  if (itr == mapDeviceInfo_.end())
    return false;
  PMLOG_INFO(CONST_MODULE_MCP, "%s device %s on adapter %s", __FUNCTION__,
             address.c_str(), adapterAddress.c_str());
  if (removed)
    *removed = itr->second;
  mapDeviceInfo_.erase(itr);
  return true;
}

std::vector<BTDeviceInfo> MediaControlPrivate::getBTDevices(int displayId) const {
  std::vector<BTDeviceInfo> devices;
  for (const auto& itr : mapDeviceInfo_) {
    if (itr.second.displayId_ == displayId)
      devices.push_back(itr.second);
  }
  return devices;
}

std::vector<BTDeviceInfo> MediaControlPrivate::takeBTDevices() {
  std::vector<BTDeviceInfo> devices;
  for (auto& itr : mapDeviceInfo_)
    devices.push_back(std::move(itr.second));
  mapDeviceInfo_.clear();
  return devices;
}

bool MediaControlPrivate::isDeviceRegistered(const std::string& address,
                                             const std::string& adapterAddress) const {
  return mapDeviceInfo_.count(deviceKey(address, adapterAddress)) != 0;
}
