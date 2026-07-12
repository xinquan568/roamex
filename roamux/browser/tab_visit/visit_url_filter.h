// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_TAB_VISIT_VISIT_URL_FILTER_H_
#define ROAMUX_BROWSER_TAB_VISIT_VISIT_URL_FILTER_H_

class GURL;

namespace roamux::tab_visit {

// True if `url` is a real, committed destination worth recording as a settled
// visit (roam-23 / I-4.3). Empty, invalid, about:blank, and the New Tab Page
// (chrome://newtab, chrome://new-tab-page, chrome://new-tab-page-third-party,
// chrome-search://) are NOT recordable. The observer bridge calls this BEFORE
// SettledVisitJournalService::RecordVisit, which appends url.spec()
// unconditionally (F2). Kept in the pure //roamux/browser/tab_visit source_set
// (depends only on //url) so the gating is unit-testable without the UI layer.
bool IsRecordableVisitUrl(const GURL& url);

}  // namespace roamux::tab_visit

#endif  // ROAMUX_BROWSER_TAB_VISIT_VISIT_URL_FILTER_H_
