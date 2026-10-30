#!/usr/bin/env python3
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# mutation-check.py [--work DIR] <corei7-64 recipe workdir>
#
# Shows the tests can fail: each mutant below puts back one fault the
# service had, or could have, in a copy of the tree, and the ASan+UBSan
# build (host gcc on the recipe's sysroot, as run-sanitizers.sh builds it,
# with cover art and the test method on so that all of the code is there)
# has to fail on it - through an assertion, through ASan/LSan, or through
# libstdc++'s own assertions. The unmutated copy has to pass first, or
# nothing below means anything.

import argparse, os, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--work", default=os.path.expanduser("~/claude-scratch/mediacontroller/mutants"))
ap.add_argument("workdir")
opts = ap.parse_args()
sys.stdout.reconfigure(line_buffering=True)

SRC = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MCS = "src/MediaControlService.cpp"
MSM = "src/MediaSessionManager.cpp"
RR = "src/RequestReceiver.cpp"
TYPES = "include/MediaControlTypes.h"
LSU = "include/Lsutils.h"
LSUC = "src/Lsutils.cpp"
MAIN = "src/main.cpp"
FETCH = "src/coverArt/CoverArtFetcher.cpp"
FOCUS = "src/MediaFocus.cpp"

# name, file, text, replacement, what should catch it (file, text and
# replacement may be parallel lists, for a mutant that needs more than one
# edit)
MUTANTS = [
    # Bluetooth: the subscription leak this fork was started for.
    ("avrcp-subscribed-every-update", MCS,
     "    auto held = obj->avrcpStatusTokens_.find(key);\n    if (held != obj->avrcpStatusTokens_.end()) {",
     "    auto held = obj->avrcpStatusTokens_.find(key);\n    if (false && held != obj->avrcpStatusTokens_.end()) {",
     "Bluetooth.AvrcpStatusIsSubscribedOncePerDevice"),
    ("device-status-subscribed-every-update", MCS,
     "    if (held != obj->btDeviceTokens_.end()) {", "    if (false && held != obj->btDeviceTokens_.end()) {",
     "Bluetooth.DeviceStatusIsSubscribedOncePerAdapter"),
    ("gone-avrcp-not-cancelled", MCS,
     "    obj->cancelCall(itr->second);\n    obj->dropBTDevice(itr->first.substr(prefix.size()), adapterAddress);",
     "    obj->dropBTDevice(itr->first.substr(prefix.size()), adapterAddress);",
     "Bluetooth.AvrcpGoingCancelsItsSubscriptionsAndAReconnectSubscribesAgain"),
    ("other-adapters-devices-dropped", MCS,
     "    if (itr->first.compare(0, prefix.size(), prefix) != 0) {\n      ++itr;\n      continue;\n    }\n    obj->cancelCall(itr->second);\n    obj->dropBTDevice(itr->first.substr(prefix.size()), adapterAddress);",
     "    obj->cancelCall(itr->second);\n    obj->dropBTDevice(itr->first.substr(prefix.size()), adapterAddress);",
     "Bluetooth.OneAdaptersUpdateLeavesTheOthersDevices"),
    ("bluetooth2-down-keeps-everything", MCS,
     "void MediaControlService::dropBluetooth() {\n", "void MediaControlService::dropBluetooth() {\n  return;\n",
     "Bluetooth.Bluetooth2GoingDownDropsEverythingAndComingBackStartsAgain"),
    ("background-session-tells-headset", MCS,
     "  if (mediaId != mediaSessionMgr_.getCurrentActiveSession())\n    return;\n", "",
     "Bluetooth.PlayStatusGoesToEveryConnectedDeviceForTheActiveSessionOnly"),
    ("none-sent-as-error", MCS, '    {"PLAYSTATE_NONE", "stopped"},\n', "",
     "Bluetooth.PlayStatesMapToAvrcp"),
    # Sessions.
    ("session-outlives-its-client", MCS,
     "    obj->mediaSessionMgr_.removeMediaSession(mediaId);\n", "",
     "Sessions.ClientGoingAwayEndsItsSession"),
    ("key-events-to-every-session", MCS,
     "  if (!LSMessageRespond(client->second, responseObj.stringify().c_str(), &lserror)) {",
     "  if (!LSSubscriptionReply(lsHandle_, \"registerMediaSession\", responseObj.stringify().c_str(), &lserror)) {",
     "Sessions.KeyEventGoesToTheLastActivatedSessionOnly"),
    ("taken-id-still-subscribed", MCS,
     "  if (errorCode == MCS_ERROR_NO_ERROR && LSMessageIsSubscription(&message)) {",
     "  if (errorCode == MCS_ERROR_SESSION_ALREADY_REGISTERED) {\n"
     "    CLSError e;\n    LSSubscriptionAdd(lsHandle_, \"registerMediaSession\", &message, &e);\n  }\n"
     "  if (errorCode == MCS_ERROR_NO_ERROR && LSMessageIsSubscription(&message)) {",
     "Sessions.SecondRegistrationOfATakenIdIsNotSubscribed"),
    # upstream's addClient(): libstdc++'s assertions abort on the first
    # activation of any test
    ("front-of-empty-list", RR,
     "  clientListInfo_.remove(mediaId);\n  clientListInfo_.push_front(mediaId);",
     "  if (clientListInfo_.front() == mediaId)\n    return;\n  clientListInfo_.remove(mediaId);\n  clientListInfo_.push_front(mediaId);",
     "!this->empty()"),
    # upstream's merge, for the album: an empty one kept the last track's
    ("metadata-merged", TYPES, "    objMetaData_ = objMetaData;",
     "    objMetaData_ = mediaMetaData(objMetaData.getTitle(), objMetaData.getArtist(),\n"
     "        objMetaData.getDuration(),\n"
     "        objMetaData.getAlbum().empty() ? objMetaData_.getAlbum() : objMetaData.getAlbum(),\n"
     "        objMetaData.getGenre(), objMetaData.getTrackNumber(), objMetaData.getVolume());",
     "Sessions.MetadataIsReplacedWhole"),
    ("stopped-sessions-kept", MCS, "  mediaSessionMgr_.clear();\n", "",
     "Lifecycle.StopsOnceAndCanStartAgain"),
    # Replies.
    ("error-handler-returns-false", MCS,
     "  respond(message, createJsonReplyString(false, errorCode, getErrorTextFromErrorCode(errorCode)));\n  return true;",
     "  respond(message, createJsonReplyString(false, errorCode, getErrorTextFromErrorCode(errorCode)));\n  return false;",
     "did not return true"),
    # upstream's parse(): it replied as well, before the handler did
    ("parse-replies-too", LSUC,
     "                payload, mSchemaText_);\n  return false;",
     "                payload, mSchemaText_);\n"
     "  LSMessageRespond(mMessage_, \"{\\\"returnValue\\\":false}\", nullptr);\n  return false;",
     "must reply exactly once"),
    ("mute-passed-through", MCS,
     '  if (muteStatus == "mute" || muteStatus == "MUTE" || muteStatus == "MUTESTATE_MUTED")\n    return "mute";',
     '  if (muteStatus == "mute" || muteStatus == "MUTE" || muteStatus == "MUTESTATE_MUTED")\n    return muteStatus;',
     "Replies.MuteStatusIsNormalised"),
    ("playback-events-to-everyone", MCS,
     "    if (!LSSubscriptionAdd(lsHandle_, playbackInfoKey(displayId, eventType).c_str(), &message, &lserror)) {",
     "    if (!LSSubscriptionAdd(lsHandle_, playbackInfoKey(displayId, \"\").c_str(), &message, &lserror)) {",
     "Replies.EachSubscriberGetsOnlyWhatItAskedFor"),
    ("playback-state-broadcast", MCS,
     "  respond(message, responsePayload.stringify());\n  return true;\n}\n\nbool MediaControlService::injectMediaKeyEvent",
     "  respond(message, responsePayload.stringify());\n"
     "  { CLSError e; LSSubscriptionReply(lsHandle_, playbackInfoKey(displayId, eventType).c_str(), responsePayload.stringify().c_str(), &e); }\n"
     "  return true;\n}\n\nbool MediaControlService::injectMediaKeyEvent",
     "Replies.FirstPlaybackInfoReplyIsTheStateToTheCallerOnly"),
    # Ownership.
    ("lserror-leaked", LSU, "    if (LSErrorIsSet(this))\n      LSErrorFree(this);\n", "",
     "an LSError was not freed"),
    ("session-call-not-released", MCS,
     "  for (auto& client : sessionClients_)\n    LSMessageUnref(client.second);\n  sessionClients_.clear();",
     "  sessionClients_.clear();",
     "a message was held past the service"),
    ("unregistered-call-not-released", MCS,
     "  LSMessageUnref(client->second);\n  sessionClients_.erase(client);",
     "  sessionClients_.erase(client);",
     "a message was held past the service"),
    # main(): each signal source removed only when it fires, so the other
    # outlives its loop.
    ("signal-source-outlives-loop", MAIN,
     ["  return G_SOURCE_CONTINUE;", "      g_source_remove(term);\n      g_source_remove(intr);\n"],
     ["  return G_SOURCE_REMOVE;", ""],
     "Daemon.ServesUntilSigtermThenLeavesTheBus"),
    # Cover art.
    ("any-scheme-fetched", FETCH, '  if (scheme != "http" && scheme != "https")\n    return false;\n', "",
     "CoverArt.OnlyHttpAndHttpsUrlsWithAHostAndNoCredentials"),
    ("loopback-allowed", FETCH, "  if (a[0] == 0 || a[0] == 10 || a[0] == 127)",
     "  if (a[0] == 0 || a[0] == 10)", "CoverArt.LocalAndPrivateAddressesAreRefused"),
    # both limits: curl enforces CURLOPT_MAXFILESIZE as data arrives too,
    # so either alone still stops the transfer
    ("no-size-limit", FETCH,
     ["  if (length > CoverArtFetcher::MAX_FILE_SIZE - sink->written) {",
      "      curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(MAX_FILE_SIZE)) == CURLE_OK &&\n"],
     ["  if (false) {", ""], "Fetcher.FailuresLeaveNothingBehind"),
    # both: CURLOPT_PROTOCOLS_STR applies to redirects as well, so either
    # alone still keeps them to http and https
    ("redirect-to-any-protocol", FETCH,
     ['      curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_OK &&\n',
      '      curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https") == CURLE_OK &&\n'],
     ["", ""], "Fetcher.RedirectsGoNowhereButHttpAndHttps"),
    ("partial-file-left", FETCH, "  if (!ok) {\n    unlink(name.data());", "  if (!ok) {",
     "Fetcher.FailuresLeaveNothingBehind"),
    ("refused-address-retried", FETCH, " && !transfer.refused", "",
     "Fetcher.LoopbackIsRefusedUnlessAllowed"),
    ("destructor-waits-for-transfer", FETCH,
     "  return static_cast<Transfer *>(userp)->stopping->load() ? 1 : 0;", "  (void)userp;\n  return 0;",
     "Fetcher.EmptiesTheCacheAtStartAndStopsATransferWhenDestroyed"),
    # Media focus: the keys follow what started last, which wins.
    ("focus-flicker-is-a-start", FOCUS,
     "    return !(state.stopped && nowMs - state.stoppedAt < kCarryOnMs);",
     "    return true;",
     "FocusService.TheMusicAppsFlickerTakesNeitherTheKeysNorTheVideo"),
    ("focus-carry-on-forever", FOCUS,
     "nowMs - state.stoppedAt < kCarryOnMs", "nowMs - state.stoppedAt >= 0",
     "Focus.PlayingAgainAfterTheCarryOnTimeIsAStart"),
    ("focus-start-not-activated", MSM,
     "  objRequestRcvr_.addClient(mediaId);\n  std::vector<MediaFocus::Session>",
     "  std::vector<MediaFocus::Session>",
     "FocusService.StartingToPlayTakesTheKeysWithoutActivating"),
    ("focus-others-play-on", MCS,
     '    sendKeyEvent(other, "pause");\n', "",
     "FocusService.TheLastOneStartedWins"),
    ("focus-own-app-paused", FOCUS,
     "session.mediaId == starter.mediaId || session.appId == starter.appId ||",
     "session.mediaId == starter.mediaId ||",
     "FocusService.PausedSessionsAndTheSameAppAreLeftAlone"),
    ("focus-other-display-paused", FOCUS,
     "        session.displayId != starter.displayId || !isPlaying", "        !isPlaying",
     "Focus.AStartPausesOtherAppsPlayingOnItsDisplay"),
    ("focus-paused-sessions-paused", FOCUS,
     "|| !isPlaying(session.playStatus))", ")",
     "FocusService.PausedSessionsAndTheSameAppAreLeftAlone"),
    ("focus-buffering-stops-it", FOCUS,
     "  if (isPlaying(playStatus))\n    return false;\n", "",
     "Focus.SeekingAndBufferingNeitherStartNorStop"),
    ("focus-gone-session-remembered", MSM,
     "  focus_.forget(mediaId);\n", "",
     "FocusService.AnUnregisteredSessionStartsAfresh"),
    # The headset's one button: a toggle, which apps do not know.
    ("playpause-sent-as-it-is", MCS,
     "MediaFocus::resolveKey(keyEvent, playStatus)", "keyEvent",
     "FocusService.TheHeadsetButtonPausesWhatPlaysAndPlaysWhatIsPaused"),
    ("playpause-always-pauses", FOCUS,
     'isPlaying(playStatus) ? "pause" : "play"', '"pause"',
     "Focus.TheHeadsetsPlayPauseIsPauseWhilePlayingAndPlayOtherwise"),
    ("playpause-only-while-really-playing", FOCUS,
     'isPlaying(playStatus) ? "pause" : "play"', 'playStatus == "PLAYSTATE_PLAYING" ? "pause" : "play"',
     "Focus.TheHeadsetsPlayPauseIsPauseWhilePlayingAndPlayOtherwise"),
    ("every-key-toggles", FOCUS,
     '  if (keyEvent != "playPause")\n    return keyEvent;\n', "",
     "Focus.OtherKeysGoAsTheyAre"),
]

S = os.path.join(opts.workdir, "recipe-sysroot")
N = os.path.join(opts.workdir, "recipe-sysroot-native")
env = dict(os.environ, PATH=os.path.join(N, "usr/bin") + os.pathsep + os.environ["PATH"],
           PKG_CONFIG_SYSROOT_DIR=S,
           PKG_CONFIG_LIBDIR=S + "/usr/lib/pkgconfig:" + S + "/usr/share/pkgconfig",
           PKG_CONFIG_PATH=S + "/usr/lib/pkgconfig",
           ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:fast_unwind_on_malloc=0",
           UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1",
           G_SLICE="always-malloc", DEBUGINFOD_URLS="",
           TMPDIR=os.path.join(opts.work, "tmp"))
os.makedirs(env["TMPDIR"], exist_ok=True)

rt = os.path.join(opts.work, "hostrt")
os.makedirs(rt, exist_ok=True)
for lib in ("libasan.so.8", "libubsan.so.1", "libstdc++.so.6", "libgcc_s.so.1"):
    f = subprocess.run(["gcc", "-print-file-name=" + lib], capture_output=True, text=True).stdout.strip()
    if os.path.exists(f):
        dst = os.path.join(rt, lib)
        if os.path.lexists(dst):
            os.remove(dst)
        os.symlink(os.path.realpath(f), dst)


def build_and_run(name, mutate=None):
    tree = os.path.join(opts.work, name, "src")
    build = os.path.join(opts.work, name, "build")
    shutil.rmtree(tree, ignore_errors=True)
    shutil.copytree(SRC, tree, ignore=shutil.ignore_patterns(".git"))
    if mutate:
        paths, olds, news = mutate
        if isinstance(olds, str):
            olds, news = [olds], [news]
        if isinstance(paths, str):
            paths = [paths] * len(olds)
        for path, old, new in zip(paths, olds, news):
            p = os.path.join(tree, path)
            text = open(p).read()
            if text.count(old) != 1:
                return None, "mutation does not apply in %s (%d matches)" % (path, text.count(old))
            open(p, "w").write(text.replace(old, new))
    cfg = subprocess.run(["cmake", "-S", tree, "-B", build, "-G", "Ninja",
                          "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(tree, "tests/hardening/host-sysroot.cmake"),
                          "-DMCS_SYSROOT=" + S, "-DMCS_HOST_CC=gcc", "-DWEBOS_INSTALL_ROOT=/",
                          "-DBUILD_TESTING=ON", "-DMCS_COVER_ART=ON", "-DMCS_TEST_METHODS=ON",
                          "-DMCS_SANITIZE=address,undefined", "-DCMAKE_CXX_FLAGS=-g -O1",
                          "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--dynamic-linker=%s/usr/lib/ld-linux-x86-64.so.2 "
                          "-Wl,--disable-new-dtags -Wl,-rpath,%s:%s/usr/lib" % (S, rt, S)],
                         capture_output=True, text=True, env=env)
    if cfg.returncode:
        return None, "configure failed:\n" + cfg.stdout[-2000:] + cfg.stderr[-2000:]
    b = subprocess.run(["ninja", "-C", build, "mcs-tests"], capture_output=True, text=True, env=env)
    if b.returncode:
        return None, "build failed:\n" + b.stdout[-2000:]
    try:
        r = subprocess.run([os.path.join(build, "tests/mcs-tests")], capture_output=True, text=True,
                           env=env, timeout=300)
    except subprocess.TimeoutExpired:
        return -1, "timeout: the tests did not finish"
    return r.returncode, r.stdout + r.stderr


fail = False
rc, out = build_and_run("unmutated")
if rc != 0:
    print("FAIL the unmutated tree does not pass:\n" + (out or "")[-3000:])
    sys.exit(1)
print("ok   unmutated tree passes")

for name, path, old, new, catcher in MUTANTS:
    rc, out = build_and_run(name, (path, old, new))
    if rc is None:
        print("FAIL %-38s %s" % (name, out))
        fail = True
    elif rc == 0:
        print("FAIL %-38s survived: no test failed" % name)
        fail = True
    elif catcher not in out:
        print("ok   %-38s killed, but not by %s" % (name, catcher))
    else:
        print("ok   %-38s killed by %s" % (name, catcher))
    shutil.rmtree(os.path.join(opts.work, name), ignore_errors=True)

sys.exit(1 if fail else 0)
