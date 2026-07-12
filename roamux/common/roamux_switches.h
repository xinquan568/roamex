// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_COMMON_ROAMUX_SWITCHES_H_
#define ROAMUX_COMMON_ROAMUX_SWITCHES_H_

// Roamux command-line switches (header-only constants).
namespace roamux::switches {

// Developer mirror of roamux.signin.optional_entry_point_enabled (roam-31):
// set by the chrome://flags entry; OR-ed with the pref by
// roamux::signin::IsSigninAllowedOnNextStartup. roamux://flags aliasing is
// owed by the product-wide branding epic.
inline constexpr char kSigninOptIn[] = "roamux-signin-opt-in";

}  // namespace roamux::switches

#endif  // ROAMUX_COMMON_ROAMUX_SWITCHES_H_
