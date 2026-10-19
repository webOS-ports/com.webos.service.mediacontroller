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

#include "fixture.h"

#include <set>

namespace {

const char* const kAdapterA = "00:11:22:33:44:55";
const char* const kAdapterB = "66:77:88:99:aa:bb";
const char* const kHeadset = "aa:bb:cc:dd:ee:01";
const char* const kSpeaker = "aa:bb:cc:dd:ee:02";

/* bluetooth2's replies, as it sends them. */
std::string adapters(std::initializer_list<const char*> addresses)
{
    pbnjson::JValue list = pbnjson::Array();
    for (const char* a : addresses) {
        pbnjson::JValue adapter = pbnjson::Object();
        adapter.put("adapterAddress", a);
        adapter.put("name", "hci");
        adapter.put("powered", true);
        list.append(adapter);
    }
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("subscribed", true);
    reply.put("adapters", list);
    return reply.stringify();
}

struct Device {
    const char* address;
    bool avrcp;
};

std::string devices(const char* adapter, std::initializer_list<Device> list)
{
    pbnjson::JValue all = pbnjson::Array();
    for (const Device& d : list) {
        pbnjson::JValue device = pbnjson::Object();
        device.put("address", d.address);
        device.put("adapterAddress", adapter);
        pbnjson::JValue profiles = pbnjson::Array();
        profiles.append("a2dp");
        if (d.avrcp)
            profiles.append("avrcp");
        device.put("connectedProfiles", profiles);
        device.put("paired", true);
        all.append(device);
    }
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("subscribed", true);
    reply.put("adapterAddress", adapter);
    reply.put("devices", all);
    return reply.stringify();
}

std::string avrcp(const char* adapter, const char* device, bool connected)
{
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("subscribed", true);
    reply.put("adapterAddress", adapter);
    reply.put("address", device);
    reply.put("connected", connected);
    reply.put("connecting", false);
    reply.put("playing", false);
    return reply.stringify();
}

std::string key(const char* adapter, const char* device, const char* code, const char* status)
{
    pbnjson::JValue reply = pbnjson::Object();
    reply.put("returnValue", true);
    reply.put("subscribed", true);
    reply.put("adapterAddress", adapter);
    reply.put("address", device);
    reply.put("keyCode", code);
    reply.put("keyStatus", status);
    return reply.stringify();
}

const char* const kError = "{\"returnValue\":false,\"errorCode\":-1,\"errorText\":\"Service gone\"}";

class Bluetooth : public McsTest
{
protected:
    //! bluetooth2 is up.
    void up()
    {
        ASSERT_TRUE(fakebus::deliver(openCall(uri::serverStatus),
            "{\"returnValue\":true,\"connected\":true,\"serviceName\":\"com.webos.service.bluetooth2\"}"));
    }
    void down()
    {
        ASSERT_TRUE(fakebus::deliver(openCall(uri::serverStatus),
            "{\"returnValue\":true,\"connected\":false,\"serviceName\":\"com.webos.service.bluetooth2\"}"));
    }

    //! The open call to u whose payload has address (or adapterAddress) a.
    LSMessageToken callFor(const char* u, const char* address)
    {
        LSMessageToken found = LSMESSAGE_TOKEN_INVALID;
        int n = 0;
        for (const fakebus::OutCall& c : fakebus::openCalls(u)) {
            pbnjson::JValue p = json(c.payload);
            std::string a = p.hasKey("address") ? p["address"].asString() : p["adapterAddress"].asString();
            if (a == address) {
                found = c.token;
                ++n;
            }
        }
        EXPECT_EQ(n, 1) << "calls open to " << u << " for " << address;
        return found;
    }

    //! A headset with AVRCP connected on adapter A, all the way through.
    void headsetConnected(const char* device = kHeadset)
    {
        up();
        fakebus::deliver(openCall(uri::adapterStatus), adapters({kAdapterA}));
        fakebus::deliver(openCall(uri::deviceStatus), devices(kAdapterA, {{device, true}}));
        fakebus::deliver(callFor(uri::avrcpStatus, device), avrcp(kAdapterA, device, true));
    }

    //! Calls made to u since the start, open or not.
    size_t made(const char* u)
    {
        size_t n = 0;
        for (const fakebus::OutCall& c : fakebus::callsMade())
            n += c.uri == u;
        return n;
    }

    std::vector<std::string> notified()
    {
        std::vector<std::string> out;
        for (const fakebus::OutCall& c : fakebus::callsMade()) {
            if (c.uri != uri::notifyPlayStatus)
                continue;
            pbnjson::JValue p = json(c.payload);
            out.push_back(p["address"].asString() + "=" + p["playbackStatus"]["status"].asString());
        }
        return out;
    }
};

TEST_F(Bluetooth, AdapterStatusIsSubscribedOnce)
{
    start();
    up();
    up();
    EXPECT_EQ(fakebus::openCalls(uri::adapterStatus).size(), 1u);
    EXPECT_TRUE(json(fakebus::openCalls(uri::adapterStatus)[0].payload)["subscribe"].asBool());
}

TEST_F(Bluetooth, DeviceStatusIsSubscribedOncePerAdapter)
{
    start();
    up();
    LSMessageToken adapter = openCall(uri::adapterStatus);
    for (int i = 0; i < 5; i++)
        fakebus::deliver(adapter, adapters({kAdapterA, kAdapterB}));
    EXPECT_EQ(fakebus::openCalls(uri::deviceStatus).size(), 2u);
    EXPECT_EQ(made(uri::deviceStatus), 2u) << "subscribed again on every update";
    callFor(uri::deviceStatus, kAdapterA);
    callFor(uri::deviceStatus, kAdapterB);

    // an adapter going cancels its subscription
    fakebus::deliver(adapter, adapters({kAdapterB}));
    EXPECT_EQ(fakebus::openCalls(uri::deviceStatus).size(), 1u);
    callFor(uri::deviceStatus, kAdapterB);
}

// The leak this fork was started for: every device/getStatus update opened
// another avrcp/getStatus subscription for each device with AVRCP.
TEST_F(Bluetooth, AvrcpStatusIsSubscribedOncePerDevice)
{
    start();
    up();
    fakebus::deliver(openCall(uri::adapterStatus), adapters({kAdapterA}));
    LSMessageToken device = openCall(uri::deviceStatus);
    for (int i = 0; i < 50; i++)
        fakebus::deliver(device, devices(kAdapterA, {{kHeadset, true}, {kSpeaker, false}}));
    EXPECT_EQ(fakebus::openCalls(uri::avrcpStatus).size(), 1u);
    EXPECT_EQ(made(uri::avrcpStatus), 1u) << "subscribed again on every update";
    callFor(uri::avrcpStatus, kHeadset);

    fakebus::deliver(device, devices(kAdapterA, {{kHeadset, true}, {kSpeaker, true}}));
    EXPECT_EQ(fakebus::openCalls(uri::avrcpStatus).size(), 2u);
    EXPECT_EQ(made(uri::avrcpStatus), 2u);
}

TEST_F(Bluetooth, AvrcpGoingCancelsItsSubscriptionsAndAReconnectSubscribesAgain)
{
    start();
    headsetConnected();
    EXPECT_EQ(fakebus::openCalls(uri::passThrough).size(), 1u);
    LSMessageToken device = openCall(uri::deviceStatus);

    fakebus::deliver(device, devices(kAdapterA, {{kHeadset, false}}));
    EXPECT_TRUE(fakebus::openCalls(uri::avrcpStatus).empty());
    EXPECT_TRUE(fakebus::openCalls(uri::passThrough).empty()) << "the gone device's keys are still subscribed";

    fakebus::deliver(device, devices(kAdapterA, {{kHeadset, true}}));
    EXPECT_EQ(fakebus::openCalls(uri::avrcpStatus).size(), 1u);
}

TEST_F(Bluetooth, EmptyDeviceListIsEveryDeviceGone)
{
    start();
    headsetConnected();
    fakebus::deliver(openCall(uri::deviceStatus), devices(kAdapterA, {}));
    EXPECT_TRUE(fakebus::openCalls(uri::avrcpStatus).empty());
    EXPECT_TRUE(fakebus::openCalls(uri::passThrough).empty());
}

TEST_F(Bluetooth, OneAdaptersUpdateLeavesTheOthersDevices)
{
    start();
    up();
    fakebus::deliver(openCall(uri::adapterStatus), adapters({kAdapterA, kAdapterB}));
    fakebus::deliver(callFor(uri::deviceStatus, kAdapterA), devices(kAdapterA, {{kHeadset, true}}));
    fakebus::deliver(callFor(uri::deviceStatus, kAdapterB), devices(kAdapterB, {{kSpeaker, true}}));
    fakebus::deliver(callFor(uri::deviceStatus, kAdapterB), devices(kAdapterB, {{kSpeaker, true}}));
    EXPECT_EQ(fakebus::openCalls(uri::avrcpStatus).size(), 2u);
}

TEST_F(Bluetooth, PassThroughIsSubscribedOncePerConnectedDevice)
{
    start();
    headsetConnected();
    LSMessageToken status = openCall(uri::avrcpStatus);
    for (int i = 0; i < 5; i++)
        fakebus::deliver(status, avrcp(kAdapterA, kHeadset, true));
    EXPECT_EQ(fakebus::openCalls(uri::passThrough).size(), 1u);

    fakebus::deliver(status, avrcp(kAdapterA, kHeadset, false));
    EXPECT_TRUE(fakebus::openCalls(uri::passThrough).empty());
}

TEST_F(Bluetooth, AvrcpStatusErrorDropsTheDevice)
{
    start();
    headsetConnected();
    LSMessageToken status = openCall(uri::avrcpStatus);
    fakebus::deliver(status, kError);
    fakebus::endCall(status);
    EXPECT_TRUE(fakebus::openCalls(uri::passThrough).empty());
    // and the next device update subscribes it afresh
    fakebus::deliver(openCall(uri::deviceStatus), devices(kAdapterA, {{kHeadset, true}}));
    EXPECT_EQ(fakebus::openCalls(uri::avrcpStatus).size(), 1u);
}

TEST_F(Bluetooth, Bluetooth2GoingDownDropsEverythingAndComingBackStartsAgain)
{
    start();
    headsetConnected();
    down();
    EXPECT_TRUE(fakebus::openCalls(uri::adapterStatus).empty());
    EXPECT_TRUE(fakebus::openCalls(uri::deviceStatus).empty());
    EXPECT_TRUE(fakebus::openCalls(uri::avrcpStatus).empty());
    EXPECT_TRUE(fakebus::openCalls(uri::passThrough).empty());

    session("m1", "com.one");
    call("setMediaPlayStatus", "{\"mediaId\":\"m1\",\"playStatus\":\"PLAYSTATE_PLAYING\"}");
    EXPECT_TRUE(notified().empty()) << "play status sent to a device bluetooth2 no longer has";

    headsetConnected();
    EXPECT_EQ(fakebus::openCalls(uri::passThrough).size(), 1u);
}

TEST_F(Bluetooth, BrokenRepliesAreIgnored)
{
    start();
    up();
    LSMessageToken adapter = openCall(uri::adapterStatus);
    for (const char* reply : {"", "garbage", "[]", "{\"returnValue\":true}",
                              "{\"returnValue\":true,\"adapters\":7}",
                              "{\"returnValue\":true,\"adapters\":[{\"adapterAddress\":3}]}"})
        fakebus::deliver(adapter, reply);
    EXPECT_TRUE(fakebus::openCalls(uri::deviceStatus).empty());
    fakebus::deliver(adapter, adapters({kAdapterA}));
    LSMessageToken device = openCall(uri::deviceStatus);
    for (const char* reply : {"{\"returnValue\":true,\"devices\":[{\"connectedProfiles\":\"avrcp\"}]}",
                              "{\"returnValue\":true,\"devices\":[{\"address\":\"x\",\"connectedProfiles\":[5]}]}"})
        fakebus::deliver(device, reply);
    EXPECT_TRUE(fakebus::openCalls(uri::avrcpStatus).empty());
}

TEST_F(Bluetooth, ReleasedKeyGoesToTheActiveSession)
{
    start();
    std::string one = session("m1", "com.one");
    headsetConnected();
    LSMessageToken keys = openCall(uri::passThrough);
    fakebus::deliver(keys, key(kAdapterA, kHeadset, "pause", "pressed"));
    fakebus::deliver(keys, key(kAdapterA, kHeadset, "pause", "released"));
    std::vector<std::string> sent;
    for (const std::string& r : fakebus::repliesTo(one)) {
        if (json(r).hasKey("keyEvent"))
            sent.push_back(json(r)["keyEvent"].asString());
    }
    EXPECT_EQ(sent, std::vector<std::string>{"pause"});

    // a device it does not know is not listened to
    fakebus::deliver(keys, key(kAdapterA, kSpeaker, "play", "released"));
    EXPECT_EQ(fakebus::repliesTo(one).size(), 2u);
}

TEST_F(Bluetooth, PlayStatusGoesToEveryConnectedDeviceForTheActiveSessionOnly)
{
    start();
    session("m1", "com.one");
    session("m2", "com.two");  // activated last: the one the keys go to
    up();
    fakebus::deliver(openCall(uri::adapterStatus), adapters({kAdapterA}));
    fakebus::deliver(openCall(uri::deviceStatus), devices(kAdapterA, {{kHeadset, true}, {kSpeaker, true}}));
    fakebus::deliver(callFor(uri::avrcpStatus, kHeadset), avrcp(kAdapterA, kHeadset, true));
    fakebus::deliver(callFor(uri::avrcpStatus, kSpeaker), avrcp(kAdapterA, kSpeaker, true));

    call("setMediaPlayStatus", "{\"mediaId\":\"m1\",\"playStatus\":\"PLAYSTATE_PAUSED\"}");
    EXPECT_TRUE(notified().empty()) << "a background session spoke for the phone";

    call("setMediaPlayStatus", "{\"mediaId\":\"m2\",\"playStatus\":\"PLAYSTATE_PLAYING\"}");
    std::vector<std::string> sent = notified();
    std::set<std::string> got(sent.begin(), sent.end());
    EXPECT_EQ(got, (std::set<std::string>{std::string(kHeadset) + "=playing", std::string(kSpeaker) + "=playing"}));

    // one device going leaves the other told
    fakebus::deliver(callFor(uri::avrcpStatus, kSpeaker), avrcp(kAdapterA, kSpeaker, false));
    call("setMediaPlayStatus", "{\"mediaId\":\"m2\",\"playStatus\":\"PLAYSTATE_PAUSED\"}");
    EXPECT_EQ(notified().back(), std::string(kHeadset) + "=paused");
    EXPECT_EQ(notified().size(), 3u);
}

TEST_F(Bluetooth, PlayStatesMapToAvrcp)
{
    start();
    session("m1", "com.one");
    headsetConnected();
    const std::pair<const char*, const char*> map[] = {
        {"PLAYSTATE_NONE", "stopped"}, {"PLAYSTATE_STOPPED", "stopped"}, {"PLAYSTATE_PAUSED", "paused"},
        {"PLAYSTATE_PLAYING", "playing"}, {"PLAYSTATE_FAST_FORWARDING", "fwd_seek"},
        {"PLAYSTATE_REWINDING", "rev_seek"}, {"PLAYSTATE_ERROR", "error"}};
    for (const auto& m : map) {
        call("setMediaPlayStatus", std::string("{\"mediaId\":\"m1\",\"playStatus\":\"") + m.first + "\"}");
        EXPECT_EQ(notified().back(), std::string(kHeadset) + "=" + m.second) << m.first;
    }
    size_t before = notified().size();
    call("setMediaPlayStatus", "{\"mediaId\":\"m1\",\"playStatus\":\"PLAYSTATE_BUFFERING\"}");
    EXPECT_EQ(notified().size(), before) << "buffering was sent";
}

} // namespace
