// SPDX-License-Identifier: Apache-2.0
// roam-37: browser proxy for chrome://roamux-about — binds the update page to
// roam-85's UpdatePageHandlerFactory (hands a UpdatePage remote + a handler
// receiver via CreatePageHandler).

import {UpdatePageCallbackRouter, UpdatePageHandlerFactory, UpdatePageHandlerRemote} from './update_page.mojom-webui.js';

export interface BrowserProxy {
  handler: UpdatePageHandlerRemote;
  callbackRouter: UpdatePageCallbackRouter;
}

export class BrowserProxyImpl implements BrowserProxy {
  handler: UpdatePageHandlerRemote;
  callbackRouter: UpdatePageCallbackRouter;

  constructor() {
    this.handler = new UpdatePageHandlerRemote();
    this.callbackRouter = new UpdatePageCallbackRouter();
    const factory = UpdatePageHandlerFactory.getRemote();
    factory.createPageHandler(
        this.callbackRouter.$.bindNewPipeAndPassRemote(),
        this.handler.$.bindNewPipeAndPassReceiver());
  }

  static getInstance(): BrowserProxy {
    return instance || (instance = new BrowserProxyImpl());
  }

  static setInstance(obj: BrowserProxy) {
    instance = obj;
  }
}

let instance: BrowserProxy|null = null;
