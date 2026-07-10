// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/profiles/brave_style_profiles.h"

#include "base/feature_list.h"
#include "roamex/common/roamex_features.h"

namespace roamex::profiles {

bool IsNameOnlyProfileCreationEnabled() {
  return base::FeatureList::IsEnabled(roamex::features::kBraveStyleProfiles);
}

bool AllowSigninProfileCreationStep(bool upstream_allowed) {
  return upstream_allowed && !IsNameOnlyProfileCreationEnabled();
}

}  // namespace roamex::profiles
