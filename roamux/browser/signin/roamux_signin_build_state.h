// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_SIGNIN_ROAMUX_SIGNIN_BUILD_STATE_H_
#define ROAMUX_BROWSER_SIGNIN_ROAMUX_SIGNIN_BUILD_STATE_H_

// The three §7.3/§12.3 build states. Keys are necessary but NOT sufficient:
// functional sign-in also requires the distributor's compiled-in authorization
// (roamux_signin_authorized_build).
namespace roamux::signin {

enum class BuildState {
  kKeyless, // (1) no Google API keys — sign-in surfaces inert-with-explanation
  kKeyedUnauthorized, // (2) keys present, no authorization — still
                      // inert-with-explanation
  kKeyedAuthorized, // (3) keys + compiled-in authorization — the standard flow
                    // may run
};

// The decision table over explicit inputs — the testable seam.
BuildState GetBuildStateForInputs(bool has_api_keys, bool authorized_build);

// The real build's state: google_apis key configuration x the compiled-in
// authorization flag.
BuildState GetBuildState();

// True unless the build is keyed-and-authorized — the E5 surfaces render inert
// when true.
bool IsSigninInert();

} // namespace roamux::signin

#endif // ROAMUX_BROWSER_SIGNIN_ROAMUX_SIGNIN_BUILD_STATE_H_
