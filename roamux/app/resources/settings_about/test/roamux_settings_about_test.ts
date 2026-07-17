// SPDX-License-Identifier: Apache-2.0
// roam-140: chrome://settings/help branded About page integration test — the
// Roamux logo/title, the embedded <roamux-update-card>, and the Website+GitHub
// links card (BOTH → github.com/xinquan568/roamux). Renders the real
// settings-about-page with roamuxBrandedAbout overridden on, so the suite is
// hermetic on a non-Sparkle test build (the buildflag drives the default, this
// override drives the branded branch). (TDD/P6.)

// Register the relocated element so <roamux-update-card> upgrades when the
// branded about_page stamps it.
import 'chrome://settings/roamux_about/roamux_update_card.js';

import type {SettingsAboutPageElement} from 'chrome://settings/settings.js';
import {loadTimeData} from 'chrome://settings/settings.js';
import {flushTasks} from 'chrome://webui-test/polymer_test_util.js';
import {assertEquals, assertFalse, assertTrue} from 'chrome://webui-test/chai_assert.js';

const GITHUB_URL = 'https://github.com/xinquan568/roamux';

suite('RoamuxSettingsAbout', function() {
  let page: SettingsAboutPageElement;

  setup(async function() {
    loadTimeData.overrideValues({
      roamuxBrandedAbout: true,
      updatesAvailable: false,
      productName: 'Roamux',
      // roam-156: the ROAMUX marketing version, compiled in from
      // roamux/build/VERSION — not the Chromium version. Deliberately unlike a
      // Chromium version number (which is MAJOR.MINOR.BUILD.PATCH) so a
      // regression to GetVersionNumber() cannot pass by coincidence.
      version: '1.2.3-alpha.4',
      aboutBrowserVersion: 'Roamux 1.2.3-alpha.4 (Developer Build) (arm64)',
      aboutChromiumVersion: 'Chromium: 149.0.7827.201',
      aboutProductTitle: 'Roamux',
    });
    page = document.createElement('settings-about-page');
    document.body.appendChild(page);
    await flushTasks();
  });

  teardown(function() {
    page.remove();
  });

  function q(selector: string): HTMLElement|null {
    return page.shadowRoot!.querySelector<HTMLElement>(selector);
  }

  test('embeds the roamux update card', function() {
    assertTrue(!!q('roamux-update-card'));
  });

  test('branded logo and title render', function() {
    const logo = q('#product-logo') as HTMLImageElement | null;
    assertTrue(!!logo);
    // The branded logo is the settings-served Roamux asset, not the channel
    // logo theme resource.
    assertTrue(
        (logo.getAttribute('srcset') ?? logo.getAttribute('src') ?? '')
            .includes('roamux_about/roamux_logo.png'));
    assertTrue(!!q('.product-title'));
    assertEquals('Roamux', q('.product-title')!.textContent.trim());
  });

  test('version lines name Roamux and Chromium separately', function() {
    // roam-156: the About page used to render only the Chromium version, because
    // the Roamux marketing version did not exist at runtime and settings_ui.cc
    // fell back to version_info::GetVersionNumber(). Two lines now: the product
    // first, the upstream base it is pinned to second.
    const secondaries =
        Array.from(page.shadowRoot!.querySelectorAll('.secondary'))
            .map(e => e.textContent!.trim());

    const roamuxLine = secondaries.find(t => t.startsWith('Roamux '));
    assertTrue(!!roamuxLine, `no Roamux version line in: ${secondaries}`);
    assertEquals('Roamux 1.2.3-alpha.4 (Developer Build) (arm64)', roamuxLine);

    // The regression this issue reports: the product line must not be showing
    // the Chromium version.
    assertFalse(
        roamuxLine!.includes('149.0.7827.201'),
        'the Roamux version line must not carry the Chromium version');

    // Separate element, not a newline — aboutBrowserVersion renders as one
    // string inside a single div, so the base version needs its own.
    assertTrue(
        secondaries.includes('Chromium: 149.0.7827.201'),
        `no Chromium base line in: ${secondaries}`);
  });

  test('website and github links both resolve to the GitHub repo', function() {
    const website = q('#websiteLink') as HTMLAnchorElement | null;
    const github = q('#githubLink') as HTMLAnchorElement | null;
    assertTrue(!!website);
    assertTrue(!!github);
    assertEquals(GITHUB_URL, website.href);
    assertEquals(GITHUB_URL, github.href);
  });
});
