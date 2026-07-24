// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/ui/views/tabs/tab_strip_pin_controller.h"

namespace roamux {

TabStripPinController::TabStripPinController(Delegate* delegate)
    : delegate_(delegate) {}

TabStripPinController::~TabStripPinController() = default;

void TabStripPinController::OnShortcut(Mode mode) {
  switch (mode) {
    case Mode::kPinPeek:
      if (!pinned_) {
        // D1: not-pinned (collapsed OR hover-revealed) -> pin. A pending
        // suppression is superseded by an explicit re-pin.
        if (suppressing_) {
          suppressing_ = false;
          delegate_->ReleaseForceCollapse();
        }
        pinned_ = true;
        armed_ = false;
        delegate_->AcquireKeepExpanded();
        delegate_->FocusStrip();
      } else {
        ExplicitUnpin();
      }
      return;
    case Mode::kPlainToggle:
      // D7: two triggers, one state — exactly the collapse button.
      delegate_->ToggleCollapseViaButtonSemantics();
      return;
    case Mode::kDisabled:
      return;  // command is disabled; a stray call is a no-op.
  }
}

void TabStripPinController::OnEscInStrip() {
  if (pinned_) {
    ExplicitUnpin();
  }
}

void TabStripPinController::OnCompletedInteraction() {
  if (!pinned_) {
    return;
  }
  armed_ = true;
  // The interaction may have already moved focus out (keyboard activation
  // switches to web content); the pointer may be gone too.
  MaybeUnpin();
}

void TabStripPinController::OnPointerExit() {
  if (suppressing_) {
    // The hide-under-cursor suppression ends at the first genuine exit.
    suppressing_ = false;
    delegate_->ReleaseForceCollapse();
  }
  MaybeUnpin();
}

void TabStripPinController::OnFocusChanged() {
  MaybeUnpin();
}

void TabStripPinController::OnExternalLockReleased() {
  // D9 omnibox ordering: this is invoked BEFORE upstream decides to
  // re-expand, so an armed+abandoned pin dissolves instead of bouncing open.
  MaybeUnpin();
}

void TabStripPinController::OnLifecycleReset() {
  // D9: fullscreen entry, placement change, vertical-mode off, hover-setting
  // flip. Drop everything; upstream owns the visual outcome.
  if (pinned_) {
    delegate_->ReleaseKeepExpanded();
  }
  if (suppressing_) {
    delegate_->ReleaseForceCollapse();
  }
  pinned_ = false;
  armed_ = false;
  suppressing_ = false;
}

void TabStripPinController::OnRegionDetached() {
  // F4 teardown order: the region view dies before this controller. The
  // delegate tolerates the detached region; state must not outlive it.
  OnLifecycleReset();
}

void TabStripPinController::ExplicitUnpin() {
  delegate_->ReleaseKeepExpanded();
  pinned_ = false;
  armed_ = false;
  // Hide-under-cursor rule: suppress hover re-reveal until the pointer
  // genuinely leaves once.
  if (delegate_->IsPointerInsideStrip()) {
    suppressing_ = true;
    delegate_->AcquireForceCollapse();
  }
  delegate_->ForceCollapseNow();
}

void TabStripPinController::MaybeUnpin() {
  if (!pinned_ || !armed_) {
    return;
  }
  if (delegate_->IsPointerInsideStrip() || delegate_->IsFocusInsideStrip() ||
      delegate_->ExternalKeepOpenLockCount() > 0) {
    return;
  }
  // Predicate holds: release the pin lock and let upstream collapse
  // naturally (no force-collapse on this path).
  delegate_->ReleaseKeepExpanded();
  pinned_ = false;
  armed_ = false;
}

}  // namespace roamux
