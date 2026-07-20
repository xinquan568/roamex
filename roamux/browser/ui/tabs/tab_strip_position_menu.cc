// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/ui/tabs/tab_strip_position_menu.h"

#include <array>
#include <string>
#include <utility>

#include "base/feature_list.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/tab_strip_placement.h"
#include "ui/base/models/menu_separator_types.h"

namespace roamux::tabs {

namespace {

// Ordered by TabStripPlacement value (Top, Bottom, Left, Right). v1 strings are
// literal English (plan D5); localization lands with the epic flag flip.
constexpr std::array<const char16_t*, 4> kPlacementLabels = {u"Top", u"Bottom",
                                                             u"Left", u"Right"};
constexpr int kRadioGroupId = 1;

}  // namespace

TabStripPositionMenuModel::TabStripPositionMenuModel(PrefService* pref_service)
    : ui::SimpleMenuModel(this), pref_service_(pref_service) {
  for (size_t i = 0; i < kPlacementLabels.size(); ++i) {
    AddRadioItem(kTabStripPositionFirstItemCommandId + static_cast<int>(i),
                 kPlacementLabels[i], kRadioGroupId);
  }
}

TabStripPositionMenuModel::~TabStripPositionMenuModel() = default;

bool TabStripPositionMenuModel::IsCommandIdChecked(int command_id) const {
  const int value = command_id - kTabStripPositionFirstItemCommandId;
  return static_cast<int>(GetTabStripPlacement(pref_service_)) == value;
}

bool TabStripPositionMenuModel::IsCommandIdEnabled(int command_id) const {
  return true;
}

void TabStripPositionMenuModel::ExecuteCommand(int command_id,
                                               int event_flags) {
  const int value = command_id - kTabStripPositionFirstItemCommandId;
  SetTabStripPlacement(pref_service_, static_cast<TabStripPlacement>(value));
}

std::unique_ptr<ui::SimpleMenuModel> MaybeAppendTabStripPositionSubMenu(
    ui::SimpleMenuModel* parent,
    PrefService* pref_service) {
  if (!parent || !pref_service ||
      !base::FeatureList::IsEnabled(features::kTabStripPosition)) {
    return nullptr;
  }
  auto submenu = std::make_unique<TabStripPositionMenuModel>(pref_service);
  parent->AddSeparator(ui::NORMAL_SEPARATOR);
  parent->AddSubMenu(kTabStripPositionSubMenuCommandId, u"Tab strip position",
                     submenu.get());
  return submenu;
}

}  // namespace roamux::tabs
