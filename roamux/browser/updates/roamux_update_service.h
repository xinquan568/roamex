// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UPDATES_ROAMUX_UPDATE_SERVICE_H_
#define ROAMUX_BROWSER_UPDATES_ROAMUX_UPDATE_SERVICE_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/keyed_service/core/keyed_service.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "roamux/browser/updates/update_state_machine.h"
#include "roamux/mojom/update_page.mojom.h"

// The Roamux update service (roam-85, I-6.5): a per-profile facade over ONE
// process-wide Sparkle owner (updates are app-wide). It implements the Mojo
// UpdatePageHandler (WebUI commands) and pushes UpdateSnapshot state to the
// bound UpdatePage; the state comes from the pure UpdateStateMachine, driven
// by a custom SPUUserDriver conformer that stores Sparkle's found/ready reply
// blocks so Download/Skip/InstallAndRelaunch can drive them. Flag-on
// (roamux_enable_sparkle) only.
namespace roamux::updates {

// Opaque process-wide Sparkle owner (defined in the .mm; holds the single
// SPUUpdater + conformer). Exposed as a forward decl so the header stays
// Objective-C-free for C++ includers.
class SparkleOwner;

class RoamuxUpdateService : public KeyedService,
                            public mojom::UpdatePageHandlerFactory,
                            public mojom::UpdatePageHandler {
 public:
  RoamuxUpdateService();
  ~RoamuxUpdateService() override;

  RoamuxUpdateService(const RoamuxUpdateService&) = delete;
  RoamuxUpdateService& operator=(const RoamuxUpdateService&) = delete;

  // Binds the factory receiver (the WebUI's entry point).
  void BindFactory(
      mojo::PendingReceiver<mojom::UpdatePageHandlerFactory> receiver);

  // mojom::UpdatePageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingRemote<mojom::UpdatePage> page,
      mojo::PendingReceiver<mojom::UpdatePageHandler> handler) override;

  // mojom::UpdatePageHandler:
  void CheckForUpdates() override;
  void Download() override;
  void InstallAndRelaunch() override;
  void Skip(const std::string& version) override;

  // Called by the conformer (on the main thread) with a translated event.
  void OnUpdateEvent(const UpdateEvent& event);

 private:
  void PushSnapshot(const UpdateSnapshot& snapshot);

  UpdateStateMachine state_machine_;
  // Ref-counted process-wide Sparkle owner (not owned uniquely).
  raw_ptr<SparkleOwner> shared_owner_ = nullptr;
  mojo::Receiver<mojom::UpdatePageHandlerFactory> factory_receiver_{this};
  mojo::Receiver<mojom::UpdatePageHandler> handler_receiver_{this};
  mojo::Remote<mojom::UpdatePage> page_;
  base::WeakPtrFactory<RoamuxUpdateService> weak_factory_{this};
};

}  // namespace roamux::updates

#endif  // ROAMUX_BROWSER_UPDATES_ROAMUX_UPDATE_SERVICE_H_
