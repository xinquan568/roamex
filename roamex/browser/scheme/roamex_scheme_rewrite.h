// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_BROWSER_SCHEME_ROAMEX_SCHEME_REWRITE_H_
#define ROAMEX_BROWSER_SCHEME_ROAMEX_SCHEME_REWRITE_H_

class GURL;

namespace content {
class BrowserContext;
} // namespace content

namespace roamex {

// content::BrowserURLHandler forward rewriter (roam-91): rewrites the curated
// alias map roamux://about → chrome://roamux-about and roamux://flags →
// chrome://flags when features::kRoamexSchemeAlias is enabled (checked per
// call, so ScopedFeatureList toggles behave predictably). Follows the upstream
// rewrite-then-return-false chaining idiom
// (chrome/browser/browser_about_handler.cc): the URL is mutated in place and
// the function ALWAYS returns false so later handlers (HandleWebUI et al.)
// process the rewritten chrome:// URL. Unmapped roamex:// hosts and non-roamex
// schemes are never touched. Registered by patch 0028 in
// ChromeContentBrowserClient::BrowserURLHandlerCreated.
bool MaybeRewriteRoamexAliasURL(GURL *url,
                                content::BrowserContext *browser_context);

} // namespace roamex

#endif // ROAMEX_BROWSER_SCHEME_ROAMEX_SCHEME_REWRITE_H_
