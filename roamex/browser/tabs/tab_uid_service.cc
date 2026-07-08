// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tabs/tab_uid_service.h"

#include "base/uuid.h"

namespace roamex::tabs {

TabUidService::TabUidService() = default;
TabUidService::~TabUidService() = default;

std::string TabUidService::MintAndRegister(SessionID tab_id) {
  Unregister(tab_id);
  std::string uid = base::Uuid::GenerateRandomV4().AsLowercaseString();
  uid_by_tab_[tab_id.id()] = uid;
  tab_by_uid_[uid] = tab_id.id();
  return uid;
}

std::string TabUidService::AdoptOrRestamp(SessionID tab_id,
                                          const std::string& restored_uid) {
  const bool well_formed = base::Uuid::ParseLowercase(restored_uid).is_valid();
  if (!well_formed || IsLive(restored_uid)) {
    // §6.9: keep the incumbent's identity; the newcomer (or a malformed
    // value) gets a fresh mint.
    return MintAndRegister(tab_id);
  }
  Unregister(tab_id);
  uid_by_tab_[tab_id.id()] = restored_uid;
  tab_by_uid_[restored_uid] = tab_id.id();
  return restored_uid;
}

void TabUidService::Unregister(SessionID tab_id) {
  auto it = uid_by_tab_.find(tab_id.id());
  if (it == uid_by_tab_.end()) {
    return;
  }
  tab_by_uid_.erase(it->second);
  uid_by_tab_.erase(it);
}

std::optional<std::string> TabUidService::GetUidForTab(SessionID tab_id) const {
  auto it = uid_by_tab_.find(tab_id.id());
  if (it == uid_by_tab_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool TabUidService::IsLive(const std::string& uid) const {
  return tab_by_uid_.contains(uid);
}

}  // namespace roamex::tabs
