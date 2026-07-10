// SPDX-License-Identifier: Apache-2.0
// roam-37: the chrome://roamex-about page — identity + links + update card +
// check row, bound to roam-85's update state machine via the browser proxy.
// Termixion's AboutSettings.tsx minus the configuration groups.

import './update_page.mojom-webui.js';

import {CrLitElement} from '//resources/lit/v3_0/lit.rollup.js';
import {loadTimeData} from '//resources/js/load_time_data.js';

import {getCss} from './app.css.js';
import {getHtml} from './app.html.js';
import {BrowserProxyImpl} from './browser_proxy.js';
import type {BrowserProxy} from './browser_proxy.js';
import {UpdateStatus} from './update_page.mojom-webui.js';
import type {UpdateSnapshot} from './update_page.mojom-webui.js';

export class RoamexAboutAppElement extends CrLitElement {
  static get is() {
    return 'roamex-about-app';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      snapshot_: {type: Object, state: true},
      updatesAvailable_: {type: Boolean, state: true},
    };
  }

  protected accessor snapshot_: UpdateSnapshot = {
    status: UpdateStatus.kIdle,
    version: '',
    date: '',
    notes: '',
    error: '',
    progress: 0,
  };
  protected accessor updatesAvailable_: boolean =
      loadTimeData.getBoolean('updatesAvailable');

  private proxy_: BrowserProxy = BrowserProxyImpl.getInstance();

  override connectedCallback() {
    super.connectedCallback();
    if (this.updatesAvailable_) {
      this.proxy_.callbackRouter.onStateChanged.addListener(
          (snapshot: UpdateSnapshot) => {
            this.snapshot_ = snapshot;
          });
    }
  }

  // Exposed for tests: inject a fake proxy before connect.
  setProxyForTesting(proxy: BrowserProxy) {
    this.proxy_ = proxy;
  }

  protected onCheckNowClick_() {
    this.proxy_.handler.checkForUpdates();
  }
  protected onDownloadClick_() {
    this.proxy_.handler.download();
  }
  protected onRestartClick_() {
    this.proxy_.handler.installAndRelaunch();
  }
  protected onSkipClick_() {
    this.proxy_.handler.skip(this.snapshot_.version);
  }

  // Template helpers as methods (getters are disallowed in Lit templates).
  protected getProductName_(): string {
    return loadTimeData.getString('productName');
  }
  protected getVersion_(): string {
    return loadTimeData.getString('version');
  }
  protected getStatusLabel_(): string {
    switch (this.snapshot_.status) {
      case UpdateStatus.kChecking:
        return 'Checking for updates…';
      case UpdateStatus.kUpToDate:
        return 'Roamex is up to date';
      case UpdateStatus.kAvailable:
        return 'Update available';
      case UpdateStatus.kDownloading:
        return 'Downloading…';
      case UpdateStatus.kReadyToInstall:
        return 'Ready to install';
      case UpdateStatus.kError:
        return this.snapshot_.error || 'Update error';
      default:
        return '';
    }
  }
  protected shouldShowCard_(): boolean {
    const s = this.snapshot_.status;
    return s === UpdateStatus.kAvailable || s === UpdateStatus.kDownloading ||
        s === UpdateStatus.kReadyToInstall;
  }
  protected isAvailable_(): boolean {
    return this.snapshot_.status === UpdateStatus.kAvailable;
  }
  protected isDownloading_(): boolean {
    return this.snapshot_.status === UpdateStatus.kDownloading;
  }
  protected isReady_(): boolean {
    return this.snapshot_.status === UpdateStatus.kReadyToInstall;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'roamex-about-app': RoamexAboutAppElement;
  }
}

customElements.define(RoamexAboutAppElement.is, RoamexAboutAppElement);
