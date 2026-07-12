// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/signin/signin_inert.h"

#include "base/feature_list.h"
#include "roamux/common/roamux_features.h"

namespace roamux::signin {

namespace {
bool g_intercepted = false;
bool g_explanation_shown = false;
bool g_dialog_suppressed = false;
}  // namespace

bool ShouldInterceptSigninForState(bool brave_style_profiles_enabled,
                                   BuildState state) {
  return brave_style_profiles_enabled && state != BuildState::kKeyedAuthorized;
}

bool ShouldInterceptInertSignin() {
  return ShouldInterceptSigninForState(
      base::FeatureList::IsEnabled(roamux::features::kBraveStyleProfiles),
      GetBuildState());
}

std::u16string GetInertSigninExplanation(BuildState state) {
  switch (state) {
    case BuildState::kKeyless:
      return u"This Roamux build ships without Google API keys, so Google "
             u"sign-in and sync cannot run. Profiles stay local and "
             u"isolated; no account is needed.";
    case BuildState::kKeyedUnauthorized:
      return u"This build has API keys but no Google authorization for "
             u"sign-in/sync, so Google sign-in cannot mint tokens. Profiles "
             u"stay local and isolated.";
    case BuildState::kKeyedAuthorized:
      return std::u16string();
  }
}

void RecordInertSigninIntercepted() {
  g_intercepted = true;
}
bool WasInertSigninInterceptedForTesting() {
  return g_intercepted;
}
void RecordInertExplanationShown() {
  g_explanation_shown = true;
}
bool WasInertExplanationShownForTesting() {
  return g_explanation_shown;
}
void ResetInertSigninTestState() {
  g_intercepted = false;
  g_explanation_shown = false;
}
void SuppressInertExplanationDialogForTesting() {
  g_dialog_suppressed = true;
}
void UnsuppressInertExplanationDialogForTesting() {
  g_dialog_suppressed = false;
}
bool IsInertExplanationDialogSuppressedForTesting() {
  return g_dialog_suppressed;
}

}  // namespace roamux::signin
