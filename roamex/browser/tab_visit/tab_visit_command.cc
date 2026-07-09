// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tab_visit/tab_visit_command.h"

#include "base/feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "roamex/browser/tab_visit/tab_visit_traversal_coordinator.h"
#include "roamex/browser/tab_visit/tab_visit_traversal_coordinator_factory.h"
#include "roamex/common/roamex_features.h"

namespace roamex::tab_visit {

namespace {
TabVisitTraversalCoordinator* CoordinatorFor(Browser* browser) {
  if (!browser || !base::FeatureList::IsEnabled(features::kTabVisitNav)) {
    return nullptr;
  }
  return TabVisitTraversalCoordinatorFactory::GetForProfile(browser->profile());
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

}  // namespace roamex::tab_visit
