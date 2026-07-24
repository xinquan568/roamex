// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UI_VIEWS_TABS_TAB_STRIP_PIN_CONTROLLER_VIEWS_H_
#define ROAMUX_BROWSER_UI_VIEWS_TABS_TAB_STRIP_PIN_CONTROLLER_VIEWS_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "components/prefs/pref_change_registrar.h"
#include "roamux/browser/ui/views/tabs/tab_strip_pin_controller.h"
#include "roamux/browser/ui/views/tabs/vertical_tab_strip_roamux_observer.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/events/event_handler.h"

class Browser;
class FullscreenController;
class VerticalTabStripRegionView;

namespace roamux {

// roam-214: the per-window views glue (patch 0054; owned by
// BrowserWindowFeatures — created only when kTabStripToggleShortcut is on).
// Implements the pin core's Delegate against the real region view, receives
// the upstream notify-only seam, installs the command handler, and owns the
// pinned-lifetime event handler (mouse completion pairing + pre-target key
// intent) and the Esc accelerator (D6 scoping).
//
// F4 teardown order: Browser::~Browser destroys the window (and the region
// view) BEFORE BrowserWindowFeatures members — OnRegionViewDestroyed clears
// the region and drops all locks; every region use is null-checked.
class TabStripPinControllerViews : public TabStripPinController::Delegate,
                                   public VerticalTabStripRoamuxObserver,
                                   public ui::EventHandler,
                                   public ui::AcceleratorTarget,
                                   public TabStripModelObserver,
                                   public FullscreenObserver {
 public:
  explicit TabStripPinControllerViews(Browser* browser);
  TabStripPinControllerViews(const TabStripPinControllerViews&) = delete;
  TabStripPinControllerViews& operator=(const TabStripPinControllerViews&) =
      delete;
  ~TabStripPinControllerViews() override;

  // Region-view lifecycle (called from the patched ctor/dtor via the free
  // functions below; also covers placement-driven strip rebuilds).
  void AttachRegionView(VerticalTabStripRegionView* region_view);
  void DetachRegionView();

  TabStripPinController& core() { return core_; }

  // TabStripPinController::Delegate:
  void AcquireKeepExpanded() override;
  void ReleaseKeepExpanded() override;
  void AcquireForceCollapse() override;
  void ReleaseForceCollapse() override;
  int ExternalKeepOpenLockCount() override;
  bool IsPointerInsideStrip() override;
  bool IsFocusInsideStrip() override;
  void FocusStrip() override;
  void ForceCollapseNow() override;
  void ToggleCollapseViaButtonSemantics() override;

  // VerticalTabStripRoamuxObserver:
  void OnRoamuxPointerExit() override;
  void OnRoamuxFocusChanged() override;
  void OnRoamuxKeepOpenLocksChanged(int total_keep_open_count,
                                    bool released) override;
  void OnRoamuxStripMenuCommandExecuted() override;
  void OnRoamuxStripControlActivated() override;

  // ui::EventHandler — the glue itself is the PRE-target handler while
  // pinned (Esc scoping + key-intent recording); the nested PostHandler is
  // the POST-target one (completion-aware mouse pairing + end-of-dispatch
  // intent clearing).
  void OnMouseEvent(ui::MouseEvent* event) override;
  void OnKeyEvent(ui::KeyEvent* event) override;

  // ui::AcceleratorTarget (Esc while pinned; D6 focus scoping inside):
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

  // TabStripModelObserver (keyboard-activation completion confirmation):
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // FullscreenObserver (D9 reset):
  void OnFullscreenStateChanged() override;

 private:
  friend class TabStripPinControllerViewsTestApi;

  void OnToggleCommand();
  void UpdateCommandEnablement();
  void OnModeRelevantPrefChanged();
  void InstallPinnedHandlers();
  void RemovePinnedHandlers();

  raw_ptr<Browser> browser_;
  raw_ptr<VerticalTabStripRegionView> region_view_ = nullptr;
  TabStripPinController core_;

  std::unique_ptr<ExpandOnHoverLock> keep_expanded_lock_;
  std::unique_ptr<ExpandOnHoverLock> force_collapse_lock_;
  int total_keep_open_locks_ = 0;

  bool pinned_handlers_installed_ = false;

  PrefChangeRegistrar pref_registrar_;
  base::ScopedObservation<FullscreenController, FullscreenObserver>
      fullscreen_observation_{this};
  base::CallbackListSubscription browser_close_subscription_;
  base::WeakPtrFactory<TabStripPinControllerViews> weak_factory_{this};
};

}  // namespace roamux

#endif  // ROAMUX_BROWSER_UI_VIEWS_TABS_TAB_STRIP_PIN_CONTROLLER_VIEWS_H_
