// SPDX-License-Identifier: Apache-2.0
// roam-37: chrome://roamex-about WebUI tests against a TS-side fake proxy —
// the status matrix, Download→progress→Restart, Skip hides the card, NO
// configuration/reset groups, identity + links. (TDD/P6.)

import 'chrome://roamex-about/app.js';

import {RoamexAboutAppElement} from 'chrome://roamex-about/app.js';
import type {BrowserProxy} from 'chrome://roamex-about/browser_proxy.js';
import {UpdateStatus, type UpdateSnapshot} from 'chrome://roamex-about/update_page.mojom-webui.js';
import {assertEquals, assertTrue, assertFalse} from 'chrome://webui-test/chai_assert.js';

// A TS-side fake UpdatePageHandler + a snapshot pump (no C++/Mojo needed).
class FakeProxy implements BrowserProxy {
  checkCount = 0;
  downloadCount = 0;
  installCount = 0;
  skipped: string[] = [];
  private listener_: ((s: UpdateSnapshot) => void)|null = null;

  handler = {
    checkForUpdates: () => {
      this.checkCount++;
    },
    download: () => {
      this.downloadCount++;
    },
    installAndRelaunch: () => {
      this.installCount++;
    },
    skip: (v: string) => {
      this.skipped.push(v);
    },
  } as unknown as BrowserProxy['handler'];

  callbackRouter = {
    onStateChanged: {
      addListener: (cb: (s: UpdateSnapshot) => void) => {
        this.listener_ = cb;
      },
    },
  } as unknown as BrowserProxy['callbackRouter'];

  push(snapshot: Partial<UpdateSnapshot>) {
    this.listener_!({
      status: UpdateStatus.kIdle,
      version: '',
      date: '',
      notes: '',
      error: '',
      progress: 0,
      ...snapshot,
    });
  }
}

suite('RoamexAbout', function() {
  let element: RoamexAboutAppElement;
  let fake: FakeProxy;

  setup(async function() {
    fake = new FakeProxy();
    element = new RoamexAboutAppElement();
    element.setProxyForTesting(fake);
    document.body.appendChild(element);
    await element.updateComplete;
  });

  teardown(function() {
    element.remove();
  });

  function q(id: string): HTMLElement|null {
    return element.shadowRoot!.querySelector(`#${id}`);
  }

  test('identity and links render', function() {
    assertTrue(!!q('productName'));
    assertTrue(!!q('version'));
    assertTrue(!!q('websiteLink'));
    assertTrue(!!q('githubLink'));
  });

  test('no configuration or reset groups present', function() {
    // Termixion parity MINUS config: none of these exist.
    assertFalse(!!element.shadowRoot!.querySelector('settings-section'));
    assertFalse(!!q('resetGroup'));
    assertFalse(!!q('configGroup'));
  });

  test('available shows card with download and skip', async function() {
    fake.push({status: UpdateStatus.kAvailable, version: '2.0.0'});
    await element.updateComplete;
    assertTrue(!!q('updateCard'));
    assertTrue(!!q('download'));
    assertTrue(!!q('skip'));
  });

  test('download then progress then restart', async function() {
    fake.push({status: UpdateStatus.kAvailable, version: '2.0.0'});
    await element.updateComplete;
    q('download')!.click();
    assertEquals(1, fake.downloadCount);

    fake.push({status: UpdateStatus.kDownloading, progress: 0.5});
    await element.updateComplete;
    assertTrue(!!q('progress'));

    fake.push({status: UpdateStatus.kReadyToInstall});
    await element.updateComplete;
    assertTrue(!!q('restart'));
    q('restart')!.click();
    assertEquals(1, fake.installCount);
  });

  test('skip hides the card', async function() {
    fake.push({status: UpdateStatus.kAvailable, version: '2.0.0'});
    await element.updateComplete;
    q('skip')!.click();
    assertEquals(1, fake.skipped.length);
    assertEquals('2.0.0', fake.skipped[0]);

    // The service would push upToDate after a skip → card gone.
    fake.push({status: UpdateStatus.kUpToDate});
    await element.updateComplete;
    assertFalse(!!q('updateCard'));
  });

  test('check now issues a check', function() {
    q('checkNow')!.click();
    assertEquals(1, fake.checkCount);
  });
});
