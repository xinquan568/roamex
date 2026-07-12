// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_IMPORTER_EDGE_IMPORT_ADAPTER_H_
#define ROAMUX_BROWSER_IMPORTER_EDGE_IMPORT_ADAPTER_H_

#include <memory>
#include <optional>

#include "base/files/file_path.h"
#include "base/version.h"
#include "roamux/browser/importer/edge_import_types.h"

namespace roamux {

// Reads the browser version of the last Edge instance that ran, from the
// Chromium `Last Version` plain-text file at the User-Data root (a sibling of
// `Default/`). Returns nullopt if the file is missing or unparseable — the
// caller then treats the source version as undetermined and best-efforts with
// the newest adapter. `Last Version` is Chromium's canonical last-run version
// record and is a more reliable milestone source than `Local State` (whose JSON
// does not dependably carry the browser version).
std::optional<base::Version> DetectEdgeVersion(
    const base::FilePath& user_data_dir);

// The versioned Edge import adapter (roam-19 / I-3.5). It resolves the Edge
// User-Data / profile layout, detects the source version, decides whether that
// version is within the milestone family this build supports (Edge 150.x), and
// answers per-carrier source availability / schema-presence — the "schema
// mismatch" signal. Constructing it never touches the destination and never
// mutates the source. A single concrete adapter targets the 150.x family today;
// the class is the seam a future Edge schema break plugs a new adapter into.
class EdgeImportAdapter {
 public:
  // The Edge major-milestone family this build's adapter targets.
  static constexpr uint32_t kSupportedMilestone = 150;

  // Locates `<app_data_root>/Microsoft Edge` (+ `/Default`), detects the
  // version, and returns an adapter. Always returns a non-null adapter — even
  // when the version is undetermined or outside the supported family — so the
  // import flow can best-effort and report, rather than aborting.
  static std::unique_ptr<EdgeImportAdapter> Detect(
      const base::FilePath& app_data_root);

  EdgeImportAdapter(const EdgeImportAdapter&) = delete;
  EdgeImportAdapter& operator=(const EdgeImportAdapter&) = delete;
  ~EdgeImportAdapter();

  const std::optional<base::Version>& version() const { return version_; }
  bool version_supported() const { return version_supported_; }
  const base::FilePath& user_data_dir() const { return user_data_dir_; }
  const base::FilePath& profile_dir() const { return profile_dir_; }

  // True iff the source profile has the on-disk artifact this carrier imports
  // from, in the shape the 150.x adapter expects. False = absent or
  // schema-mismatched (the coordinator reports the carrier kUnsupported).
  bool CarrierAvailable(EdgeCarrier carrier) const;

 private:
  EdgeImportAdapter(base::FilePath user_data_dir,
                    base::FilePath profile_dir,
                    std::optional<base::Version> version,
                    bool version_supported);

  const base::FilePath user_data_dir_;
  const base::FilePath profile_dir_;
  const std::optional<base::Version> version_;
  const bool version_supported_;
};

}  // namespace roamux

#endif  // ROAMUX_BROWSER_IMPORTER_EDGE_IMPORT_ADAPTER_H_
