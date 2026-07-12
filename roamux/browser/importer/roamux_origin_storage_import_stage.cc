// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/importer/roamux_origin_storage_import_stage.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "components/services/storage/public/mojom/local_storage_control.mojom.h"
#include "content/public/browser/storage_partition.h"
#include "roamux/browser/importer/edge_local_storage_reader.h"

namespace roamux {

RoamuxOriginStorageImportStage::RoamuxOriginStorageImportStage(
    base::FilePath source_path,
    Profile* profile)
    : source_path_(std::move(source_path)), profile_(profile) {}

RoamuxOriginStorageImportStage::~RoamuxOriginStorageImportStage() = default;

void RoamuxOriginStorageImportStage::Import(
    base::OnceCallback<void(size_t accepted)> done) {
  done_ = std::move(done);
  // The LevelDB read + copy-to-temp is blocking file I/O — run it on a
  // MayBlock pool, then apply the writes back on the UI thread (mojo Remotes
  // are UI-thread-bound).
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadEdgeLocalStorage, source_path_),
      base::BindOnce(&RoamuxOriginStorageImportStage::OnRead,
                     base::Unretained(this)));
}

void RoamuxOriginStorageImportStage::OnRead(
    std::vector<OriginLocalStorage> origins) {
  storage::mojom::LocalStorageControl* control =
      profile_->GetDefaultStoragePartition()->GetLocalStorageControl();

  // Count total Puts first so the completion check is race-free even if the
  // first Put replies synchronously.
  for (const OriginLocalStorage& origin : origins) {
    pending_ += origin.entries.size();
  }
  if (pending_ == 0) {
    std::move(done_).Run(0);
    return;
  }

  for (const OriginLocalStorage& origin : origins) {
    mojo::Remote<blink::mojom::StorageArea> area;
    control->BindStorageArea(origin.storage_key,
                             area.BindNewPipeAndPassReceiver());
    for (const LocalStorageEntry& entry : origin.entries) {
      area->Put(entry.key, entry.value, /*client_old_value=*/std::nullopt,
                /*source=*/nullptr,
                base::BindOnce(&RoamuxOriginStorageImportStage::OnPutComplete,
                               base::Unretained(this)));
    }
    areas_.push_back(std::move(area));
  }
}

void RoamuxOriginStorageImportStage::OnPutComplete(bool success) {
  if (success) {
    ++accepted_;
  }
  if (--pending_ == 0) {
    areas_.clear();
    std::move(done_).Run(accepted_);
  }
}

}  // namespace roamux
