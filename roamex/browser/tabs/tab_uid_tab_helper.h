// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_TABS_TAB_UID_TAB_HELPER_H_
#define ROAMEX_BROWSER_TABS_TAB_UID_TAB_HELPER_H_

#include <map>
#include <string>

#include "base/memory/raw_ptr.h"
#include "components/sessions/core/session_id.h"
#include "content/public/browser/web_contents_user_data.h"

class Profile;

namespace tabs {
class TabInterface;
}

namespace roamex::tabs {

// Per-tab uid glue (roam-10 / I-2.1), created for every tab from
// TabFeatures::Init via patch 0009 when roamex::features::kInitialUrl is on.
// Adopts a pending restored uid (or mints fresh) at attach, stamps it into
// tab-level session extra_data (never in OTR), and unregisters on destruction
// so closed-tab reopen can reuse the uid.
//
class TabUidTabHelper : public content::WebContentsUserData<TabUidTabHelper> {
 public:
  static constexpr char kExtraDataKey[] = "roamex.tab_uid";

  // Patch-0009 entry point (a): flag-gated per-tab creation.
  static void MaybeCreateForTab(::tabs::TabInterface& tab, Profile* profile);

  // Patch-0009 entry point (b): captures the live uid into a closed-tab
  // entry's extra_data (the TabRestoreService reopen channel).
  static void PopulateExtraData(::tabs::TabInterface* tab,
                                std::map<std::string, std::string>* extra_data);

  // Patch-0009 entry point (c): discard/contents-replacement — carries the
  // SAME uid onto the replacement WebContents (a discard is the same live
  // tab; §6.2 identity must survive it).
  static void WillDiscardContents(::tabs::TabInterface* tab,
                                  content::WebContents* old_contents,
                                  content::WebContents* new_contents);

  // Patch-0009 entry point (d): stashes the restored uid from tab-level
  // session extra_data on the not-yet-inserted WebContents; adopted at
  // attach.
  static void SetPendingRestoredUid(
      content::WebContents* web_contents,
      const std::map<std::string, std::string>& extra_data);

  TabUidTabHelper(const TabUidTabHelper&) = delete;
  TabUidTabHelper& operator=(const TabUidTabHelper&) = delete;
  ~TabUidTabHelper() override;

  const std::string& uid() const { return uid_; }

 private:
  friend class content::WebContentsUserData<TabUidTabHelper>;
  TabUidTabHelper(content::WebContents* web_contents, Profile* profile);

  void Stamp();

  raw_ptr<Profile> profile_;
  // Cached at construction: user-data teardown order is unspecified, so the
  // destructor must not resolve the id through SessionTabHelper.
  const SessionID tab_id_;
  std::string uid_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace roamex::tabs

#endif  // ROAMEX_BROWSER_TABS_TAB_UID_TAB_HELPER_H_
