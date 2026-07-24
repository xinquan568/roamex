// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_COMMON_ROAMUX_FEATURES_H_
#define ROAMUX_COMMON_ROAMUX_FEATURES_H_

#include "base/feature_list.h"

// Per-feature base::Feature flags — each ships DISABLED by default until its
// epic completes (plan P3), then flips on with the flag kept as a kill-switch
// (first: kRoamuxSchemeAlias, roam-179).
namespace roamux::features {

BASE_DECLARE_FEATURE(kTabStripPosition); // E1 — configurable tab-strip position
BASE_DECLARE_FEATURE(kInitialUrl);       // E2 — per-tab initial URL
BASE_DECLARE_FEATURE(kEdgeImport);       // E3 — Microsoft Edge import
BASE_DECLARE_FEATURE(kBookmarkSubfolderGroups);  // roam-208 — bookmarks: subfolders as tab groups
                                         // (roam-190; SHIPS ENABLED,
                                         // kill-switch)
BASE_DECLARE_FEATURE(kTabVisitNav);      // E4 — tab visit-order navigation
BASE_DECLARE_FEATURE(
    kBraveStyleProfiles); // E5 — Brave-style profiles + hidden optional sign-in
BASE_DECLARE_FEATURE(
    kRoamuxSchemeAlias); // E8 — roamux:// scheme alias + display branding
                         // (roam-91/roam-179; SHIPS ENABLED, kill-switch)
BASE_DECLARE_FEATURE(
    kRoamuxExternalOpenProfile); // roam-213 — designated profile for external
                                 // opens (Finder files / links from other apps)
BASE_DECLARE_FEATURE(
    kTabStripToggleShortcut); // roam-214 — pin/peek + collapse/expand shortcut
                              // for the vertical tab strip

} // namespace roamux::features

#endif // ROAMUX_COMMON_ROAMUX_FEATURES_H_
