// SPDX-License-Identifier: Apache-2.0
#include "roamux/common/roamux_features.h"

namespace roamux::features {

BASE_FEATURE(kTabStripPosition, "RoamuxTabStripPosition",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kInitialUrl, "RoamuxInitialUrl",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kEdgeImport, "RoamuxEdgeImport",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kTabVisitNav, "RoamuxTabVisitNav",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kBraveStyleProfiles, "RoamuxBraveStyleProfiles",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kRoamuxSchemeAlias, "RoamuxSchemeAlias",
             base::FEATURE_DISABLED_BY_DEFAULT);

} // namespace roamux::features
