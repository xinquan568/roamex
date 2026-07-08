// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tabs/tab_initial_url_helper.h"

#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "components/sessions/content/session_tab_helper.h"
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

// static
std::string TabInitialUrlHelper::EncodeExtraData(const GURL& url, bool locked) {
  return (locked ? "1" : "0") + url.spec();
}

// static
bool TabInitialUrlHelper::DecodeExtraData(const std::string& value,
                                          GURL* url,
                                          bool* locked) {
  if (value.size() < 2 || (value[0] != '0' && value[0] != '1')) {
    return false;
  }
  GURL parsed(value.substr(1));
  if (!parsed.is_valid()) {
    return false;
  }
  *url = std::move(parsed);
  *locked = value[0] == '1';
  return true;
}

// static
void TabInitialUrlHelper::PopulateExtraData(
    ::tabs::TabInterface* tab,
    std::map<std::string, std::string>* extra_data) {
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  TabInitialUrlHelper* helper = contents ? FromWebContents(contents) : nullptr;
  if (helper && helper->has_initial_url()) {
    (*extra_data)[kExtraDataKey] =
        EncodeExtraData(helper->initial_url_, helper->user_locked_);
  }
}

// static
void TabInitialUrlHelper::SetPendingRestoredInitialUrl(
    content::WebContents* web_contents,
    const std::map<std::string, std::string>& extra_data) {
  auto it = extra_data.find(kExtraDataKey);
  if (it == extra_data.end()) {
    return;
  }
  GURL url;
  bool locked = false;
  if (!DecodeExtraData(it->second, &url, &locked)) {
    return;  // Malformed persisted data: restore proceeds uncaptured.
  }
  MaybeCreateForWebContents(web_contents);
  TabInitialUrlHelper* helper = FromWebContents(web_contents);
  if (helper) {
    // Pre-arm BEFORE the restore's first navigation; a locked value stays
    // edit-locked across restore (§4.7).
    helper->SetRestoredInitialUrl(url, locked);
  }
}

// static
void TabInitialUrlHelper::OnTabDuplicated(content::WebContents* source_contents,
                                          content::WebContents* new_contents) {
  TabInitialUrlHelper* source =
      source_contents ? FromWebContents(source_contents) : nullptr;
  if (!source || !source->has_initial_url()) {
    return;
  }
  MaybeCreateForWebContents(new_contents);
  TabInitialUrlHelper* clone = FromWebContents(new_contents);
  if (clone) {
    // §4.2: a duplicated tab INHERITS value+lock (the uid re-mints).
    clone->initial_url_ = source->initial_url_;
    clone->captured_ = source->captured_;
    clone->user_locked_ = source->user_locked_;
    clone->PersistToSession();
  }
}

TabInitialUrlHelper::TabInitialUrlHelper(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<TabInitialUrlHelper>(*web_contents) {}

TabInitialUrlHelper::~TabInitialUrlHelper() = default;

void TabInitialUrlHelper::SetUserInitialUrl(const GURL& url) {
  initial_url_ = url;
  captured_ = true;
  user_locked_ = true;
  PersistToSession();
}

void TabInitialUrlHelper::SetRestoredInitialUrl(const GURL& url) {
  SetRestoredInitialUrl(url, /*locked=*/false);
}

void TabInitialUrlHelper::SetRestoredInitialUrl(const GURL& url, bool locked) {
  initial_url_ = url;
  captured_ = true;
  user_locked_ = locked;
  // Defer the session write until the restored tab is attached (below).
  pending_session_write_ = true;
}

void TabInitialUrlHelper::PersistToSession() {
  // The uid Stamp() pattern (roam-10): OTR/absent session service degrade to
  // silently-unpersisted (§4.7 / D5).
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  SessionService* session_service =
      SessionServiceFactory::GetForProfile(profile);
  if (!session_service) {
    return;
  }
  session_service->AddTabExtraData(
      sessions::SessionTabHelper::IdForWindowContainingTab(web_contents()),
      sessions::SessionTabHelper::IdForTab(web_contents()), kExtraDataKey,
      EncodeExtraData(initial_url_, user_locked_));
}

void TabInitialUrlHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  // State gate.
  if (captured_ || user_locked_) {
    // A restored value re-persists on its first post-attach navigation, when
    // the session window is finally tracked (Step-8 finding 1).
    if (pending_session_write_ && navigation_handle->IsInPrimaryMainFrame() &&
        navigation_handle->HasCommitted()) {
      pending_session_write_ = false;
      PersistToSession();
    }
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
  PersistToSession();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabInitialUrlHelper);

}  // namespace roamex::tabs
