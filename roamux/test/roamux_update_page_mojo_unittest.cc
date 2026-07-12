// SPDX-License-Identifier: Apache-2.0
// roam-85: the UpdatePageHandler/UpdatePage Mojo round-trip via the fake
// handler — command capture + state broadcast — the surface roam-37 builds on.

#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "roamux/browser/updates/fake_update_page_handler.h"
#include "roamux/mojom/update_page.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace roamux::updates {
namespace {

// A fake WebUI-side page that records pushed snapshots.
class RecordingPage : public mojom::UpdatePage {
 public:
  void OnStateChanged(mojom::UpdateSnapshotPtr snapshot) override {
    last_status = snapshot->status;
    last_version = snapshot->version;
    ++count;
  }
  mojom::UpdateStatus last_status = mojom::UpdateStatus::kIdle;
  std::string last_version;
  int count = 0;
};

class UpdatePageMojoTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(UpdatePageMojoTest, CommandsCapturedAndStateBroadcast) {
  FakeUpdatePageHandler handler;
  RecordingPage page;
  mojo::Receiver<mojom::UpdatePage> page_receiver{&page};
  mojo::Remote<mojom::UpdatePageHandler> handler_remote;

  handler.Bind(page_receiver.BindNewPipeAndPassRemote(),
               handler_remote.BindNewPipeAndPassReceiver());

  // Commands from the WebUI reach the handler.
  handler_remote->CheckForUpdates();
  handler_remote->Download();
  handler_remote->Skip("2.0.0");
  handler_remote.FlushForTesting();
  EXPECT_EQ(handler.check_count(), 1);
  EXPECT_EQ(handler.download_count(), 1);
  ASSERT_EQ(handler.skipped().size(), 1u);
  EXPECT_EQ(handler.skipped()[0], "2.0.0");

  // State pushed from the service reaches the page.
  auto snap = mojom::UpdateSnapshot::New();
  snap->status = mojom::UpdateStatus::kAvailable;
  snap->version = "2.0.0";
  handler.PushState(std::move(snap));
  page_receiver.FlushForTesting();
  EXPECT_EQ(page.count, 1);
  EXPECT_EQ(page.last_status, mojom::UpdateStatus::kAvailable);
  EXPECT_EQ(page.last_version, "2.0.0");
}

TEST_F(UpdatePageMojoTest, FactoryBindPushesInitialSnapshotAndDrivenState) {
  FakeUpdatePageHandler handler;
  FakeUpdatePageHandlerFactory factory(&handler);
  RecordingPage page;
  mojo::Receiver<mojom::UpdatePage> page_receiver{&page};
  mojo::Remote<mojom::UpdatePageHandlerFactory> factory_remote;
  factory.Bind(factory_remote.BindNewPipeAndPassReceiver());

  mojo::Remote<mojom::UpdatePageHandler> handler_remote;
  factory_remote->CreatePageHandler(
      page_receiver.BindNewPipeAndPassRemote(),
      handler_remote.BindNewPipeAndPassReceiver());
  factory_remote.FlushForTesting();
  page_receiver.FlushForTesting();
  // Bootstrap: the initial snapshot was pushed.
  EXPECT_GE(page.count, 1);
  EXPECT_EQ(page.last_status, mojom::UpdateStatus::kIdle);

  // A state-machine-driven event reaches the page as OnStateChanged.
  handler.DriveEvent({UpdateEventType::kCheckStarted});
  UpdateEvent found{UpdateEventType::kUpdateFound};
  found.version = "9.9.9";
  handler.DriveEvent(found);
  page_receiver.FlushForTesting();
  EXPECT_EQ(page.last_status, mojom::UpdateStatus::kAvailable);
  EXPECT_EQ(page.last_version, "9.9.9");
}

}  // namespace
}  // namespace roamux::updates
