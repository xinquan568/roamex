// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/tab_visit/tab_visit_gesture_watcher.h"

#include <set>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "roamex/browser/tab_visit/tab_visit_traversal_coordinator.h"
#include "roamex/browser/tab_visit/tab_visit_traversal_coordinator_factory.h"
#include "ui/base/base_window.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/views/event_monitor.h"

namespace roamex::tab_visit {

namespace {
// The traversal chord modifiers (Ctrl+Cmd); the gesture is "held" while both
// remain down. Releasing either settles.
constexpr int kChordModifiers = ui::EF_CONTROL_DOWN | ui::EF_COMMAND_DOWN;
}  // namespace

TabVisitGestureWatcher::TabVisitGestureWatcher(BrowserWindowInterface* browser)
    : browser_(browser) {
  event_monitor_ = views::EventMonitor::CreateWindowMonitor(
      this, browser_->GetWindow()->GetNativeWindow(),
      {ui::EventType::kKeyPressed, ui::EventType::kKeyReleased});
}

TabVisitGestureWatcher::~TabVisitGestureWatcher() = default;

void TabVisitGestureWatcher::OnEvent(const ui::Event& event) {
  if (!event.IsKeyEvent()) {
    return;
  }
  TabVisitTraversalCoordinator* coordinator =
      TabVisitTraversalCoordinatorFactory::GetForProfile(
          browser_->GetProfile());
  if (!coordinator || !coordinator->IsTraversalActive()) {
    return;
  }
  const ui::KeyEvent& key = *event.AsKeyEvent();

  // Escape cancels the gesture without committing.
  if (key.type() == ui::EventType::kKeyPressed &&
      key.key_code() == ui::VKEY_ESCAPE) {
    coordinator->CancelGesture();
    RefreshCommands();
    return;
  }

  // Modifier-release-PRIMARY: once the chord is no longer fully held, settle.
  if ((key.flags() & kChordModifiers) != kChordModifiers) {
    coordinator->Settle();
    RefreshCommands();
  }
}

void TabVisitGestureWatcher::RefreshCommands() {
  // The gesture just ended, so Back/Forward enablement changed. Refresh this
  // window's command state (the debounce-fallback path refreshes on the next
  // tab-state change; command execution re-checks CanGo* regardless).
  if (Browser* browser = browser_->GetBrowserForMigrationOnly()) {
    browser->command_controller()->TabStateChanged();
  }
}

}  // namespace roamex::tab_visit
