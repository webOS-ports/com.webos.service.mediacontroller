// Not built. Every rule in this directory must match something here;
// static-analysis.sh checks that, so a rule that stops matching (a
// Coccinelle upgrade, a typo) is noticed instead of passing quietly.
#include <luna-service2/lunaservice.h>

void call(LSHandle *sh) {
  LSError lserror;
  LSErrorInit(&lserror);
  LSCall(sh, "luna://x/y", "{}", nullptr, nullptr, nullptr, &lserror);
}
