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

#ifndef MEDIA_CONTROL_PRIVATE_H_
#define MEDIA_CONTROL_PRIVATE_H_

/*-----------------------------------------------------------------------------
 (File Inclusions)
 ------------------------------------------------------------------------------*/
#include <map>
#include <string>
#include <vector>

#include "MediaControlTypes.h"

class MediaControlPrivate
{
public:
  MediaControlPrivate();

  /* Bluetooth devices with AVRCP connected, keyed by
   * "<adapter address>/<device address>". */
  void setBTDeviceInfo(const BTDeviceInfo& objDevInfo);
  bool removeBTDevice(const std::string& address, const std::string& adapterAddress,
                      BTDeviceInfo *removed);
  std::vector<BTDeviceInfo> getBTDevices(int displayId) const;
  std::vector<BTDeviceInfo> takeBTDevices();
  bool isDeviceRegistered(const std::string& address, const std::string& adapterAddress) const;

private:
  std::map<std::string, BTDeviceInfo> mapDeviceInfo_;
};

#endif /*MEDIA_CONTROL_PRIVATE_H_*/
