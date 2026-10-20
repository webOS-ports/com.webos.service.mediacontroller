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

#include "MediaControlService.h"

#include <glib-unix.h>
#include <signal.h>

static gboolean quitLoop(gpointer loop) {
  g_main_loop_quit(static_cast<GMainLoop *>(loop));
  // kept until the loop is over, so both are removed in one place
  return G_SOURCE_CONTINUE;
}

/* Serves until SIGTERM or SIGINT, then leaves the bus: 0, or 1 when it
 * could not start. */
int runMediaControlService() {
  GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
  int status = 1;
  {
    MediaControlService service;
    if (service.start(loop)) {
      guint term = g_unix_signal_add(SIGTERM, quitLoop, loop);
      guint intr = g_unix_signal_add(SIGINT, quitLoop, loop);
      g_main_loop_run(loop);
      // neither may outlive the loop it would quit
      g_source_remove(term);
      g_source_remove(intr);
      status = 0;
    }
  }
  g_main_loop_unref(loop);
  return status;
}

#ifndef MCS_NO_MAIN
int main() {
  return runMediaControlService();
}
#endif
