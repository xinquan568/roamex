// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tabs/tab_initial_url_helper.h"

#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "roamex/common/roamex_features.h"
#include "ui/base/page_transition_types.h"

namespace roamex::tabs {

namespace {

bool IsIgnorableStart(const GURL& url) {
  return url.is_empty() || url.IsAboutBlank() ||
         url.spec() == "chrome://newtab/" ||
         url.spec() == "chrome://new-tab-page/";
}

}  // namespace

// static
void TabInitialUrlHelper::MaybeCreateForTab(::tabs::TabInterface& tab) {
  MaybeCreateForWebContents(tab.GetContents());
}

// static
void TabInitialUrlHelper::MaybeCreateForWebContents(
    content::WebContents* web_contents) {
  if (!base::FeatureList::IsEnabled(features::kInitialUrl) || !web_contents ||
      FromWebContents(web_contents)) {
    return;
  }
  web_contents->SetUserData(
      UserDataKey(), base::WrapUnique(new TabInitialUrlHelper(web_contents)));
}

// static
void TabInitialUrlHelper::WillDiscardContents(
    ::tabs::TabInterface* tab,
    content::WebContents* old_contents,
    content::WebContents* new_contents) {
  TabInitialUrlHelper* old_helper = FromWebContents(old_contents);
  if (!old_helper || !new_contents) {
    return;
  }
  MaybeCreateForWebContents(new_contents);
  TabInitialUrlHelper* new_helper = FromWebContents(new_contents);
  if (!new_helper) {
    return;
  }
  // A discard is the same live tab: carry value + state verbatim.
  new_helper->initial_url_ = old_helper->initial_url_;
  new_helper->captured_ = old_helper->captured_;
  new_helper->user_locked_ = old_helper->user_locked_;
}

TabInitialUrlHelper::TabInitialUrlHelper(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<TabInitialUrlHelper>(*web_contents) {}

TabInitialUrlHelper::~TabInitialUrlHelper() = default;

void TabInitialUrlHelper::SetUserInitialUrl(const GURL& url) {
  initial_url_ = url;
  captured_ = true;
  user_locked_ = true;
}

void TabInitialUrlHelper::SetRestoredInitialUrl(const GURL& url) {
  initial_url_ = url;
  captured_ = true;
  user_locked_ = false;
}

void TabInitialUrlHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  // State gate.
  if (captured_ || user_locked_) {
    return;
  }
  // Frame/commit gates.
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted() ||
      navigation_handle->IsSameDocument()) {
    return;
  }
  // Activation exclusions (explicit; plan D1): activations re-show existing
  // pages and are never "the navigation the user opened this tab for".
  if (navigation_handle->IsServedFromBackForwardCache() ||
      navigation_handle->IsPrerenderedPageActivation()) {
    return;
  }
  // User-intent rule: no client-redirect first navigation; renderer-initiated
  // is accepted only as the tab's first-ever committed navigation (a tab
  // opened via link). SERVER_REDIRECT qualifiers are accepted by design (the
  // SSO case) — the captured value is the chain head.
  const ui::PageTransition transition = navigation_handle->GetPageTransition();
  if (transition & ui::PAGE_TRANSITION_CLIENT_REDIRECT) {
    return;
  }
  if (navigation_handle->IsRendererInitiated()) {
    // "First-ever committed navigation" == this commit is the only one.
    content::NavigationController& controller = web_contents()->GetController();
    if (controller.GetEntryCount() > 1 ||
        (controller.GetLastCommittedEntry() &&
         controller.GetLastCommittedEntry()->IsInitialEntry())) {
      return;
    }
    // Exactly one committed non-initial entry (this navigation): accept.
  }
  // Value gate + capture: the redirect-chain head.
  const GURL& committed = navigation_handle->GetURL();
  if (IsIgnorableStart(committed)) {
    return;
  }
  const GURL head = navigation_handle->GetRedirectChain().empty()
                        ? committed
                        : navigation_handle->GetRedirectChain().front();
  if (IsIgnorableStart(head)) {
    return;
  }
  initial_url_ = head;
  captured_ = true;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabInitialUrlHelper);

}  // namespace roamex::tabs
