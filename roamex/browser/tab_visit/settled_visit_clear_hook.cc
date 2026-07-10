// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tab_visit/settled_visit_clear_hook.h"

#include <utility>

#include "base/no_destructor.h"

namespace roamex::tab_visit {

namespace {

// Process-global, UI-thread-only. Unset until the ui layer registers an impl at
// startup; a null hook means the feature/ui is absent, so callers run `done`.
ClearJournalHook& GlobalHook() {
  static base::NoDestructor<ClearJournalHook> hook;
  return *hook;
}

}  // namespace

void SetClearJournalHook(ClearJournalHook hook) {
  GlobalHook() = std::move(hook);
}

void RunClearJournalHook(content::BrowserContext* context,
                         base::Time begin,
                         base::Time end,
                         base::OnceClosure done) {
  if (GlobalHook()) {
    GlobalHook().Run(context, begin, end, std::move(done));
  } else {
    std::move(done).Run();
  }
}

}  // namespace roamex::tab_visit
