// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/updates/fake_update_page_handler.h"

#include <utility>

namespace roamux::updates {

FakeUpdatePageHandler::FakeUpdatePageHandler() = default;
FakeUpdatePageHandler::~FakeUpdatePageHandler() = default;

void FakeUpdatePageHandler::Bind(
    mojo::PendingRemote<mojom::UpdatePage> page,
    mojo::PendingReceiver<mojom::UpdatePageHandler> handler) {
  page_.Bind(std::move(page));
  receiver_.Bind(std::move(handler));
}

void FakeUpdatePageHandler::PushState(mojom::UpdateSnapshotPtr snapshot) {
  page_->OnStateChanged(std::move(snapshot));
}

void FakeUpdatePageHandler::DriveEvent(const UpdateEvent& event) {
  const UpdateSnapshot snap = state_machine_.OnEvent(event);
  auto out = mojom::UpdateSnapshot::New();
  out->status = static_cast<mojom::UpdateStatus>(snap.status);
  out->version = snap.version;
  out->date = snap.date;
  out->notes = snap.notes;
  out->error = snap.error;
  out->progress = snap.progress;
  PushState(std::move(out));
}

void FakeUpdatePageHandler::CheckForUpdates() {
  ++check_count_;
}

void FakeUpdatePageHandler::Download() {
  ++download_count_;
}

void FakeUpdatePageHandler::InstallAndRelaunch() {
  ++install_count_;
}

void FakeUpdatePageHandler::Skip(const std::string& version) {
  skipped_.push_back(version);
}

FakeUpdatePageHandlerFactory::FakeUpdatePageHandlerFactory(
    FakeUpdatePageHandler* handler)
    : handler_(handler) {}
FakeUpdatePageHandlerFactory::~FakeUpdatePageHandlerFactory() = default;

void FakeUpdatePageHandlerFactory::Bind(
    mojo::PendingReceiver<mojom::UpdatePageHandlerFactory> receiver) {
  receiver_.Bind(std::move(receiver));
}

void FakeUpdatePageHandlerFactory::CreatePageHandler(
    mojo::PendingRemote<mojom::UpdatePage> page,
    mojo::PendingReceiver<mojom::UpdatePageHandler> handler) {
  handler_->Bind(std::move(page), std::move(handler));
  // The service pushes the current snapshot on bind.
  handler_->PushState(mojom::UpdateSnapshot::New());
}

}  // namespace roamux::updates
