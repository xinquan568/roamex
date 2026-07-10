// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/signin/signin_surfaces.h"

#include "base/feature_list.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "roamex/common/roamex_features.h"
#include "roamex/common/roamex_prefs.h"

namespace roamex::signin {

bool IsSigninAllowedOnNextStartup(const PrefService* prefs) {
  const bool upstream = prefs->GetBoolean(::prefs::kSigninAllowedOnNextStartup);
  if (!base::FeatureList::IsEnabled(roamex::features::kBraveStyleProfiles)) {
    return upstream;
  }
  return prefs->GetBoolean(roamex::prefs::kSigninOptionalEntryPoint) &&
         upstream;
}

}  // namespace roamex::signin
