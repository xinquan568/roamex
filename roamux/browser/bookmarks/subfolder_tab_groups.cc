// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/bookmarks/subfolder_tab_groups.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils_desktop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/page_transition_types.h"

namespace roamux {

namespace {
BulkOpenPromptCallback g_bulk_prompt_for_testing = nullptr;
}  // namespace

SubfolderGroupPlan::SubfolderGroupPlan() = default;
SubfolderGroupPlan::SubfolderGroupPlan(std::u16string title,
                                       std::vector<GURL> urls)
    : title(std::move(title)), urls(std::move(urls)) {}
SubfolderGroupPlan::SubfolderGroupPlan(const SubfolderGroupPlan&) = default;
SubfolderGroupPlan::SubfolderGroupPlan(SubfolderGroupPlan&&) = default;
SubfolderGroupPlan& SubfolderGroupPlan::operator=(const SubfolderGroupPlan&) =
    default;
SubfolderGroupPlan& SubfolderGroupPlan::operator=(SubfolderGroupPlan&&) =
    default;
SubfolderGroupPlan::~SubfolderGroupPlan() = default;

std::vector<SubfolderGroupPlan> BuildSubfolderGroupPlans(
    const bookmarks::BookmarkNode& folder) {
  std::vector<SubfolderGroupPlan> plans;
  for (const auto& child : folder.children()) {
    if (!child->is_folder()) {
      continue;
    }
    std::vector<GURL> urls;
    for (const auto& grandchild : child->children()) {
      if (grandchild->is_url()) {
        urls.push_back(grandchild->url());
      }
    }
    if (!urls.empty()) {
      plans.emplace_back(child->GetTitle(), std::move(urls));
    }
  }
  return plans;
}

namespace {

// Decision 3: one new window in the invoking profile; groups in bookmark
// order. Decision 8: direct group creation — the bookmark-connected
// open-in-group flow (and its conversion dialog) is never involved.
void DoOpenSubfolderGroups(Profile* profile,
                           std::vector<SubfolderGroupPlan> plans) {
  Browser* opened = Browser::Create(Browser::CreateParams(profile, true));
  TabStripModel* tabs = opened->tab_strip_model();
  for (const SubfolderGroupPlan& plan : plans) {
    const int start = tabs->count();
    for (const GURL& url : plan.urls) {
      NavigateParams params(opened, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
      params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
      Navigate(&params);
    }
    std::vector<int> indices;
    for (int i = start; i < tabs->count(); ++i) {
      indices.push_back(i);
    }
    if (indices.empty()) {
      continue;
    }
    const tab_groups::TabGroupId group_id = tabs->AddToNewGroup(indices);
    // Decisions 5+6: exact subfolder title; keep the internally assigned
    // color (never SuggestUniqueTabGroupName, never an explicit color).
    const tab_groups::TabGroupVisualData* assigned =
        tabs->group_model()->GetTabGroup(group_id)->visual_data();
    tabs->ChangeTabGroupVisuals(
        group_id, tab_groups::TabGroupVisualData(plan.title, assigned->color()),
        /*is_customized=*/true);
  }

  if (tabs->count() == 0) {
    opened->window()->Close();
    return;
  }
  tabs->ActivateTabAt(0);
  opened->window()->Show();
}

void OnBulkOpenPromptAnswered(Profile* profile,
                              std::vector<SubfolderGroupPlan> plans,
                              chrome::MessageBoxResult result) {
  if (result == chrome::MESSAGE_BOX_RESULT_YES) {
    DoOpenSubfolderGroups(profile, std::move(plans));
  }
}

}  // namespace

void OpenSubfolderGroupsInNewWindow(Browser* source,
                                    std::vector<SubfolderGroupPlan> plans) {
  if (!source || plans.empty()) {
    return;
  }

  size_t total = 0;
  for (const SubfolderGroupPlan& plan : plans) {
    total += plan.urls.size();
  }

  // Decision 7: exactly one aggregate prompt at the shared threshold — the
  // same asynchronous dialog shape and strings the bookmark open-all path
  // uses. The plans are snapshots, so the continuation cannot dangle.
  if (total >= bookmarks::kNumBookmarkUrlsBeforePrompting) {
    const std::u16string message = l10n_util::GetStringFUTF16(
        IDS_BOOKMARK_BAR_SHOULD_OPEN_ALL, base::NumberToString16(total));
    if (g_bulk_prompt_for_testing) {
      if (g_bulk_prompt_for_testing(total, message)) {
        DoOpenSubfolderGroups(source->profile(), std::move(plans));
      }
      return;
    }
    chrome::ShowQuestionMessageBoxAsync(
        source->window()->GetNativeWindow(),
        l10n_util::GetStringUTF16(IDS_PRODUCT_NAME), message,
        base::BindOnce(&OnBulkOpenPromptAnswered, source->profile(),
                       std::move(plans)));
    return;
  }
  DoOpenSubfolderGroups(source->profile(), std::move(plans));
}

BulkOpenPromptCallback SetBulkOpenPromptCallbackForTesting(
    BulkOpenPromptCallback callback) {
  BulkOpenPromptCallback previous = g_bulk_prompt_for_testing;
  g_bulk_prompt_for_testing = callback;
  return previous;
}

}  // namespace roamux
