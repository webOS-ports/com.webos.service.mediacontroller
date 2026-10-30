mediacontroller test and hardening harness
==========================================

Three layers: unit tests on the build host, the sanitizers and valgrind
over them, and static analysis with a warning gate over the sources.

Every script takes a Yocto recipe workdir (`tmp/work/<arch>/<recipe>/<ver>`)
for the toolchain and the sysroot. The com.webos.service.mediacontroller
recipe's own will do, once googletest is in its sysroot (copy it from
`tmp/sysroots-components/<arch>/googletest`). The corei7-64 one is the one
the host can run.

Unit tests (`unit/`, `fakes/`)
------------------------------

gtest, built from the service's own objects (the `mcs-core` object
library, and `main.cpp` without `main()`), linked with an in-process
luna-service2 (`fakes/fakebus.cpp`) instead of the real library. The fake
calls the service's methods as a client would, records every reply and
which call it went to, holds subscriptions under their keys, plays a
client cancelling its call or going away (and a reply to it failing
after that), records the calls the service makes and delivers bluetooth2's
replies to them, fails any LS call on request, and counts handles, LSErrors
and messages. Messages are reference counted as the real ones are and freed
at zero, so a message held past its last reference is a use after free the
sanitizers see.

Every test's TearDown checks that no handle, LSError or message is left,
and fails on any critical or warning from GLib itself. The tests are built
with `_GLIBCXX_ASSERTIONS`, so libstdc++'s own checks (front() of an empty
list and the like) abort instead of passing unseen.

| File | What it pins |
|---|---|
| `test_lifecycle.cpp` | the name, methods and cancel function registered; the API files (both ACGs) listing exactly what the build registers, and the permissions covering what it calls; every start step failing cleanly; stop() and the destructor releasing every held call and every session; `runMediaControlService()`: serves until SIGTERM or SIGINT, exits 0 and leaves no signal source behind, exits 1 when it cannot start |
| `test_sessions.cpp` | a session ending when its client unsubscribes or goes away, and only its own; empty ids refused; a second registration of a taken mediaId not subscribed; key events (injectMediaKeyEvent) reaching the last activated session and no other; no active session an error; activating twice and deactivating what is not active harmless; metadata replaced whole |
| `test_focus.cpp` | the media focus rules (MediaFocus): a session starting to play takes the keys without activating, and pauses the other apps' sessions playing on its display, not its own app's nor paused ones; PAUSED and PLAYING within the carry-on time (the Music app's flicker) not a start, so it neither takes the keys nor pauses the video; seeking and buffering neither start nor stop; a session gone forgotten; a headset's "playPause" sent as pause to a session playing, seeking or buffering and as play to any other, other keys as they are |
| `test_replies.cpp` | every registered method replying exactly once and returning true to any payload (luna-service2 sends a second, "not handled", reply otherwise); error codes and texts; mute states normalised to "mute"/"unmute"; each receiveMediaPlaybackInfo subscriber told only the events it asked for, its first reply the state and to it alone, every event carrying the same fields |
| `test_bluetooth.cpp` | adapter/getStatus once, device/getStatus once per adapter, avrcp/getStatus once per device with AVRCP (the subscription leak), however many updates come; each cancelled when what it watches goes, and only for its adapter; an empty device list all devices gone; everything dropped when bluetooth2 goes down and started again when it is back; malformed replies ignored; a released key to the active session; play status to every connected device, for the active session only, mapped to AVRCP's states |
| `test_coverart.cpp` | with `MCS_COVER_ART` only: which URLs are fetched; file names the URL's hash; secrets kept out of the logs; local, private and reserved addresses refused (IPv4, IPv6, mapped, NAT64); the method's argument checks; against a loopback HTTP server: a fetch landing under its name and coming from the cache the second time, HTTP errors, oversized files and redirects to file:// leaving nothing behind, a refused address not retried, the queue bound, the cache emptied at start, and a transfer in progress aborted when the fetcher goes |

Build and run on the host:

    cmake -DBUILD_TESTING=ON [-DMCS_COVER_ART=ON -DMCS_TEST_METHODS=ON] ...
    ninja mcs-tests
    <sysroot>/usr/lib/ld-linux-x86-64.so.2 --library-path <sysroot>/usr/lib \
        tests/mcs-tests

The tests read the API files from the source tree; point them elsewhere with
`MCS_SOURCE_DIR`. The fetcher tests make a cache under `$TMPDIR` and remove
it.

Hardening (`hardening/`)
------------------------

- `run-sanitizers.sh <corei7-64 workdir> [work dir]`: the tests under
  ASan+UBSan and TSan, each built with gcc and with clang, with cover art
  off and on (the worker thread exists only with it on), and under valgrind
  memcheck on the image toolchain's own build. clang also gets its integer
  and nullability checks, without unsigned wrap-around, which libstdc++
  relies on. The image toolchain ships no sanitizer runtimes, so the host's
  gcc and clang build against the sysroot (`host-sysroot.cmake`).
- `static-analysis.sh <corei7-64 workdir> [work dir]`: gcc `-fanalyzer` at
  -O0 and -O2, the clang analyzer with the alpha checkers, clang-tidy,
  cppcheck (exhaustive, inconclusive), flawfinder and Coccinelle (`cocci/`),
  over all of `src/`, cover art included. It fails on any finding not in
  `baseline.txt`, where each one looked at and left has its reason.
  cppcheck's style notes are listed but do not gate. sparse is not run: it
  parses C only.
- `build-all.py <workdir>...`: the warning gate. The service and the tests
  for each architecture given, as shipped and with everything on, with the
  image's gcc and with the host's clang, at `-Wall -Wextra`; a warning in
  the tree fails it. Run it with the armv7, aarch64 and corei7-64 workdirs.
- `mutation-check.py <corei7-64 workdir>`: shows the tests can fail. Each
  mutant puts back one fault the service had or could have - the Bluetooth
  subscription leak and its relatives, sessions outliving their clients,
  key events to every session, double replies, the global playback-event
  flags, unnormalised mute states, a leaked LSError or message, a signal
  source outliving its loop, and the cover art fetcher's limits - and must
  be caught by a test, by ASan, or by libstdc++'s assertions.

The Coccinelle rule flags a bare LSError (CLSError frees itself) and is
checked against `cocci/selftest.cpp`, so a rule that silently matches
nothing fails the gate instead of passing it. A rule for a bus method that
returns false would be worth having, but Coccinelle 1.3 does not match
class-qualified function definitions; the replies test checks it at run
time instead.
