// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_TAB_VISIT_SETTLED_VISIT_CLEAR_HOOK_H_
#define ROAMEX_BROWSER_TAB_VISIT_SETTLED_VISIT_CLEAR_HOOK_H_

#include "base/functional/callback.h"
#include "base/time/time.h"

namespace content {
class BrowserContext;
}

namespace roamex::tab_visit {

// The cross-GN-layer seam for Clear-Browsing-Data (roam-28 / I-4.8).
//
// The BrowsingDataRemover delegate lives in //chrome/browser/browsing_data — a
// layer BELOW //chrome/browser/ui/tabs, where the per-profile journal factory +
// traversal coordinator are compiled. So the ui layer REGISTERS a hook here
// (from the journal factory's constructor, which the profiles registration
// builds at startup) and the delegate INVOKES it, avoiding a
// browsing_data -> ui/tabs GN cycle (mirrors roam-27's injected ReopenFn). The
// registry lives in the low //roamex/browser/tab_visit source_set that the
// delegate can depend on cleanly; `BrowserContext` is only passed through, so a
// forward declaration suffices (no //content dependency added here).
//
// `fn(context, begin, end, done)` clears the context's journal visits in
// [begin, end) + orphaned closed tab_state and purges the in-memory reopenable
// cache, then runs `done`.
using ClearJournalHook =
    base::RepeatingCallback<void(content::BrowserContext* context,
                                 base::Time begin,
                                 base::Time end,
                                 base::OnceClosure done)>;

// Sets the process-global hook (idempotent; called once at startup by the
// journal factory constructor). No-op registration is fine.
void SetClearJournalHook(ClearJournalHook hook);

// Runs the registered hook (feature enabled + ui layer present); otherwise runs
// `done` immediately so the caller's completion token always fires exactly
// once. Called by the BrowsingDataRemover delegate on the UI thread.
void RunClearJournalHook(content::BrowserContext* context,
                         base::Time begin,
                         base::Time end,
                         base::OnceClosure done);

}  // namespace roamex::tab_visit

#endif  // ROAMEX_BROWSER_TAB_VISIT_SETTLED_VISIT_CLEAR_HOOK_H_
