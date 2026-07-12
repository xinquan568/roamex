// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UI_SIGNIN_SIGNIN_INERT_DIALOG_H_
#define ROAMUX_BROWSER_UI_SIGNIN_SIGNIN_INERT_DIALOG_H_

class Profile;

// Chrome-facing shim for the roam-31 inert gate, compiled into
// //chrome/browser/signin:impl via patch 0024 (the 0009/0015 idiom — a
// roamux-owned file inside the upstream target, so it may use chrome/browser
// UI headers without a GN cycle).
namespace roamux::signin_ui {

// Returns true when sign-in initiation must stop (kBraveStyleProfiles on and
// the build is keyless / keyed-but-unauthorized): shows the state-specific
// explanation dialog (unless suppressed for testing) and records it.
// Returns false on keyed-and-authorized builds or flag-off (pass-through).
bool MaybeInterceptInertSignin(Profile* profile);

}  // namespace roamux::signin_ui

#endif  // ROAMUX_BROWSER_UI_SIGNIN_SIGNIN_INERT_DIALOG_H_
