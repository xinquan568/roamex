// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/profiles/brave_style_profiles.h"

#include "base/feature_list.h"
#include "roamux/common/roamux_features.h"

namespace roamux::profiles {

bool IsNameOnlyProfileCreationEnabled() {
  return base::FeatureList::IsEnabled(roamux::features::kBraveStyleProfiles);
}

bool AllowSigninProfileCreationStep(bool upstream_allowed) {
  return upstream_allowed && !IsNameOnlyProfileCreationEnabled();
}

}  // namespace roamux::profiles
