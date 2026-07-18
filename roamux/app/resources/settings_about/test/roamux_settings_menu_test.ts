// SPDX-License-Identifier: Apache-2.0
// roam-157: the settings left-nav About chip. Before this it rendered a blank slot
// (1x1 stub logo) and a single inline run "About Roamux<version>". It is now a
// two-line chip: the Roamux glyph, "About Roamux" over "v <version>". Rendered with
// roamuxBrandedAbout overridden on so the suite is hermetic on a non-Sparkle test
// build. (TDD/P6.)
//
// Assertion style mirrors roam_settings_about_test.ts (which passes eslint's
// no-unnecessary-type-assertion): `assertTrue(!!q(...))` for existence, `q(...)!`
// inline where a value is needed, `.textContent` WITHOUT `!` (non-nullable in this
// lib config), and `?? ''` rather than `!` for getAttribute.

import type {SettingsMenuElement} from 'chrome://settings/settings.js';
import {loadTimeData} from 'chrome://settings/settings.js';
import {flushTasks} from 'chrome://webui-test/polymer_test_util.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';

suite('RoamuxSettingsMenu', function() {
  let menu: SettingsMenuElement;

  setup(async function() {
    loadTimeData.overrideValues({
      roamuxBrandedAbout: true,
      // The chip title is $i18n{aboutPageTitle}; in a shipped build the GRIT rebrand
      // channel (roam-132) turns "About Chromium" into "About Roamux". Pin it here so
      // the assertion is deterministic regardless of whether this test build ran the
      // rebrand.
      aboutPageTitle: 'About Roamux',
      // The Roamux marketing version (roam-156), deliberately unlike a Chromium
      // MAJOR.MINOR.BUILD.PATCH number so a regression to the Chromium version
      // cannot pass by coincidence.
      version: '1.2.3-alpha.4',
    });
    menu = document.createElement('settings-menu');
    document.body.appendChild(menu);
    await flushTasks();
  });

  teardown(function() {
    menu.remove();
  });

  function q(selector: string): HTMLElement|null {
    return menu.shadowRoot!.querySelector<HTMLElement>(selector);
  }

  test('about chip shows the roamux glyph', function() {
    const logo = q('#about-menu #roamuxMenuLogo') as HTMLImageElement | null;
    assertTrue(!!logo, 'no #roamuxMenuLogo in the About chip');
    // The inline glyph, not the tile logo and not the native product icon.
    assertTrue(
        (logo.getAttribute('src') ?? '').endsWith('roamux_about/roamux_glyph.svg'),
        `unexpected chip icon src: ${logo.getAttribute('src')}`);
    assertFalse(!!q('#about-menu cr-icon'),
                'the native product cr-icon must not render in the branded chip');
  });

  test('about chip renders two lines: title over v-version', function() {
    assertTrue(!!q('#about-menu #roamuxMenuTitle'), 'no #roamuxMenuTitle');
    assertTrue(!!q('#about-menu #roamuxMenuVersion'), 'no #roamuxMenuVersion');
    assertEquals('About Roamux', q('#about-menu #roamuxMenuTitle')!.textContent.trim());
    // "v " prefix + the Roamux version, not the Chromium version.
    assertEquals(
        'v 1.2.3-alpha.4', q('#about-menu #roamuxMenuVersion')!.textContent.trim());
    assertFalse(
        q('#about-menu #roamuxMenuVersion')!.textContent.includes('149.'),
        'the chip version must be the Roamux version, not the Chromium one');
  });

  test('other nav items keep a single line', function() {
    // The two-line treatment is scoped to #about-menu; a sibling nav item (#autofill
    // is a stable id in the M149 settings menu) must not gain a #roamuxMenuText column.
    assertTrue(!!q('#autofill'), 'expected the #autofill nav item to compare against');
    assertFalse(!!q('#autofill #roamuxMenuText'),
                'a non-About nav item must not gain the two-line column');
  });
});
