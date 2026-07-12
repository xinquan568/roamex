// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/tab_visit/tab_visit_command.h"

#include <map>
#include <memory>
#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_live_tab_context.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/core/live_tab_context.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "roamux/browser/tab_visit/tab_visit_traversal_coordinator.h"
#include "roamux/browser/tab_visit/tab_visit_traversal_coordinator_factory.h"
#include "roamux/browser/tabs/tab_uid_tab_helper.h"
#include "roamux/common/roamux_features.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace roamux::tab_visit {

namespace {
TabVisitTraversalCoordinator* CoordinatorFor(Browser* browser) {
  if (!browser || !base::FeatureList::IsEnabled(features::kTabVisitNav)) {
    return nullptr;
  }
  TabVisitTraversalCoordinator* coordinator =
      TabVisitTraversalCoordinatorFactory::GetForProfile(browser->profile());
  // roam-27: lazily inject the reopen action the first time the ui layer
  // reaches the coordinator (it cannot reach TabRestoreService itself without a
  // GN cycle). Idempotent — has_reopen_fn() guards re-binding.
  if (coordinator && !coordinator->has_reopen_fn()) {
    coordinator->SetReopenFn(base::BindRepeating(&ReopenClosedTabByUid));
  }
  return coordinator;
}
}  // namespace

bool CanTabVisitBack(Browser* browser) {
  TabVisitTraversalCoordinator* coordinator = CoordinatorFor(browser);
  return coordinator && coordinator->CanGoBack();
}

bool CanTabVisitForward(Browser* browser) {
  TabVisitTraversalCoordinator* coordinator = CoordinatorFor(browser);
  return coordinator && coordinator->CanGoForward();
}

void TabVisitBack(Browser* browser) {
  if (TabVisitTraversalCoordinator* coordinator = CoordinatorFor(browser)) {
    coordinator->StepBack();
  }
}

void TabVisitForward(Browser* browser) {
  if (TabVisitTraversalCoordinator* coordinator = CoordinatorFor(browser)) {
    coordinator->StepForward();
  }
}

bool ReopenClosedTabByUid(BrowserWindowInterface* target,
                          const std::string& uid,
                          const std::string& last_known_url) {
  if (!target || uid.empty()) {
    return false;
  }
  Profile* profile = target->GetProfile();
  // BrowserLiveTabContext is-a sessions::LiveTabContext (the upcast is why this
  // shim lives in //chrome/browser/ui).
  sessions::LiveTabContext* context = target->GetFeatures().live_tab_context();
  if (!context) {
    return false;
  }

  // 1. EXACT reopen — find the closed entry whose uid extra_data matches and
  //    restore precisely that tab (uid disambiguates same-URL siblings). Use
  //    WindowOpenDisposition::UNKNOWN so the entry's saved placement is
  //    honored; the coordinator re-resolves + activates the reopened tab
  //    afterwards.
  if (sessions::TabRestoreService* svc =
          TabRestoreServiceFactory::GetForProfile(profile)) {
    for (const auto& entry : svc->entries()) {
      if (entry->type == sessions::tab_restore::TAB) {
        auto it = entry->extra_data.find(tabs::TabUidTabHelper::kExtraDataKey);
        if (it != entry->extra_data.end() && it->second == uid) {
          svc->RestoreEntryById(context, entry->id,
                                WindowOpenDisposition::UNKNOWN);
          return true;
        }
        continue;
      }
      if (entry->type == sessions::tab_restore::WINDOW) {
        // Restore ONLY the matching nested tab out of a closed WINDOW entry.
        const auto* window =
            static_cast<const sessions::tab_restore::Window*>(entry.get());
        for (const auto& tab : window->tabs) {
          auto it = tab->extra_data.find(tabs::TabUidTabHelper::kExtraDataKey);
          if (it != tab->extra_data.end() && it->second == uid) {
            svc->RestoreEntryById(context, tab->id,
                                  WindowOpenDisposition::UNKNOWN);
            return true;
          }
        }
      }
    }
  }

  // 2. EVICTION fallback — no matching restore entry (evicted from the closed
  //    list). Open a fresh tab at last_known_url and re-stamp the OLD uid so
  //    the reopened tab adopts it at attach (roam-10 AdoptOrRestamp: valid UUID
  //    + not live ⇒ adopt) and rebinds to its journal entry (§6.6).
  GURL url(last_known_url);
  if (!url.is_valid()) {
    return false;
  }
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(content::WebContents::CreateParams(profile));
  content::WebContents* raw_contents = contents.get();
  // Stash BEFORE attach so the helper adopts the uid when the tab is inserted.
  tabs::TabUidTabHelper::SetPendingRestoredUid(
      raw_contents, {{tabs::TabUidTabHelper::kExtraDataKey, uid}});
  target->GetTabStripModel()->AppendWebContents(std::move(contents),
                                                /*foreground=*/true);
  raw_contents->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(url));
  return true;
}

}  // namespace roamux::tab_visit
