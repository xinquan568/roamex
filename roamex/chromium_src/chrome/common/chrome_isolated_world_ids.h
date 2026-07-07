// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_CHROME_ISOLATED_WORLD_IDS_H_
#define CHROME_COMMON_CHROME_ISOLATED_WORLD_IDS_H_

// Roamex (roam-2): full-file override sample proving the chromium_src include-redirect
// (SPDX-License-Identifier: Apache-2.0 for the Roamex delta; upstream content BSD-licensed).
#define ROAMEX_CHROMIUM_SRC_OVERRIDE_ACTIVE 1

#include "build/build_config.h"
#include "content/public/common/isolated_world_ids.h"
// LINT.IfChange
// When changing this file, ChromeIsolatedWorldIds.java must be updated as well.
enum ChromeIsolatedWorldIDs {
  // Isolated world ID for Chrome Translate.
  ISOLATED_WORLD_ID_TRANSLATE = content::ISOLATED_WORLD_ID_CONTENT_END + 1,

  // Isolated world ID for Indigo.
  ISOLATED_WORLD_ID_INDIGO,

  // Isolated world ID for internal Chrome features.
  ISOLATED_WORLD_ID_CHROME_INTERNAL,

#if BUILDFLAG(IS_MAC)
  // Isolated world ID for AppleScript.
  ISOLATED_WORLD_ID_APPLESCRIPT,
#endif  // BUILDFLAG(IS_MAC)

  // Numbers for isolated worlds for extensions are set in
  // extensions/renderer/script_injection.cc, and are are greater than or equal
  // to this number.
  ISOLATED_WORLD_ID_EXTENSIONS
};
// LINT.ThenChange(//chrome/android/java/src/org/chromium/chrome/browser/common/ChromeIsolatedWorldIds.java)
#endif  // CHROME_COMMON_CHROME_ISOLATED_WORLD_IDS_H_
