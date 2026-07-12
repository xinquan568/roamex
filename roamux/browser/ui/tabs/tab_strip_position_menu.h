// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UI_TABS_TAB_STRIP_POSITION_MENU_H_
#define ROAMUX_BROWSER_UI_TABS_TAB_STRIP_POSITION_MENU_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"

class PrefService;

namespace roamux::tabs {

// Command ids inside TabMenuModel's id space. TabMenuModel's dynamic submenus
// document their ranges in
// chrome/browser/ui/tabs/existing_base_sub_menu_model.h (1001–1900); roamux
// takes 2101+.
inline constexpr int kTabStripPositionSubMenuCommandId = 2101;
// Radio items: 2102 = Top, then +1 each for Bottom / Left / Right (mirrors the
// roamux::TabStripPlacement values).
inline constexpr int kTabStripPositionFirstItemCommandId = 2102;

// The flag-gated "Tab strip position (Roamux)" submenu for the tab context
// menu: four radio items whose checked state mirrors
// roamux::prefs::kTabStripPosition and whose activation writes it. Acts as its
// own delegate, so activation never routes through the host menu's delegate.
class TabStripPositionMenuModel : public ui::SimpleMenuModel,
                                  public ui::SimpleMenuModel::Delegate {
 public:
  explicit TabStripPositionMenuModel(PrefService* pref_service);
  TabStripPositionMenuModel(const TabStripPositionMenuModel&) = delete;
  TabStripPositionMenuModel& operator=(const TabStripPositionMenuModel&) =
      delete;
  ~TabStripPositionMenuModel() override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  raw_ptr<PrefService> pref_service_;
};

// When roamux::features::kTabStripPosition is enabled, appends a separator +
// the submenu to `parent` and returns the submenu model — the caller owns it
// and must keep it alive for the menu's lifetime. Returns nullptr (and leaves
// `parent` untouched) when the flag is off or either argument is null.
std::unique_ptr<ui::SimpleMenuModel> MaybeAppendTabStripPositionSubMenu(
    ui::SimpleMenuModel* parent,
    PrefService* pref_service);

}  // namespace roamux::tabs

#endif  // ROAMUX_BROWSER_UI_TABS_TAB_STRIP_POSITION_MENU_H_
