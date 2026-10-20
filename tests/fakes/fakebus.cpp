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

#include "fakebus.h"

#include <glib.h>
#include <pbnjson.hpp>

#include <stdio.h>

#include <algorithm>
#include <set>

struct LSHandle {
    std::string name;
    std::vector<LSMethod> methods;
    void* categoryData = nullptr;
    LSFilterFunc cancelFunction = nullptr;
    void* cancelContext = nullptr;
};

struct LSMessage {
    std::string payload;
    std::string token;      // unique token of a call to the service
    std::string sender;
    std::string method;
    LSMessageToken responseToken = LSMESSAGE_TOKEN_INVALID; // reply to the service's call
    int refs = 1;
};

namespace {

struct Subscription {
    std::string key;
    LSMessage* message;
};

struct Call {
    fakebus::OutCall call;
    LSFilterFunc callback;
    void* context;
};

std::set<std::string> s_failNext;
std::vector<fakebus::Reply> s_replies;
std::vector<std::string> s_methods;
std::map<std::string, int> s_calls;
std::set<std::string> s_gone;
std::string s_name;
std::string s_lastToken;
LSHandle* s_handle = nullptr;
int s_errors = 0;
int s_handles = 0;
int s_messages = 0;
int s_serial = 0;
LSMessageToken s_nextToken = 0;
std::vector<Subscription> s_subscriptions; // oldest first; each holds a reference
std::vector<Call> s_open;
std::vector<fakebus::OutCall> s_made;

bool enter(const char* function, LSError* error, const char* why = "failed as asked")
{
    ++s_calls[function];
    if (!s_failNext.erase(function))
        return false;
    if (error) {
        error->error_code = -1;
        error->message = g_strdup_printf("fakebus: %s %s", function, why);
        error->file = __FILE__;
        error->line = __LINE__;
        error->func = function;
        ++s_errors;
    }
    return true;
}

bool fail(const char* function, LSError* error, const char* why)
{
    s_failNext.insert(function);
    return !enter(function, error, why);
}

LSMessage* newMessage()
{
    ++s_messages;
    return new LSMessage();
}

void unref(LSMessage* message)
{
    if (--message->refs == 0) {
        --s_messages;
        delete message;
    }
}

void endSubscription(size_t i, bool notify)
{
    LSMessage* message = s_subscriptions[i].message;
    s_subscriptions.erase(s_subscriptions.begin() + static_cast<long>(i));
    // a message subscribed under several keys is cancelled once, when the
    // last of them goes, as the catalog does
    bool last = std::none_of(s_subscriptions.begin(), s_subscriptions.end(),
                             [message](const Subscription& s) { return s.message == message; });
    if (notify && last && s_handle && s_handle->cancelFunction)
        s_handle->cancelFunction(s_handle, message, s_handle->cancelContext);
    unref(message);
}

void dropEverything()
{
    for (const Subscription& s : s_subscriptions)
        unref(s.message);
    s_subscriptions.clear();
    s_open.clear();
}

} // namespace

namespace fakebus {

void reset()
{
    dropEverything();
    s_failNext.clear();
    s_replies.clear();
    s_methods.clear();
    s_calls.clear();
    s_gone.clear();
    s_name.clear();
    s_lastToken.clear();
    s_made.clear();
    s_errors = 0;
    // a handle or message the service leaked stays leaked for the leak
    // checkers; the counts keep it
    s_handle = nullptr;
}

void failNext(const std::string& function) { s_failNext.insert(function); }

bool callMethod(const std::string& method, const std::string& payload, const Caller& caller)
{
    if (!s_handle)
        return false;
    for (const LSMethod& m : s_handle->methods) {
        if (method != m.name)
            continue;
        LSMessage* message = newMessage();
        message->payload = payload;
        message->token = caller.sender + "." + std::to_string(++s_serial);
        message->sender = caller.sender;
        message->method = method;
        s_lastToken = message->token;
        bool result = m.function(s_handle, message, s_handle->categoryData);
        unref(message);
        return result;
    }
    return false;
}

const std::string& lastToken() { return s_lastToken; }
const std::vector<Reply>& replies() { return s_replies; }

std::vector<std::string> repliesTo(const std::string& token)
{
    std::vector<std::string> out;
    for (const Reply& r : s_replies) {
        if (r.to == token)
            out.push_back(r.payload);
    }
    return out;
}

bool cancel(const std::string& token)
{
    bool found = false;
    for (size_t i = s_subscriptions.size(); i-- > 0;) {
        if (s_subscriptions[i].message->token == token) {
            endSubscription(i, true);
            found = true;
        }
    }
    return found;
}

int disconnect(const std::string& sender)
{
    s_gone.insert(sender);
    int n = 0;
    for (size_t i = s_subscriptions.size(); i-- > 0;) {
        if (i < s_subscriptions.size() && s_subscriptions[i].message->sender == sender) {
            endSubscription(i, true);
            ++n;
        }
    }
    return n;
}

std::vector<std::string> subscribers(const std::string& key)
{
    std::vector<std::string> tokens;
    for (const Subscription& s : s_subscriptions) {
        if (s.key == key)
            tokens.push_back(s.message->token);
    }
    return tokens;
}

std::vector<std::string> subscriptionKeys()
{
    std::set<std::string> keys;
    for (const Subscription& s : s_subscriptions)
        keys.insert(s.key);
    return std::vector<std::string>(keys.begin(), keys.end());
}

const std::vector<std::string>& methods() { return s_methods; }
const std::string& registeredName() { return s_name; }
bool cancelFunctionSet() { return s_handle && s_handle->cancelFunction; }

std::vector<OutCall> openCalls()
{
    std::vector<OutCall> out;
    for (const Call& c : s_open)
        out.push_back(c.call);
    return out;
}

std::vector<OutCall> openCalls(const std::string& uri)
{
    std::vector<OutCall> out;
    for (const Call& c : s_open) {
        if (c.call.uri == uri)
            out.push_back(c.call);
    }
    return out;
}

const std::vector<OutCall>& callsMade() { return s_made; }

bool deliver(LSMessageToken token, const std::string& payload)
{
    auto open = std::find_if(s_open.begin(), s_open.end(),
                             [token](const Call& c) { return c.call.token == token; });
    if (open == s_open.end())
        return false;
    Call call = *open;
    if (call.call.oneReply)
        s_open.erase(open);
    if (!call.callback)
        return true;
    LSMessage* reply = newMessage();
    reply->payload = payload;
    reply->responseToken = token;
    call.callback(s_handle, reply, call.context);
    unref(reply);
    return true;
}

bool endCall(LSMessageToken token)
{
    auto open = std::find_if(s_open.begin(), s_open.end(),
                             [token](const Call& c) { return c.call.token == token; });
    if (open == s_open.end())
        return false;
    s_open.erase(open);
    return true;
}

int calls(const std::string& function) { return s_calls[function]; }
int errorsOutstanding() { return s_errors; }
int handlesOutstanding() { return s_handles; }
int messagesAlive() { return s_messages; }

} // namespace fakebus

extern "C" {

bool LSErrorInit(LSError* error)
{
    error->error_code = 0;
    error->message = nullptr;
    error->file = nullptr;
    error->line = 0;
    error->func = nullptr;
    error->padding = nullptr;
    error->magic = 0;
    return true;
}

void LSErrorFree(LSError* error)
{
    if (error && error->message) {
        g_free(error->message);
        error->message = nullptr;
        --s_errors;
    }
}

bool LSErrorIsSet(LSError* error)
{
    return error && error->message;
}

void LSErrorPrint(LSError* error, FILE* out)
{
    if (error && error->message)
        fprintf(out, "LSError: %s\n", error->message);
}

bool LSRegister(const char* name, LSHandle** sh, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;
    *sh = new LSHandle();
    (*sh)->name = name;
    s_name = name;
    s_handle = *sh;
    ++s_handles;
    return true;
}

bool LSRegisterCategory(LSHandle* sh, const char* category, LSMethod* methods,
                        LSSignal*, LSProperty*, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;
    if (std::string(category) != "/")
        return true;
    for (LSMethod* m = methods; m && m->name; ++m) {
        sh->methods.push_back(*m);
        s_methods.push_back(m->name);
    }
    return true;
}

bool LSCategorySetData(LSHandle* sh, const char*, void* user_data, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;
    sh->categoryData = user_data;
    return true;
}

bool LSSubscriptionSetCancelFunction(LSHandle* sh, LSFilterFunc cancelFunction, void* ctx, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;
    sh->cancelFunction = cancelFunction;
    sh->cancelContext = ctx;
    return true;
}

bool LSGmainAttach(LSHandle*, GMainLoop*, LSError* lserror)
{
    return !enter(__func__, lserror);
}

bool LSUnregister(LSHandle* service, LSError* lserror)
{
    // Frees the handle whatever it reports; its subscriptions and calls go
    // with it, without the cancel function or a reply.
    if (s_handle == service) {
        dropEverything();
        s_handle = nullptr;
    }
    delete service;
    --s_handles;
    return !enter(__func__, lserror);
}

const char* LSMessageGetPayload(LSMessage* message)
{
    ++s_calls[__func__];
    return message->payload.c_str();
}

const char* LSMessageGetUniqueToken(LSMessage* message) { return message->token.c_str(); }
const char* LSMessageGetMethod(LSMessage* message) { return message->method.c_str(); }
LSMessageToken LSMessageGetResponseToken(LSMessage* reply) { return reply->responseToken; }

bool LSMessageIsSubscription(LSMessage* message)
{
    pbnjson::JValue payload = pbnjson::JDomParser::fromString(message->payload);
    return payload.isObject() && payload["subscribe"].isBoolean() && payload["subscribe"].asBool();
}

void LSMessageRef(LSMessage* message) { ++message->refs; }
void LSMessageUnref(LSMessage* message) { unref(message); }

bool LSMessageRespond(LSMessage* message, const char* reply_payload, LSError* lserror)
{
    s_replies.push_back({message->token, reply_payload ? reply_payload : "(null)"});
    if (s_gone.count(message->sender))
        return fail(__func__, lserror, "the client has gone");
    return !enter(__func__, lserror);
}

bool LSSubscriptionAdd(LSHandle*, const char* key, LSMessage* message, LSError* lserror)
{
    if (s_gone.count(message->sender))
        return fail(__func__, lserror, "the client has been disconnected");
    if (enter(__func__, lserror))
        return false;
    for (const Subscription& s : s_subscriptions) {
        if (s.message == message && s.key == key)
            return true;
    }
    ++message->refs;
    s_subscriptions.push_back({key, message});
    return true;
}

bool LSSubscriptionReply(LSHandle*, const char* key, const char* payload, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;
    for (const Subscription& s : s_subscriptions) {
        if (s.key == key && !s_gone.count(s.message->sender))
            s_replies.push_back({s.message->token, payload});
    }
    return true;
}

static bool call(const char* function, const char* uri, const char* payload, LSFilterFunc callback,
                 void* ctx, LSMessageToken* ret_token, LSError* lserror, bool oneReply)
{
    if (enter(function, lserror))
        return false;
    fakebus::OutCall made = {++s_nextToken, uri, payload ? payload : "", oneReply};
    s_made.push_back(made);
    s_open.push_back({made, callback, ctx});
    if (ret_token)
        *ret_token = made.token;
    return true;
}

bool LSCall(LSHandle*, const char* uri, const char* payload, LSFilterFunc callback, void* user_data,
            LSMessageToken* ret_token, LSError* lserror)
{
    return call(__func__, uri, payload, callback, user_data, ret_token, lserror, false);
}

bool LSCallOneReply(LSHandle*, const char* uri, const char* payload, LSFilterFunc callback, void* ctx,
                    LSMessageToken* ret_token, LSError* lserror)
{
    return call(__func__, uri, payload, callback, ctx, ret_token, lserror, true);
}

bool LSCallCancel(LSHandle*, LSMessageToken token, LSError* lserror)
{
    if (enter(__func__, lserror))
        return false;
    if (!fakebus::endCall(token))
        return fail(__func__, lserror, "no such call");
    return true;
}

} // extern "C"
