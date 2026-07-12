// SPDX-License-Identifier: Apache-2.0
#include "roamux/browser/ui/tabs/edit_initial_url_dialog.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/web_contents.h"
#include "roamux/browser/tabs/tab_initial_url_helper.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/models/dialog_model.h"
#include "url/gurl.h"

namespace roamux::tabs {

namespace {

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kRoamuxInitialUrlField);

// Validates + writes. Shared by the dialog accept and the testing seam. An
// invalid GURL is a no-op (the dialog is v1-simple: no inline error state).
bool CommitEdit(content::WebContents* contents, const std::string& text) {
  GURL url(text);
  if (!url.is_valid()) {
    return false;
  }
  TabInitialUrlHelper::MaybeCreateForWebContents(contents);
  TabInitialUrlHelper* helper = TabInitialUrlHelper::FromWebContents(contents);
  if (!helper) {
    return false;
  }
  helper->SetUserInitialUrl(url);
  return true;
}

class EditInitialUrlDelegate : public ui::DialogModelDelegate {
 public:
  explicit EditInitialUrlDelegate(content::WebContents* contents)
      : contents_(contents) {}

  void OnAccept() {
    CommitEdit(
        contents_,
        base::UTF16ToUTF8(dialog_model()
                              ->GetTextfieldByUniqueId(kRoamuxInitialUrlField)
                              ->text()));
  }

 private:
  const raw_ptr<content::WebContents> contents_;
};

}  // namespace

void ShowEditInitialUrlDialog(content::WebContents* contents) {
  TabInitialUrlHelper* helper = TabInitialUrlHelper::FromWebContents(contents);
  const std::u16string initial_text =
      helper && helper->has_initial_url()
          ? base::UTF8ToUTF16(helper->initial_url().spec())
          : std::u16string();
  auto delegate = std::make_unique<EditInitialUrlDelegate>(contents);
  EditInitialUrlDelegate* delegate_ptr = delegate.get();
  ui::DialogModel::Builder builder(std::move(delegate));
  builder.SetTitle(u"Edit initial URL")
      .AddTextfield(kRoamuxInitialUrlField, u"URL", initial_text)
      .AddOkButton(base::BindOnce(&EditInitialUrlDelegate::OnAccept,
                                  base::Unretained(delegate_ptr)),
                   ui::DialogModel::Button::Params().SetLabel(u"Save"))
      .AddCancelButton(base::DoNothing());
  constrained_window::ShowWebModal(builder.Build(), contents);
}

bool SubmitEditInitialUrlForTesting(content::WebContents* contents,
                                    const std::string& text) {
  return CommitEdit(contents, text);
}

}  // namespace roamux::tabs
