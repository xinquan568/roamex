// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/ui/tabs/initial_url_menu.h"

#include "base/feature_list.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/web_contents.h"
#include "roamux/browser/tabs/tab_initial_url_helper.h"
#include "roamux/browser/ui/tabs/edit_initial_url_dialog.h"
#include "roamux/common/roamux_features.h"
#include "ui/menus/simple_menu_model.h"

namespace roamux::tabs {

namespace {

// The command ids and their guard predicate are exported from the header
// (roam-194): the guard in patch 0005 and the tab-menu guard browsertest need
// to name them.
bool CanSetToCurrentPage(content::WebContents* contents) {
  const GURL& url = contents->GetLastCommittedURL();
  return url.is_valid() && !url.IsAboutBlank() &&
         url.spec() != "chrome://newtab/";
}

// The submenu owns its command handling (0005 pattern): upstream's
// TabMenuModel delegate is never touched.
class InitialUrlMenuModel : public ui::SimpleMenuModel,
                            public ui::SimpleMenuModel::Delegate {
 public:
  explicit InitialUrlMenuModel(content::WebContents* contents)
      : ui::SimpleMenuModel(this), contents_(contents) {
    AddItem(kEditInitialUrlCommandId, u"Edit initial URL…");
    AddItem(kSetInitialUrlToCurrentPageCommandId,
            u"Set initial URL to current page");
  }

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override {
    if (command_id == kSetInitialUrlToCurrentPageCommandId) {
      return CanSetToCurrentPage(contents_);
    }
    return true;
  }

  void ExecuteCommand(int command_id, int event_flags) override {
    TabInitialUrlHelper::MaybeCreateForWebContents(contents_);
    TabInitialUrlHelper* helper =
        TabInitialUrlHelper::FromWebContents(contents_);
    if (!helper) {
      return;
    }
    switch (command_id) {
      case kEditInitialUrlCommandId:
        ShowEditInitialUrlDialog(contents_);
        break;
      case kSetInitialUrlToCurrentPageCommandId:
        if (CanSetToCurrentPage(contents_)) {
          helper->SetUserInitialUrl(contents_->GetLastCommittedURL());
        }
        break;
    }
  }

 private:
  const raw_ptr<content::WebContents> contents_;
};

}  // namespace

std::unique_ptr<ui::SimpleMenuModel> MaybeAppendInitialUrlSubMenu(
    ui::SimpleMenuModel* parent,
    content::WebContents* contents) {
  if (!contents ||
      !base::FeatureList::IsEnabled(roamux::features::kInitialUrl)) {
    return nullptr;
  }
  auto submenu = std::make_unique<InitialUrlMenuModel>(contents);
  parent->AddSubMenu(kInitialUrlSubMenuCommandId, u"Initial URL",
                     submenu.get());
  return submenu;
}

}  // namespace roamux::tabs
