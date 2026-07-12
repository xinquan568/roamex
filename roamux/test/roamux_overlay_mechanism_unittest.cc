// SPDX-License-Identifier: Apache-2.0
// Proves the §12.2 integration mechanisms are live in this build (roam-2):
//   mechanism 2 - a roamux/chromium_src full-file override shadows an upstream
//   header; mechanism 3 - a runhook-applied one-line upstream patch.
// Both samples are inert markers; they change no browser behavior.

#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

TEST(RoamuxOverlayMechanismTest, ChromiumSrcOverrideShadowsUpstreamHeader) {
#if defined(ROAMUX_CHROMIUM_SRC_OVERRIDE_ACTIVE)
  SUCCEED();
#else
  FAIL() << "roamux/chromium_src/chrome/common/chrome_isolated_world_ids.h did "
            "not shadow upstream "
            "- is patches/0002-chromium-src-include-redirect.patch applied "
            "(roamux/build/apply_patches.py)?";
#endif
}

TEST(RoamuxOverlayMechanismTest, SamplePatchApplied) {
#if defined(ROAMUX_SAMPLE_PATCH_ACTIVE)
  SUCCEED();
#else
  FAIL() << "chrome/common/chrome_constants.h lacks ROAMUX_SAMPLE_PATCH_ACTIVE "
            "- run "
            "roamux/build/apply_patches.py --chromium-src <src>.";
#endif
}

} // namespace
