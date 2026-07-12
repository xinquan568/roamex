// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/scheme/roamux_scheme_rewrite.h"

#include <string_view>

#include "base/feature_list.h"
#include "content/public/common/url_constants.h"
#include "roamux/common/roamux_features.h"
#include "roamux/common/roamux_url_constants.h"
#include "url/gurl.h"

namespace roamux {

namespace {

// The curated alias map (roam-91). Adding a branding alias is a row here plus
// its unit/browser test rows — never new patch surface. Unlisted hosts keep
// the handled-scheme no-commit dead-end (patch 0028 lists "roamux" in
// ProfileIOData::IsHandledProtocol, so they are dropped rather than handed to
// the OS external-protocol path), and a generic roamux://X → chrome://X
// rewrite is deliberately NOT offered (no shadowing of real chrome:// hosts).
struct AliasRow {
  std::string_view roamux_host;
  std::string_view chrome_host;
};
constexpr AliasRow kAliasMap[] = {
    {kRoamuxAliasAboutHost, kChromeUIRoamuxAboutHost}, // roamux://about
    {kRoamuxAliasFlagsHost, "flags"},                  // roamux://flags
};

} // namespace

bool MaybeRewriteRoamuxAliasURL(GURL *url,
                                content::BrowserContext *browser_context) {
  // Checked per call (not at registration) so feature toggles behave
  // predictably; the handler itself is registered unconditionally.
  if (!base::FeatureList::IsEnabled(features::kRoamuxSchemeAlias)) {
    return false;
  }
  if (!url->SchemeIs(kRoamuxScheme)) {
    return false;
  }
  for (const AliasRow &row : kAliasMap) {
    if (url->GetHost() == row.roamux_host) {
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

} // namespace roamux
