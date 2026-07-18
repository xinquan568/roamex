// SPDX-License-Identifier: Apache-2.0
// roam-85 (I-6.5): the pure Termixion update state machine — no Sparkle, no
// Chrome. Exhaustive transitions incl. skip-suppression, progress, error.
// This is exactly the contract roam-37's About card renders. TDD/P6: RED.

#include "roamux/browser/updates/update_state_machine.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux::updates {
namespace {

UpdateEvent Found(const std::string& v) {
  UpdateEvent e{UpdateEventType::kUpdateFound};
  e.version = v;
  e.date = "2026-01-01";
  e.notes = "notes for " + v;
  return e;
}

TEST(UpdateStateMachineTest, StartsIdle) {
  UpdateStateMachine sm;
  EXPECT_EQ(sm.snapshot().status, UpdateStatus::kIdle);
}

TEST(UpdateStateMachineTest, CheckThenUpToDate) {
  UpdateStateMachine sm;
  EXPECT_EQ(sm.OnEvent({UpdateEventType::kCheckStarted}).status,
            UpdateStatus::kChecking);
  EXPECT_EQ(sm.OnEvent({UpdateEventType::kUpToDate}).status,
            UpdateStatus::kUpToDate);
}

TEST(UpdateStateMachineTest, CheckThenAvailableCarriesMetadata) {
  UpdateStateMachine sm;
  sm.OnEvent({UpdateEventType::kCheckStarted});
  UpdateSnapshot s = sm.OnEvent(Found("2.0.0"));
  EXPECT_EQ(s.status, UpdateStatus::kAvailable);
  EXPECT_EQ(s.version, "2.0.0");
  EXPECT_EQ(s.date, "2026-01-01");
  EXPECT_EQ(s.notes, "notes for 2.0.0");
}

TEST(UpdateStateMachineTest, DownloadProgressThenReady) {
  UpdateStateMachine sm;
  sm.OnEvent({UpdateEventType::kCheckStarted});
  sm.OnEvent(Found("2.0.0"));
  EXPECT_EQ(sm.OnEvent({UpdateEventType::kDownloadStarted}).status,
            UpdateStatus::kDownloading);
  UpdateEvent p{UpdateEventType::kDownloadProgress};
  p.received = 50;
  p.total = 200;
  UpdateSnapshot s = sm.OnEvent(p);
  EXPECT_EQ(s.status, UpdateStatus::kDownloading);
  EXPECT_DOUBLE_EQ(s.progress, 0.25);
  EXPECT_EQ(sm.OnEvent({UpdateEventType::kReadyToInstall}).status,
            UpdateStatus::kReadyToInstall);
}

TEST(UpdateStateMachineTest, ErrorFromAnyState) {
  UpdateStateMachine sm;
  sm.OnEvent({UpdateEventType::kCheckStarted});
  UpdateEvent e{UpdateEventType::kError};
  e.error = "network down";
  UpdateSnapshot s = sm.OnEvent(e);
  EXPECT_EQ(s.status, UpdateStatus::kError);
  EXPECT_EQ(s.error, "network down");
}

TEST(UpdateStateMachineTest, SkippedVersionSuppressesAvailable) {
  UpdateStateMachine sm;
  sm.SetSkippedVersion("2.0.0");
  sm.OnEvent({UpdateEventType::kCheckStarted});
  // The skipped version does not surface as available.
  EXPECT_EQ(sm.OnEvent(Found("2.0.0")).status, UpdateStatus::kUpToDate);
  // A different version surfaces on the next check.
  sm.OnEvent({UpdateEventType::kCheckStarted});
  EXPECT_EQ(sm.OnEvent(Found("2.0.1")).status, UpdateStatus::kAvailable);
}

TEST(UpdateStateMachineTest, IllegalTransitionsAreNoOps) {
  UpdateStateMachine sm;
  // Ready without a download in flight stays idle.
  EXPECT_EQ(sm.OnEvent({UpdateEventType::kReadyToInstall}).status,
            UpdateStatus::kIdle);
  // Progress while idle is ignored.
  UpdateEvent p{UpdateEventType::kDownloadProgress};
  p.received = 10;
  p.total = 100;
  EXPECT_EQ(sm.OnEvent(p).status, UpdateStatus::kIdle);
  // kUpToDate / kUpdateFound are only outcomes of a check — ignored from idle.
  EXPECT_EQ(sm.OnEvent({UpdateEventType::kUpToDate}).status,
            UpdateStatus::kIdle);
  EXPECT_EQ(sm.OnEvent(Found("2.0.0")).status, UpdateStatus::kIdle);
  // kUpdateFound does not overwrite a download in flight.
  sm.OnEvent({UpdateEventType::kCheckStarted});
  sm.OnEvent(Found("2.0.0"));
  sm.OnEvent({UpdateEventType::kDownloadStarted});
  EXPECT_EQ(sm.OnEvent(Found("3.0.0")).status, UpdateStatus::kDownloading);
}

TEST(UpdateStateMachineTest, ProgressIsClampedToUnitRange) {
  UpdateStateMachine sm;
  sm.OnEvent({UpdateEventType::kCheckStarted});
  sm.OnEvent(Found("2.0.0"));
  sm.OnEvent({UpdateEventType::kDownloadStarted});
  UpdateEvent over{UpdateEventType::kDownloadProgress};
  over.received = 500;
  over.total = 200;  // > 100%
  EXPECT_DOUBLE_EQ(sm.OnEvent(over).progress, 1.0);
}

}  // namespace
}  // namespace roamux::updates
