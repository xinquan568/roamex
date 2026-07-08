// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_TABS_TAB_UID_SERVICE_H_
#define ROAMEX_BROWSER_TABS_TAB_UID_SERVICE_H_

#include <map>
#include <optional>
#include <string>

#include "components/keyed_service/core/keyed_service.h"
#include "components/sessions/core/session_id.h"

namespace roamex::tabs {

// The live-tab uid registry (roam-10 / I-2.1; plan §6.2/§6.9). One instance
// per profile (see TabUidServiceFactory: regular and OTR each get their OWN
// instance, never redirected).
//
// E4 CONTRACT (do not change without a migration): uids are lowercase UUIDv4
// strings; at any moment no two live tabs in one profile share a uid; closing
// a tab frees its uid (closed-tab reopen reuses it); a restored uid that is
// already live is re-stamped with a fresh mint (the incumbent keeps its
// identity); duplication mints fresh (tab-level session extra_data is not
// cloned by duplication — the channel choice enforces the rule).
class TabUidService : public KeyedService {
 public:
  TabUidService();
  TabUidService(const TabUidService&) = delete;
  TabUidService& operator=(const TabUidService&) = delete;
  ~TabUidService() override;

  // Mints a fresh uid and registers it for `tab_id`.
  std::string MintAndRegister(SessionID tab_id);

  // Adopts `restored_uid` for `tab_id` when it is well-formed and not live on
  // another tab; otherwise keeps §6.2 intact by minting fresh (§6.9
  // re-stamp). If `tab_id` already held a uid (mint-before-handoff ordering),
  // the earlier uid is released. Returns the effective uid.
  std::string AdoptOrRestamp(SessionID tab_id, const std::string& restored_uid);

  // Frees `tab_id`'s uid (tab closed) — the uid becomes adoptable again.
  void Unregister(SessionID tab_id);

  std::optional<std::string> GetUidForTab(SessionID tab_id) const;
  bool IsLive(const std::string& uid) const;

 private:
  std::map<SessionID::id_type, std::string> uid_by_tab_;
  std::map<std::string, SessionID::id_type> tab_by_uid_;
};

}  // namespace roamex::tabs

#endif  // ROAMEX_BROWSER_TABS_TAB_UID_SERVICE_H_
