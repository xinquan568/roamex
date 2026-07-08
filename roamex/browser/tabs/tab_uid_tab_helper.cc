// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tabs/tab_uid_tab_helper.h"

#include <memory>
#include <utility>

#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "roamex/browser/tabs/tab_uid_service.h"
#include "roamex/browser/tabs/tab_uid_service_factory.h"
#include "roamex/common/roamex_features.h"

namespace roamex::tabs {

namespace {

// Carries a restored uid from the pre-insertion hand-off (patch 0009 hunk b)
// to helper attach.
class PendingRestoredTabUid
    : public content::WebContentsUserData<PendingRestoredTabUid> {
 public:
  ~PendingRestoredTabUid() override = default;

  static std::string Take(content::WebContents* web_contents) {
    PendingRestoredTabUid* pending = FromWebContents(web_contents);
    if (!pending) {
      return std::string();
    }
    std::string uid = std::move(pending->uid_);
    web_contents->RemoveUserData(UserDataKey());
    return uid;
  }

  PendingRestoredTabUid(content::WebContents* web_contents, std::string uid)
      : content::WebContentsUserData<PendingRestoredTabUid>(*web_contents),
        uid_(std::move(uid)) {}

 private:
  friend class content::WebContentsUserData<PendingRestoredTabUid>;

  std::string uid_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(PendingRestoredTabUid);

}  // namespace

// static
void TabUidTabHelper::MaybeCreateForTab(::tabs::TabInterface& tab,
                                        Profile* profile) {
  if (!base::FeatureList::IsEnabled(features::kInitialUrl)) {
    return;
  }
  content::WebContents* web_contents = tab.GetContents();
  if (!web_contents || FromWebContents(web_contents)) {
    return;
  }
  web_contents->SetUserData(UserDataKey(), base::WrapUnique(new TabUidTabHelper(
                                               web_contents, profile)));
}

// static
void TabUidTabHelper::WillDiscardContents(::tabs::TabInterface* tab,
                                          content::WebContents* old_contents,
                                          content::WebContents* new_contents) {
  TabUidTabHelper* old_helper = FromWebContents(old_contents);
  if (!old_helper || !new_contents) {
    return;
  }
  Profile* profile = old_helper->profile_;
  TabUidService* service = TabUidServiceFactory::GetForProfile(profile);
  const std::string uid = old_helper->uid();
  // Free the uid under the old id now; the old helper's destructor Unregister
  // then no-ops. The replacement adopts the SAME uid via the pending channel.
  service->Unregister(old_helper->tab_id_);
  new_contents->SetUserData(
      PendingRestoredTabUid::UserDataKey(),
      base::WrapUnique(new PendingRestoredTabUid(new_contents, uid)));
  new_contents->SetUserData(UserDataKey(), base::WrapUnique(new TabUidTabHelper(
                                               new_contents, profile)));
}

// static
void TabUidTabHelper::PopulateExtraData(
    ::tabs::TabInterface* tab,
    std::map<std::string, std::string>* extra_data) {
  if (!tab) {
    return;
  }
  content::WebContents* web_contents = tab->GetContents();
  if (!web_contents) {
    return;
  }
  TabUidTabHelper* helper = FromWebContents(web_contents);
  if (!helper || helper->uid().empty()) {
    return;
  }
  (*extra_data)[kExtraDataKey] = helper->uid();
}

// static
void TabUidTabHelper::SetPendingRestoredUid(
    content::WebContents* web_contents,
    const std::map<std::string, std::string>& extra_data) {
  if (!base::FeatureList::IsEnabled(features::kInitialUrl)) {
    return;
  }
  auto it = extra_data.find(kExtraDataKey);
  if (it == extra_data.end() || it->second.empty()) {
    return;
  }
  web_contents->SetUserData(
      PendingRestoredTabUid::UserDataKey(),
      base::WrapUnique(new PendingRestoredTabUid(web_contents, it->second)));
}

TabUidTabHelper::TabUidTabHelper(content::WebContents* web_contents,
                                 Profile* profile)
    : content::WebContentsUserData<TabUidTabHelper>(*web_contents),
      profile_(profile),
      tab_id_(sessions::SessionTabHelper::IdForTab(web_contents)) {
  TabUidService* service = TabUidServiceFactory::GetForProfile(profile_);
  const std::string pending = PendingRestoredTabUid::Take(web_contents);
  uid_ = pending.empty() ? service->MintAndRegister(tab_id_)
                         : service->AdoptOrRestamp(tab_id_, pending);
  Stamp();
}

TabUidTabHelper::~TabUidTabHelper() {
  if (TabUidService* service = TabUidServiceFactory::GetForProfile(profile_)) {
    service->Unregister(tab_id_);
  }
}

void TabUidTabHelper::Stamp() {
  // OTR identities never touch session persistence (D5).
  if (profile_->IsOffTheRecord()) {
    return;
  }
  SessionService* session_service =
      SessionServiceFactory::GetForProfile(profile_);
  if (!session_service) {
    return;
  }
  content::WebContents* web_contents = &GetWebContents();
  session_service->AddTabExtraData(
      sessions::SessionTabHelper::IdForWindowContainingTab(web_contents),
      tab_id_, kExtraDataKey, uid_);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabUidTabHelper);

}  // namespace roamex::tabs
