// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/ui/tabs/initial_url_menu.h"

#include "base/feature_list.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/web_contents.h"
#include "roamex/browser/tabs/tab_initial_url_helper.h"
#include "roamex/browser/ui/tabs/edit_initial_url_dialog.h"
#include "roamex/common/roamex_features.h"
#include "ui/menus/simple_menu_model.h"

namespace roamex::tabs {

namespace {

// Command ids in the roamex private high range (0005 uses 2101-2105). The
// submenu id is what the parent menu sees; the item ids are resolved by this
// model's own delegate.
constexpr int kInitialUrlSubMenu = 2110;
constexpr int kEditInitialUrl = 2111;
constexpr int kSetInitialUrlToCurrentPage = 2112;

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
    AddItem(kEditInitialUrl, u"Edit initial URL…");
    AddItem(kSetInitialUrlToCurrentPage, u"Set initial URL to current page");
  }

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override {
    if (command_id == kSetInitialUrlToCurrentPage) {
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
      case kEditInitialUrl:
        ShowEditInitialUrlDialog(contents_);
        break;
      case kSetInitialUrlToCurrentPage:
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
      !base::FeatureList::IsEnabled(roamex::features::kInitialUrl)) {
    return nullptr;
  }
  auto submenu = std::make_unique<InitialUrlMenuModel>(contents);
  parent->AddSubMenu(kInitialUrlSubMenu, u"Initial URL", submenu.get());
  return submenu;
}

}  // namespace roamex::tabs
