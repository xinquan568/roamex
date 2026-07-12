// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UI_TABS_EDIT_INITIAL_URL_DIALOG_H_
#define ROAMUX_BROWSER_UI_TABS_EDIT_INITIAL_URL_DIALOG_H_

#include <string>

namespace content {
class WebContents;
}

namespace roamux::tabs {

// Shows the tab-modal "Edit initial URL" dialog (roam-14, §4.5): textfield
// prefilled with the current value; OK is enabled only for a valid GURL and
// writes through TabInitialUrlHelper::SetUserInitialUrl (locks).
void ShowEditInitialUrlDialog(content::WebContents* contents);

// Testing seam: the dialog's validate+accept path without native UI —
// returns false when `text` does not parse to a valid GURL (no write).
bool SubmitEditInitialUrlForTesting(content::WebContents* contents,
                                    const std::string& text);

}  // namespace roamux::tabs

#endif  // ROAMUX_BROWSER_UI_TABS_EDIT_INITIAL_URL_DIALOG_H_
