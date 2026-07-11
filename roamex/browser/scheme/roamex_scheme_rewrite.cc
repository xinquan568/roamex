// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/scheme/roamex_scheme_rewrite.h"

#include <string_view>

#include "base/feature_list.h"
#include "content/public/common/url_constants.h"
#include "roamex/common/roamex_features.h"
#include "roamex/common/roamex_url_constants.h"
#include "url/gurl.h"

namespace roamex {

namespace {

// The curated alias map (roam-91). Adding a branding alias is a row here plus
// its unit/browser test rows — never new patch surface. Unlisted hosts keep
// the handled-scheme no-commit dead-end (patch 0028 lists "roamex" in
// ProfileIOData::IsHandledProtocol, so they are dropped rather than handed to
// the OS external-protocol path), and a generic roamex://X → chrome://X
// rewrite is deliberately NOT offered (no shadowing of real chrome:// hosts).
struct AliasRow {
  std::string_view roamex_host;
  std::string_view chrome_host;
};
constexpr AliasRow kAliasMap[] = {
    {kRoamexAliasAboutHost, kChromeUIRoamexAboutHost}, // roamux://about
    {kRoamexAliasFlagsHost, "flags"},                  // roamux://flags
};

} // namespace

bool MaybeRewriteRoamexAliasURL(GURL *url,
                                content::BrowserContext *browser_context) {
  // Checked per call (not at registration) so feature toggles behave
  // predictably; the handler itself is registered unconditionally.
  if (!base::FeatureList::IsEnabled(features::kRoamexSchemeAlias)) {
    return false;
  }
  if (!url->SchemeIs(kRoamexScheme)) {
    return false;
  }
  for (const AliasRow &row : kAliasMap) {
    if (url->GetHost() == row.roamex_host) {
      GURL::Replacements replacements;
      replacements.SetSchemeStr(content::kChromeUIScheme);
      replacements.SetHostStr(row.chrome_host);
      *url = url->ReplaceComponents(replacements);
      break;
    }
  }
  // Chaining idiom (browser_about_handler.cc precedent): having re-written the
  // URL, let the later chrome: handlers process it.
  return false;
}

} // namespace roamex
