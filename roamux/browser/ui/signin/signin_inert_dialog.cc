// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/ui/signin/signin_inert_dialog.h"

#include <memory>
#include <utility>

#include "base/functional/callback_helpers.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "components/constrained_window/constrained_window_views.h"
#include "roamux/browser/signin/roamux_signin_build_state.h"
#include "roamux/browser/signin/signin_inert.h"
#include "ui/base/models/dialog_model.h"

namespace roamux::signin_ui {

bool MaybeInterceptInertSignin(Profile* profile) {
  if (!roamux::signin::ShouldInterceptInertSignin()) {
    return false;
  }
  roamux::signin::RecordInertSigninIntercepted();

  if (roamux::signin::IsInertExplanationDialogSuppressedForTesting()) {
    roamux::signin::RecordInertExplanationShown();
    return true;
  }

  // Anchor strictly to the initiating profile — never another profile's
  // window. Every gated entry point is user-reachable only from a browser
  // window of that profile; a windowless (programmatic) call is intercepted
  // without a dialog and is observable via
  // WasInertSigninInterceptedForTesting.
  Browser* browser =
      profile ? chrome::FindBrowserWithProfile(profile) : nullptr;
  if (browser && browser->window()) {
    ui::DialogModel::Builder builder;
    builder.SetInternalName("RoamuxSigninInertDialog")
        .SetTitle(u"Google sign-in isn't available in this build")
        .AddParagraph(
            ui::DialogModelLabel(roamux::signin::GetInertSigninExplanation(
                roamux::signin::GetBuildState())))
        .AddOkButton(base::DoNothing());
    constrained_window::ShowBrowserModal(builder.Build(),
                                         browser->window()->GetNativeWindow());
    roamux::signin::RecordInertExplanationShown();
  }
  return true;
}

}  // namespace roamux::signin_ui
