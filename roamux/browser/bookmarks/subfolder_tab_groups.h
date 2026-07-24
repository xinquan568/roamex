// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_BOOKMARKS_SUBFOLDER_TAB_GROUPS_H_
#define ROAMUX_BROWSER_BOOKMARKS_SUBFOLDER_TAB_GROUPS_H_

#include <string>
#include <vector>

#include "url/gurl.h"

class Browser;

namespace bookmarks {
class BookmarkNode;
}

namespace roamux {

// roam-208: one planned tab group — a qualifying subfolder's exact title and
// its first-level links in bookmark order. Snapshotted: no BookmarkNode is
// retained across the (asynchronous) confirmation prompt.
struct SubfolderGroupPlan {
  SubfolderGroupPlan();
  SubfolderGroupPlan(std::u16string title, std::vector<GURL> urls);
  SubfolderGroupPlan(const SubfolderGroupPlan&);
  SubfolderGroupPlan(SubfolderGroupPlan&&);
  SubfolderGroupPlan& operator=(const SubfolderGroupPlan&);
  SubfolderGroupPlan& operator=(SubfolderGroupPlan&&);
  ~SubfolderGroupPlan();

  std::u16string title;
  std::vector<GURL> urls;
};

// The single qualifying-subfolder enumeration: one plan per immediate child
// folder of `folder` holding at least one first-level link, in bookmark
// order. Feeds the label's N, the row's visibility, and execution alike.
std::vector<SubfolderGroupPlan> BuildSubfolderGroupPlans(
    const bookmarks::BookmarkNode& folder);

// Opens one new browser window in `source`'s profile and materializes each
// plan as a named tab group (exact titles; colors assigned by the tab strip).
// Shows the aggregate confirmation first when the total link count reaches
// the bookmark-open prompt threshold.
void OpenSubfolderGroupsInNewWindow(Browser* source,
                                    std::vector<SubfolderGroupPlan> plans);

// Test seam for the aggregate prompt: when set, replaces the dialog and
// returns the canned answer. `message` is the exact dialog text production
// would show. Returns the previous callback.
using BulkOpenPromptCallback = bool (*)(size_t total_urls,
                                        const std::u16string& message);
BulkOpenPromptCallback SetBulkOpenPromptCallbackForTesting(
    BulkOpenPromptCallback callback);

}  // namespace roamux

#endif  // ROAMUX_BROWSER_BOOKMARKS_SUBFOLDER_TAB_GROUPS_H_
