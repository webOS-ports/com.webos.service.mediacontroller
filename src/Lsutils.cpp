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
#include "Lsutils.h"
#include "MediaControlTypes.h"

#include <cstring>

LSMessageJsonParser::LSMessageJsonParser(LSMessage * message,
                                         const char * schema) :
                                         mMessage_(message),
                                         mSchemaText_(schema),
                                         mSchema_(schema) {
}

bool LSMessageJsonParser::parse(const char * callerFunction) {
  const char * payload = getPayload();
  if (!payload) {
    PMLOG_ERROR(CONST_MODULE_MCS, "%s : message without a payload", callerFunction);
    return false;
  }
  if (mParser_.parse(payload, mSchema_))
    return true;

  pbnjson::JSchemaFragment genericSchema(SCHEMA_ANY);
  if (strcmp(mSchemaText_, SCHEMA_ANY) == 0 || !mParser_.parse(payload, genericSchema))
    PMLOG_ERROR(CONST_MODULE_MCS, "%s : '%s' is not a valid JSON message", callerFunction, payload);
  else
    PMLOG_ERROR(CONST_MODULE_MCS, "%s : '%s' does not match the schema '%s'", callerFunction,
                payload, mSchemaText_);
  return false;
}

std::string createJsonReplyString(bool returnValue,
                                  int errorCode,
                                  const std::string& errorText) {
  pbnjson::JValue responseObj = pbnjson::Object();
  if (returnValue)
    responseObj.put("returnValue", returnValue);
  else {
    responseObj.put("returnValue", false);
    responseObj.put("errorCode", errorCode);
    responseObj.put("errorText", errorText);
  }

  return responseObj.stringify();
}
