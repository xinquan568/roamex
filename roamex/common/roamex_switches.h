// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_COMMON_ROAMEX_SWITCHES_H_
#define ROAMEX_COMMON_ROAMEX_SWITCHES_H_

// Roamex command-line switches (header-only constants).
namespace roamex::switches {

// Developer mirror of roamex.signin.optional_entry_point_enabled (roam-31):
// set by the chrome://flags entry; OR-ed with the pref by
// roamex::signin::IsSigninAllowedOnNextStartup. roamex://flags aliasing is
// owed by the product-wide branding epic.
inline constexpr char kSigninOptIn[] = "roamex-signin-opt-in";

}  // namespace roamex::switches

#endif  // ROAMEX_COMMON_ROAMEX_SWITCHES_H_
