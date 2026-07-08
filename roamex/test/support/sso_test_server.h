// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMEX_TEST_SUPPORT_SSO_TEST_SERVER_H_
#define ROAMEX_TEST_SUPPORT_SSO_TEST_SERVER_H_

#include "net/test/embedded_test_server/embedded_test_server.h"
#include "url/gurl.h"

namespace roamex::test {

// The shared SSO/auth fixture (plan §4.7/§5.6, reused by E2 command tests and
// E3 import verification): an "app" origin whose /dashboard 302s to a
// cross-origin "IdP" /login, which 302s back to the app's /landing.
class SsoTestServer {
 public:
  SsoTestServer();
  ~SsoTestServer();

  // Starts both servers; false on failure.
  bool Start();

  GURL dashboard_url() const;  // The URL the user "really opened".
  GURL landing_url() const;    // Where the chain commits.
  GURL idp_page_url() const;   // A plain cross-origin page (no redirects).

 private:
  net::EmbeddedTestServer app_server_;
  net::EmbeddedTestServer idp_server_;
};

}  // namespace roamex::test

#endif  // ROAMEX_TEST_SUPPORT_SSO_TEST_SERVER_H_
