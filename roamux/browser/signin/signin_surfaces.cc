// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/signin/signin_surfaces.h"

#include "base/command_line.h"
#include "base/feature_list.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_prefs.h"
#include "roamux/common/roamux_switches.h"

namespace roamux::signin {

bool IsSigninAllowedOnNextStartup(const PrefService* prefs) {
  const bool upstream = prefs->GetBoolean(::prefs::kSigninAllowedOnNextStartup);
  if (!base::FeatureList::IsEnabled(roamux::features::kBraveStyleProfiles)) {
    return upstream;
  }
  const bool opted_in =
      prefs->GetBoolean(roamux::prefs::kSigninOptionalEntryPoint) ||
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          roamux::switches::kSigninOptIn);
  return opted_in && upstream;
}

}  // namespace roamux::signin
