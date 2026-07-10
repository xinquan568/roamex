// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_SIGNIN_SIGNIN_SURFACES_H_
#define ROAMEX_BROWSER_SIGNIN_SIGNIN_SURFACES_H_

class PrefService;

// Default sign-in surface suppression (E5, roam-30). The
// AccountConsistencyModeManager startup derivation (patch 0023) consumes this
// in place of its raw kSigninAllowedOnNextStartup read; the derived
// kSigninAllowed write then propagates to every §7.3 surface (profile menu,
// settings People/sync, FRE, promos, avatar bubble).
namespace roamex::signin {

// Flag off: the upstream prefs::kSigninAllowedOnNextStartup value, untouched.
// Flag on: roamex.signin.optional_entry_point_enabled AND the upstream value —
// the opt-in pref is authoritative (default off ⇒ suppressed) while a managed
// or user false still wins; takes effect per Chromium's restart semantics
// (ACMM derives once per profile startup).
bool IsSigninAllowedOnNextStartup(const PrefService* prefs);

}  // namespace roamex::signin

#endif  // ROAMEX_BROWSER_SIGNIN_SIGNIN_SURFACES_H_
