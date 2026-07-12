// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_SIGNIN_SIGNIN_INERT_H_
#define ROAMUX_BROWSER_SIGNIN_SIGNIN_INERT_H_

#include <string>

#include "roamux/browser/signin/roamux_signin_build_state.h"

// Inert-with-explanation (E5, roam-31): with kBraveStyleProfiles on, sign-in
// initiation on keyless / keyed-but-unauthorized builds is intercepted at the
// signin_ui_util funnel (patch 0024) and explained — never a silent dead
// button. Keyed-and-authorized builds pass through to the standard flow.
namespace roamux::signin {

// Pure decision seam (unit-testable with injected inputs).
bool ShouldInterceptSigninForState(bool brave_style_profiles_enabled,
                                   BuildState state);

// The real build's decision: feature flag AND IsSigninInert().
bool ShouldInterceptInertSignin();

// State-specific, user-visible explanation. Empty for kKeyedAuthorized.
std::u16string GetInertSigninExplanation(BuildState state);

// Observability + dialog control for browser tests (the dialog shim compiled
// into //chrome/browser/signin:impl records via these). "Intercepted" counts
// every blocked initiation; "shown" only those where the explanation dialog
// was actually displayed (or would have been, when suppressed for testing).
void RecordInertSigninIntercepted();
bool WasInertSigninInterceptedForTesting();
void RecordInertExplanationShown();
bool WasInertExplanationShownForTesting();
void ResetInertSigninTestState();
void SuppressInertExplanationDialogForTesting();
void UnsuppressInertExplanationDialogForTesting();
bool IsInertExplanationDialogSuppressedForTesting();

}  // namespace roamux::signin

#endif  // ROAMUX_BROWSER_SIGNIN_SIGNIN_INERT_H_
