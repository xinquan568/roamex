// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_COMMON_ROAMEX_URL_CONSTANTS_H_
#define ROAMEX_COMMON_ROAMEX_URL_CONSTANTS_H_

namespace roamex {

// The product URL scheme ("roamux://…"), roam-91 (value rebranded by
// roam-93). Registered as a standard scheme and marked browser-handled by
// patch 0028 (which uses the annotated string literal at both hook sites — a
// unit test pins literal↔constant agreement).
inline constexpr char kRoamexScheme[] = "roamux";

// chrome:// host of the About WebUI (roam-37; moved here from
// roamex_about_ui.h by roam-91 so generic scheme code never deps the About
// WebUI target).
inline constexpr char kChromeUIRoamexAboutHost[] = "roamux-about";

// roamux:// alias hosts (roam-91): the curated alias map rewrites
// roamux://about → chrome://roamux-about and roamux://flags → chrome://flags.
inline constexpr char kRoamexAliasAboutHost[] = "about";
inline constexpr char kRoamexAliasFlagsHost[] = "flags";

} // namespace roamex

#endif // ROAMEX_COMMON_ROAMEX_URL_CONSTANTS_H_
