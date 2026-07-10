// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/signin/signin_surfaces.h"

#include "base/command_line.h"
#include "base/feature_list.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "roamex/common/roamex_features.h"
#include "roamex/common/roamex_prefs.h"
#include "roamex/common/roamex_switches.h"

namespace roamex::signin {

bool IsSigninAllowedOnNextStartup(const PrefService* prefs) {
  const bool upstream = prefs->GetBoolean(::prefs::kSigninAllowedOnNextStartup);
  if (!base::FeatureList::IsEnabled(roamex::features::kBraveStyleProfiles)) {
    return upstream;
  }
  const bool opted_in =
      prefs->GetBoolean(roamex::prefs::kSigninOptionalEntryPoint) ||
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          roamex::switches::kSigninOptIn);
  return opted_in && upstream;
}

}  // namespace roamex::signin
