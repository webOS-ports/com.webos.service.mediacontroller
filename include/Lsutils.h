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

#ifndef LSUTILS_H_
#define LSUTILS_H_

/*-----------------------------------------------------------------------------
 (File Inclusions)
 ------------------------------------------------------------------------------*/
#include <string>
#include <pbnjson.hpp>
#include <luna-service2/lunaservice.h>

#define SYSTEM_PARAMETERS "\"$activity\":{\"type\":\"object\",\"optional\":true}"

// Build a schema as a const char * string without any execution overhead
#define SCHEMA_ANY "{}"
#define SCHEMA_0 "{\"type\":\"object\",\"properties\":{" SYSTEM_PARAMETERS "},\"additionalProperties\":false}"
#define SCHEMA_2(p1, p2) "{\"type\":\"object\",\"properties\":{" p1 "," p2 "," SYSTEM_PARAMETERS "},\"additionalProperties\":false}"
#define SCHEMA_5(p1, p2, p3, p4, p5) "{\"type\":\"object\",\"properties\":{" p1 "," p2 "," p3 "," p4 "," p5 "," SYSTEM_PARAMETERS "},\"additionalProperties\":false}"
#define SCHEMA_6(p1, p2, p3, p4, p5, p6) "{\"type\":\"object\",\"properties\":{" p1 "," p2 "," p3 "," p4 "," p5 "," p6 "," SYSTEM_PARAMETERS "},\"additionalProperties\":false}"
#define SCHEMA_8(p1, p2, p3, p4, p5, p6, p7, p8) "{\"type\":\"object\",\"properties\":{" p1 "," p2 "," p3 "," p4 "," p5 "," p6 "," p7 "," p8 "," SYSTEM_PARAMETERS "},\"additionalProperties\":false}"
#define SCHEMA_9(p1, p2, p3, p4, p5, p6, p7, p8, p9) "{\"type\":\"object\",\"properties\":{" p1 "," p2 "," p3 "," p4 "," p5 "," p6 "," p7 "," p8 "," p9 "," SYSTEM_PARAMETERS "},\"additionalProperties\":false}"
#define OBJECT(name, objschema) "\"" #name "\":" objschema
#define OBJSCHEMA_7(p1, p2, p3, p4, p5, p6, p7) "{\"type\":\"object\",\"properties\":{" p1 "," p2 "," p3 "," p4 "," p5 "," p6 "," p7 "}}"
#define PROPS_1(p1) ",\"properties\":{" p1 "}"
#define PROPS_2(p1, p2) ",\"properties\":{" p1 "," p2 "}"
#define PROPS_3(p1, p2, p3) ",\"properties\":{" p1 "," p2 "," p3 "}"
#define STRICT_SCHEMA(attributes) "{\"type\":\"object\"" attributes ",\"additionalProperties\":false}"
#define REQUIRED_1(p1) ",\"required\":[\"" #p1 "\"]"
#define REQUIRED_2(p1, p2) ",\"required\":[\"" #p1 "\",\"" #p2 "\"]"
#define REQUIRED_3(p1, p2, p3) ",\"required\":[\"" #p1 "\",\"" #p2 "\",\"" #p3 "\"]"

// Macros to use in place of the parameters in the SCHEMA_xxx macros above
#define REQUIRED(name, type) "\"" #name "\":{\"type\":\"" #type "\"}"
#define OPTIONAL(name, type) "\"" #name "\":{\"type\":\"" #type "\",\"optional\":true}"

/*
 * LSError that frees itself: luna-service2 allocates the message of an error
 * it reports, and every failure path used to leak it.
 */
struct CLSError : public LSError
{
  CLSError()
  {
    LSErrorInit(this);
  }
  ~CLSError()
  {
    if (LSErrorIsSet(this))
      LSErrorFree(this);
  }
  CLSError(const CLSError&) = delete;
  CLSError& operator=(const CLSError&) = delete;

  //! The message, or "" when none was set.
  const char *text() const
  {
    return message ? message : "";
  }
};

class LSMessageJsonParser
{
public:
  // Default no using any specific schema. Will simply validate that the message is a valid json message.
  LSMessageJsonParser(LSMessage * message, const char * schema);

  //Parse the message using the schema passed in constructor; false, and
  //the reason logged, if it is not JSON or does not match. Replying is
  //the caller's.
  bool parse(const char *func);
  pbnjson::JValue get() { return mParser_.getDom(); }
  const char * getPayload() { return LSMessageGetPayload(mMessage_); }

  // convenience functions to get a parameter directly.
  bool get(const char * name, std::string & str){ return get()[name].asString(str) == CONV_OK; }
  bool get(const char * name, bool & boolean) { return get()[name].asBool(boolean) == CONV_OK; }
  template <class T> bool get(const char *name, T &number) { return get()[name].asNumber<T>(number) == CONV_OK; }

private:
  LSMessage *mMessage_;
  const char *mSchemaText_;
  pbnjson::JSchemaFragment mSchema_;
  pbnjson::JDomParser mParser_;
};

// build a standard json reply string without the overhead of using json schema
std::string createJsonReplyString(bool returnValue = true, int errorCode = 0, const std::string& errorText = "" );

#endif /*LSUTILS_H_*/
