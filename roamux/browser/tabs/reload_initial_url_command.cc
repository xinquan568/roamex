// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/tabs/reload_initial_url_command.h"

#include "base/feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "roamux/browser/tabs/tab_initial_url_helper.h"
#include "roamux/common/roamux_features.h"
#include "ui/base/page_transition_types.h"

namespace roamux::tabs {

namespace {

TabInitialUrlHelper* ActiveTabHelper(const Browser* browser) {
  if (!browser) {
    return nullptr;
  }
  content::WebContents* active =
      browser->tab_strip_model()->GetActiveWebContents();
  return active ? TabInitialUrlHelper::FromWebContents(active) : nullptr;
}

}  // namespace

bool CanReloadInitialUrl(const Browser* browser) {
  if (!base::FeatureList::IsEnabled(features::kInitialUrl)) {
    return false;
  }
  TabInitialUrlHelper* helper = ActiveTabHelper(browser);
  return helper && helper->has_initial_url() &&
         helper->initial_url().is_valid();
}

void ReloadInitialUrl(Browser* browser) {
  if (!CanReloadInitialUrl(browser)) {
    return;
  }
  content::WebContents* active =
      browser->tab_strip_model()->GetActiveWebContents();
  content::NavigationController::LoadURLParams params(
      ActiveTabHelper(browser)->initial_url());
  params.transition_type = ui::PageTransitionFromInt(
      ui::PAGE_TRANSITION_TYPED | ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
  active->GetController().LoadURLWithParams(params);
}

}  // namespace roamux::tabs
