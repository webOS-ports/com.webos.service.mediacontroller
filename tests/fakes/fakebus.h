// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

#ifndef FAKEBUS_H
#define FAKEBUS_H

#include <map>
#include <string>
#include <vector>

#include <luna-service2/lunaservice.h>

/**
 * luna-service2, in process. The test executable defines the LS* functions
 * the service uses and is not linked with the real library, so these are
 * the only ones there are.
 *
 * Messages are reference counted as the real ones are: a call to the
 * service holds one reference while its handler runs, a subscription one
 * until it ends, and the service its own through LSMessageRef(). At zero
 * the message is freed, so a message used after the last reference is a
 * use after free ASan sees.
 *
 * Every failure it reports carries an LSError whose message is heap
 * allocated, so a missing LSErrorFree() shows up as a leak.
 */
namespace fakebus {

void reset();

//! Make the next call to the named LS function fail.
void failNext(const std::string& function);

struct Caller {
    std::string sender = ":1.100";
};

//! Calls a method the service registered under "/" as caller would.
//! Returns what the handler returned; the token of the call is lastToken().
bool callMethod(const std::string& method, const std::string& payload,
                const Caller& caller = Caller());
const std::string& lastToken();

struct Reply {
    std::string to;      //!< unique token of the call replied to
    std::string payload;
};
//! Every reply the service sent, in order, since reset().
const std::vector<Reply>& replies();
//! The replies sent to the call with this token.
std::vector<std::string> repliesTo(const std::string& token);

//! The caller ends its call: the cancel function runs if it is subscribed.
bool cancel(const std::string& token);
//! The sender disconnects: its subscriptions end, and replies to its
//! calls fail from now on.
int disconnect(const std::string& sender);
//! Unique tokens of the open subscriptions under key.
std::vector<std::string> subscribers(const std::string& key);
//! All keys with subscribers.
std::vector<std::string> subscriptionKeys();

//! Methods registered under "/", in table order.
const std::vector<std::string>& methods();
const std::string& registeredName();
bool cancelFunctionSet();

/** A call the service made. */
struct OutCall {
    LSMessageToken token;
    std::string uri;
    std::string payload;
    bool oneReply;
};
//! Calls the service has open, oldest first.
std::vector<OutCall> openCalls();
//! Open calls to uri.
std::vector<OutCall> openCalls(const std::string& uri);
//! Every call made since reset(), open or not.
const std::vector<OutCall>& callsMade();
//! The reply payload arrives for the call with token; a one-reply call is
//! over afterwards. False if no such call is open.
bool deliver(LSMessageToken token, const std::string& payload);
//! The callee ends the call (as when it goes away); no reply is delivered.
bool endCall(LSMessageToken token);

//! Calls made to each LS function since reset().
int calls(const std::string& function);
//! LSErrors handed out and not yet freed.
int errorsOutstanding();
//! Handles registered and not yet unregistered.
int handlesOutstanding();
//! Messages alive (not yet at zero references).
int messagesAlive();

} // namespace fakebus

#endif // FAKEBUS_H
