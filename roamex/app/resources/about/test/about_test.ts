// SPDX-License-Identifier: Apache-2.0
// roam-37: chrome://roamex-about WebUI tests against a TS-side fake proxy —
// the status matrix, Download→progress→Restart, Skip hides the card, NO
// configuration/reset groups, identity + links. (TDD/P6.)

import 'chrome://roamex-about/app.js';

import {RoamexAboutAppElement} from 'chrome://roamex-about/app.js';
import {BrowserProxyImpl} from 'chrome://roamex-about/browser_proxy.js';
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
    // Inject before construction so no real Mojo bind ever fires (F3).
    BrowserProxyImpl.setInstance(fake);
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

  // Table-driven status matrix: for every UpdateStatus, assert the exact pill
  // text and the FULL set of present/absent controls (F4).
  interface Row {
    status: UpdateStatus;
    version?: string;
    date?: string;
    notes?: string;
    error?: string;
    pill: string;
    present: string[];
    absent: string[];
  }
  const MATRIX: Row[] = [
    {
      status: UpdateStatus.kChecking,
      pill: 'Checking for updates…',
      present: ['spinner', 'statusPill', 'checkNow'],
      absent: ['updateCard', 'download', 'skip', 'restart', 'progress',
               'errorText'],
    },
    {
      status: UpdateStatus.kUpToDate,
      pill: 'Roamex is up to date',
      present: ['statusPill', 'checkNow'],
      absent: ['spinner', 'updateCard', 'download', 'skip', 'restart',
               'progress', 'errorText'],
    },
    {
      status: UpdateStatus.kAvailable,
      version: '2.0.0',
      date: '2026-07-01',
      notes: 'Shiny new build',
      pill: 'Update available',
      present: ['statusPill', 'updateCard', 'download', 'skip'],
      absent: ['spinner', 'restart', 'progress', 'errorText'],
    },
    {
      status: UpdateStatus.kDownloading,
      pill: 'Downloading…',
      present: ['statusPill', 'updateCard', 'progress'],
      absent: ['spinner', 'download', 'skip', 'restart', 'errorText'],
    },
    {
      status: UpdateStatus.kReadyToInstall,
      pill: 'Ready to install',
      present: ['statusPill', 'updateCard', 'restart'],
      absent: ['spinner', 'download', 'skip', 'progress', 'errorText'],
    },
    {
      status: UpdateStatus.kError,
      error: 'network down',
      pill: 'network down',
      present: ['statusPill', 'errorText'],
      absent: ['spinner', 'updateCard', 'download', 'skip', 'restart',
               'progress'],
    },
  ];

  for (const row of MATRIX) {
    test(`status ${row.status}: pill text + controls`, async function() {
      fake.push({
        status: row.status,
        version: row.version ?? '',
        date: row.date ?? '',
        notes: row.notes ?? '',
        error: row.error ?? '',
        progress: row.status === UpdateStatus.kDownloading ? 0.5 : 0,
      });
      await element.updateComplete;

      assertEquals(row.pill, q('statusPill')!.textContent!.trim());
      for (const id of row.present) {
        assertTrue(!!q(id), `expected #${id} present for status ${row.status}`);
      }
      for (const id of row.absent) {
        assertFalse(!!q(id), `expected #${id} absent for status ${row.status}`);
      }

      if (row.version) {
        assertTrue(q('updateCard')!.textContent!.includes(row.version));
      }
      if (row.date) {
        assertTrue(q('updateCard')!.textContent!.includes(row.date));
      }
      if (row.notes) {
        assertTrue(q('updateCard')!.textContent!.includes(row.notes));
      }
      if (row.error) {
        assertEquals(row.error, q('errorText')!.textContent!.trim());
      }
    });
  }
});
