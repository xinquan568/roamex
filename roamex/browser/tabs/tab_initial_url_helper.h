// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_TABS_TAB_INITIAL_URL_HELPER_H_
#define ROAMEX_BROWSER_TABS_TAB_INITIAL_URL_HELPER_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

namespace tabs {
class TabInterface;
}

namespace roamex::tabs {

// The per-tab initial-URL capture rule (roam-11 / I-2.2, plan §4.7 / R3):
// captures the REDIRECT-CHAIN HEAD of the first user-intended primary
// main-frame navigation, then stays sticky. FORWARD CONTRACT for
// I-2.3 (reload command) and I-2.5 (edit UI + persistence):
//  - initial_url()/has_initial_url(): the captured or user-set value;
//  - SetUserInitialUrl(): sets AND LOCKS — automatic capture never overwrites
//    a user-set value;
//  - SetRestoredInitialUrl(): I-2.5's restore entry — sets the value and marks
//    it captured (unlocked) so the restore's own first load cannot clobber it.
// Capture never runs for BFCache restores or prerender activations, nor for
// client-redirected first navigations; a renderer-initiated navigation is
// accepted only as the tab's first-ever committed navigation (a tab opened
// via link — the opener's click is the user intent).
class TabInitialUrlHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<TabInitialUrlHelper> {
 public:
  // Patch-0009 entry point: flag-gated per-tab creation (TabFeatures::Init).
  static void MaybeCreateForTab(::tabs::TabInterface& tab);
  // Test/glue entry: same, from a WebContents.
  static void MaybeCreateForWebContents(content::WebContents* web_contents);
  // Patch-0009 entry point: discard/contents-replacement transfer.
  static void WillDiscardContents(::tabs::TabInterface* tab,
                                  content::WebContents* old_contents,
                                  content::WebContents* new_contents);

  TabInitialUrlHelper(const TabInitialUrlHelper&) = delete;
  TabInitialUrlHelper& operator=(const TabInitialUrlHelper&) = delete;
  ~TabInitialUrlHelper() override;

  const GURL& initial_url() const { return initial_url_; }
  bool has_initial_url() const { return captured_; }
  bool is_user_locked() const { return user_locked_; }
  void SetUserInitialUrl(const GURL& url);
  void SetRestoredInitialUrl(const GURL& url);

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

 private:
  friend class content::WebContentsUserData<TabInitialUrlHelper>;
  explicit TabInitialUrlHelper(content::WebContents* web_contents);

  GURL initial_url_;
  bool captured_ = false;
  bool user_locked_ = false;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace roamex::tabs

#endif  // ROAMEX_BROWSER_TABS_TAB_INITIAL_URL_HELPER_H_
