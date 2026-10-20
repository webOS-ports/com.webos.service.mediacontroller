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
#include "RequestReceiver.h"

RequestReceiver::RequestReceiver() : clientListInfo_()
{
}

void RequestReceiver::addClient (const std::string& mediaId) {
  PMLOG_INFO(CONST_MODULE_RR, "%s mediaId : %s", __FUNCTION__, mediaId.c_str());
  //the last activated is at the front, once
  clientListInfo_.remove(mediaId);
  clientListInfo_.push_front(mediaId);
}

void RequestReceiver::removeClient (const std::string& mediaId) {
  PMLOG_INFO(CONST_MODULE_RR, "%s mediaId : %s", __FUNCTION__, mediaId.c_str());
  clientListInfo_.remove(mediaId);
}

std::string RequestReceiver::getLastActiveClient () const {
  if (clientListInfo_.empty())
    return CSTR_EMPTY;
  return clientListInfo_.front();
}
