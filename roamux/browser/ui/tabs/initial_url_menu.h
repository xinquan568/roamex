// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UI_TABS_INITIAL_URL_MENU_H_
#define ROAMUX_BROWSER_UI_TABS_INITIAL_URL_MENU_H_

#include <memory>

namespace content {
class WebContents;
}
namespace ui {
class SimpleMenuModel;
}

namespace roamux::tabs {

// Command ids inside TabMenuModel's id space (0005 took 2101-2105 for the
// tab-strip-position submenu; roam-14 took 2110-2112 here). The submenu
// ANCHOR row lives in the parent TabMenuModel, so upstream
// TabContextMenuController answers its delegate queries; the interior items
// are resolved by the submenu model's own delegate.
inline constexpr int kInitialUrlSubMenuCommandId = 2110;
inline constexpr int kEditInitialUrlCommandId = 2111;
inline constexpr int kSetInitialUrlToCurrentPageCommandId = 2112;

// roam-194: true for every command id this submenu owns (the anchor 2110 and
// the items 2111-2112). Single source of truth for the guard in upstream
// TabContextMenuController (patch 0005): without it the anchor id reaches the
// upstream delegate's blind static_cast to TabStripModel::ContextMenuCommand
// and dies on fatal NOTREACHED()/CHECK paths — the roam-181 mechanism, one
// bare right-click with RoamuxInitialUrl enabled.
inline constexpr bool IsInitialUrlCommandId(int command_id) {
  return command_id >= kInitialUrlSubMenuCommandId &&
         command_id <= kSetInitialUrlToCurrentPageCommandId;
}

// Patch-0012 entry point (roam-14, §4.5): appends the flag-gated
// "Initial URL" submenu to the tab context menu — "Edit initial URL…" (the
// dialog) and "Set initial URL to current page" (one click, locks). Returns
// the submenu model the caller must own (0005 pattern), or null when the
// flag is off or the tab has no helper.
std::unique_ptr<ui::SimpleMenuModel> MaybeAppendInitialUrlSubMenu(
    ui::SimpleMenuModel* parent,
    content::WebContents* contents);

}  // namespace roamux::tabs

#endif  // ROAMUX_BROWSER_UI_TABS_INITIAL_URL_MENU_H_
