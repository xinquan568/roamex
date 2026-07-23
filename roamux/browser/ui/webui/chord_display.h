// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_UI_WEBUI_CHORD_DISPLAY_H_
#define ROAMUX_BROWSER_UI_WEBUI_CHORD_DISPLAY_H_

#include <cstdint>
#include <string>

namespace roamux {

// roam-207: display text for the KEY half of a shortcut chord — the character
// the current keyboard layout produces for `keycode`, else a canonical
// accelerator name, else a DOM code name; never a numeric placeholder.
std::string ChordKeyDisplayString(uint16_t keycode);

}  // namespace roamux

#endif  // ROAMUX_BROWSER_UI_WEBUI_CHORD_DISPLAY_H_
