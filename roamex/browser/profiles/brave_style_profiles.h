// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_PROFILES_BRAVE_STYLE_PROFILES_H_
#define ROAMEX_BROWSER_PROFILES_BRAVE_STYLE_PROFILES_H_

// Brave-style profiles (E5, roam-29): with kBraveStyleProfiles on, profile
// creation is name-only — the picker's sign-in/type-choice step is suppressed
// and creation goes straight to the local (name + avatar/theme) path.
namespace roamex::profiles {

// True iff roamex::features::kBraveStyleProfiles is enabled.
bool IsNameOnlyProfileCreationEnabled();

// The decision seam consumed by the profile_picker_ui.cc hook (patch 0022):
// the sign-in creation step is offered iff upstream allows it AND the feature
// is off. Flag-off is a pure pass-through of `upstream_allowed`.
bool AllowSigninProfileCreationStep(bool upstream_allowed);

}  // namespace roamex::profiles

#endif  // ROAMEX_BROWSER_PROFILES_BRAVE_STYLE_PROFILES_H_
