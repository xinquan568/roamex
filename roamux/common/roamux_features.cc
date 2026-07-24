// SPDX-License-Identifier: Apache-2.0
#include "roamux/common/roamux_features.h"

namespace roamux::features {

BASE_FEATURE(kTabStripPosition, "RoamuxTabStripPosition",
             base::FEATURE_ENABLED_BY_DEFAULT);  // roam-185: shipped default-on
BASE_FEATURE(kInitialUrl, "RoamuxInitialUrl",
             base::FEATURE_ENABLED_BY_DEFAULT);  // roam-187: shipped default-on
BASE_FEATURE(kEdgeImport, "RoamuxEdgeImport",
             base::FEATURE_ENABLED_BY_DEFAULT);  // roam-190: shipped default-on
// roam-208: ships default-OFF; flip-on is a post-verification follow-up.
BASE_FEATURE(kBookmarkSubfolderGroups,
             "RoamuxBookmarkSubfolderGroups",
             base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kTabVisitNav, "RoamuxTabVisitNav",
             base::FEATURE_ENABLED_BY_DEFAULT);  // roam-189: shipped default-on
BASE_FEATURE(kBraveStyleProfiles, "RoamuxBraveStyleProfiles",
             base::FEATURE_DISABLED_BY_DEFAULT);
// roam-179: ENABLED by default — the E8 rebrand's user-visible scheme
// branding ships on (D3); the flag stays as a kill-switch (flag-off identity
// is pinned at the browser level).
BASE_FEATURE(kRoamuxSchemeAlias, "RoamuxSchemeAlias",
             base::FEATURE_ENABLED_BY_DEFAULT);
// roam-213: ships default-OFF; flip-on is a post-verification follow-up.
BASE_FEATURE(kRoamuxExternalOpenProfile,
             "RoamuxExternalOpenProfile",
             base::FEATURE_DISABLED_BY_DEFAULT);
// roam-214: ships default-OFF; flip-on is a post-verification follow-up.
BASE_FEATURE(kTabStripToggleShortcut,
             "RoamuxTabStripToggleShortcut",
             base::FEATURE_DISABLED_BY_DEFAULT);

} // namespace roamux::features
