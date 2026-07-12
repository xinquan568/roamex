// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/signin/roamux_signin_build_state.h"

#include "google_apis/google_api_keys.h"
#include "roamux/browser/signin/signin_buildflags.h"

namespace roamux::signin {

BuildState GetBuildStateForInputs(bool has_api_keys, bool authorized_build) {
  if (!has_api_keys) {
    return BuildState::kKeyless;
  }
  return authorized_build ? BuildState::kKeyedAuthorized
                          : BuildState::kKeyedUnauthorized;
}

BuildState GetBuildState() {
  return GetBuildStateForInputs(google_apis::HasAPIKeyConfigured(),
                                BUILDFLAG(ROAMUX_SIGNIN_AUTHORIZED_BUILD));
}

bool IsSigninInert() { return GetBuildState() != BuildState::kKeyedAuthorized; }

} // namespace roamux::signin
