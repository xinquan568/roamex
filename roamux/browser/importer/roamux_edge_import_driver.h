// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_IMPORTER_ROAMUX_EDGE_IMPORT_DRIVER_H_
#define ROAMUX_BROWSER_IMPORTER_ROAMUX_EDGE_IMPORT_DRIVER_H_

#include <cstdint>
#include <memory>

#include "base/containers/flat_set.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "roamux/browser/importer/edge_import_report.h"
#include "roamux/browser/importer/edge_import_types.h"

class Profile;

namespace crypto::apple {
class KeychainV2;
}

namespace user_data_importer {
struct SourceProfile;
}

namespace roamux {

class RoamuxEdgeImportCoordinator;

// How a user-selected import `items` mask splits across the two halves of an
// Edge import: the non-secret items go to the utility-process importer
// (roam-15), and the browser-side carriers (passwords/cookies via roam-16,
// localStorage/IndexedDB via roam-17/18) go to the roam-19 coordinator. Secrets
// are NEVER handed to the utility process (roam-16 handles the Keychain
// browser-side).
struct EdgeImportItemsPlan {
  EdgeImportItemsPlan();
  EdgeImportItemsPlan(const EdgeImportItemsPlan&);
  EdgeImportItemsPlan(EdgeImportItemsPlan&&);
  ~EdgeImportItemsPlan();

  uint16_t utility_items = 0;
  base::flat_set<EdgeCarrier> carriers;
};

// Splits `items` (a bitmask of user_data_importer::ImportItem): strips
// PASSWORDS|COOKIES from `utility_items`, and derives the browser-side
// `carriers` (kPasswords/kCookies iff requested; localStorage + IndexedDB
// always — the E3 origin-storage carriers). Pure.
EdgeImportItemsPlan MakeEdgeImportItemsPlan(uint16_t items);

// The utility-import mask with secrets removed (==
// MakeEdgeImportItemsPlan(items) .utility_items). The host hook calls the
// SourceProfile overload, which masks ONLY for a Chromium-Edge source with the
// kEdgeImport feature enabled; any other source (or the feature off) is
// returned unchanged.
uint16_t MaskEdgeSecretItemsForUtility(uint16_t items);
uint16_t MaskEdgeSecretItemsForUtility(
    const user_data_importer::SourceProfile& source_profile,
    uint16_t items);

// The detected Edge SourceProfile::source_path is `<app_data_root>/Microsoft
// Edge/Default`; the coordinator wants `app_data_root` (it re-appends those two
// components). Returns `source_path.DirName().DirName()`.
base::FilePath AppDataRootFromEdgeProfilePath(
    const base::FilePath& source_path);

// The production browser-side Edge import driver (roam-20 / I-3.6). It is the
// production caller the roam-19 coordinator + roam-16 secret stage were built
// for: given the destination Profile, the Edge User-Data root, and the
// user-selected import items, it runs the browser-side carriers (secrets = the
// roam-16 carry-forward, plus origin storage) and reports. Flag-gated on
// kEdgeImport. Runs on the UI thread; the caller keeps it alive until `done`
// runs (the host hook keeps it alive via the completion callback).
class RoamuxEdgeImportDriver {
 public:
  RoamuxEdgeImportDriver(Profile* profile,
                         base::FilePath app_data_root,
                         uint16_t items,
                         crypto::apple::KeychainV2* keychain_for_testing);
  RoamuxEdgeImportDriver(const RoamuxEdgeImportDriver&) = delete;
  RoamuxEdgeImportDriver& operator=(const RoamuxEdgeImportDriver&) = delete;
  ~RoamuxEdgeImportDriver();

  void Start(base::OnceCallback<void(EdgeImportReport)> done);

 private:
  void OnCoordinatorDone(EdgeImportReport report);

  const raw_ptr<Profile> profile_;
  const base::FilePath app_data_root_;
  const uint16_t items_;
  const raw_ptr<crypto::apple::KeychainV2> keychain_for_testing_;

  std::unique_ptr<RoamuxEdgeImportCoordinator> coordinator_;
  base::OnceCallback<void(EdgeImportReport)> done_;

  base::WeakPtrFactory<RoamuxEdgeImportDriver> weak_factory_{this};
};

// The host hook (patch 0015). If `source_profile` is a Chromium-Edge source and
// kEdgeImport is enabled, starts a self-owned RoamuxEdgeImportDriver for the
// user-SELECTED `items` (the driver builds its own ProfileWriter for
// `target_profile`), runs `on_done` when the browser-side half finishes, and
// returns true. Otherwise makes no change and returns false. Called from the
// patched ExternalProcessImporterHost::NotifyImportEnded (once), so the utility
// import and the browser-side carriers land in the same profile and completion
// waits for both.
bool MaybeStartEdgeBrowserSideImport(
    const user_data_importer::SourceProfile& source_profile,
    Profile* target_profile,
    uint16_t items,
    base::OnceClosure on_done);

}  // namespace roamux

#endif  // ROAMUX_BROWSER_IMPORTER_ROAMUX_EDGE_IMPORT_DRIVER_H_
