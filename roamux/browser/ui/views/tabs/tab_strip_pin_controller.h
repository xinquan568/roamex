// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UI_VIEWS_TABS_TAB_STRIP_PIN_CONTROLLER_H_
#define ROAMUX_BROWSER_UI_VIEWS_TABS_TAB_STRIP_PIN_CONTROLLER_H_

#include "base/memory/raw_ptr.h"

namespace roamux {

// roam-214: the pin/peek state machine for the auto-hide vertical tab strip
// (issue D1-D9), as a pure core. Every browser-layer effect and query goes
// through Delegate so the core is fast-unit-testable with a fake (plan R3);
// the real delegate lives in the views glue compiled into the upstream UI
// target (patch 0054).
//
// States: {pinned, armed, suppressing}. The shortcut toggles PINNED (never
// raw visibility, D1); a completed interaction ARMS retraction (D3); the
// strip unpins when the D4 predicate first holds: armed AND pointer outside
// AND focus outside AND no external keep-open lock. Explicit unpin (shortcut
// while pinned, Esc in strip) bypasses the predicate and, with the pointer
// still inside, takes a temporary SUPPRESSION (kForceCollapse) released on
// the next genuine pointer exit — the hide-under-cursor rule.
class TabStripPinController {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Lock effects (own locks; the glue maps these onto the upstream
    // ExpandOnHoverLock factory).
    virtual void AcquireKeepExpanded() = 0;
    virtual void ReleaseKeepExpanded() = 0;
    virtual void AcquireForceCollapse() = 0;
    virtual void ReleaseForceCollapse() = 0;

    // Queries. ExternalKeepOpenLockCount EXCLUDES this controller's own
    // locks (plan R2 lock-state seam).
    virtual int ExternalKeepOpenLockCount() = 0;
    virtual bool IsPointerInsideStrip() = 0;
    virtual bool IsFocusInsideStrip() = 0;

    // Mode-A effects.
    virtual void FocusStrip() = 0;
    // Collapses even with focus inside the strip and restores focus to the
    // web contents (explicit-unpin path only; predicate unpins collapse
    // naturally via lock release).
    virtual void ForceCollapseNow() = 0;

    // Mode-B effect: the exact collapse-button action semantics
    // (kActionToggleCollapseVertical path).
    virtual void ToggleCollapseViaButtonSemantics() = 0;
  };

  // Strip mode at the moment the command fires (issue table: Mode A when
  // placement is vertical and expand-on-hover is on; Mode B when vertical
  // and hover off; the command is disabled otherwise so kDisabled never
  // normally reaches the core).
  enum class Mode { kPinPeek, kPlainToggle, kDisabled };

  explicit TabStripPinController(Delegate* delegate);
  TabStripPinController(const TabStripPinController&) = delete;
  TabStripPinController& operator=(const TabStripPinController&) = delete;
  ~TabStripPinController();

  bool pinned() const { return pinned_; }
  bool armed() const { return armed_; }
  bool suppressing() const { return suppressing_; }

  // Inputs (from the glue / command shim).
  void OnShortcut(Mode mode);
  void OnEscInStrip();
  void OnCompletedInteraction();  // mouse/keyboard completion, menu execute
  void OnPointerExit();
  void OnFocusChanged();
  void OnExternalLockReleased();  // evaluated BEFORE upstream re-expand (D9)
  void OnLifecycleReset();  // fullscreen, placement/mode/setting change (D9)
  void OnRegionDetached();  // window teardown: region view dies first (F4)

 private:
  void ExplicitUnpin();
  void MaybeUnpin();  // the D4 predicate

  raw_ptr<Delegate> delegate_;
  bool pinned_ = false;
  bool armed_ = false;
  bool suppressing_ = false;
};

}  // namespace roamux

#endif  // ROAMUX_BROWSER_UI_VIEWS_TABS_TAB_STRIP_PIN_CONTROLLER_H_
