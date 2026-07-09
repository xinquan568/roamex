// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_IMPORTER_EDGE_IMPORT_TYPES_H_
#define ROAMEX_BROWSER_IMPORTER_EDGE_IMPORT_TYPES_H_

#include <array>

namespace roamex {

// The browser-side Edge import carriers roam-19's coordinator orchestrates. The
// utility-process non-secret items (history/bookmarks/search/autofill) are out
// of scope here (roam-15's importer + roam-20's production wiring own those).
enum class EdgeCarrier {
  kPasswords,
  kCookies,
  kLocalStorage,
  kIndexedDb,
};

// Stable, order-preserving list of every browser-side carrier (the coordinator
// attempts them in this order; secret carriers first, then origin storage).
inline constexpr std::array<EdgeCarrier, 4> kAllEdgeCarriers = {
    EdgeCarrier::kPasswords,
    EdgeCarrier::kCookies,
    EdgeCarrier::kLocalStorage,
    EdgeCarrier::kIndexedDb,
};

// A short stable identifier for logging/reporting (never localized).
const char* EdgeCarrierName(EdgeCarrier carrier);

}  // namespace roamex

#endif  // ROAMEX_BROWSER_IMPORTER_EDGE_IMPORT_TYPES_H_
