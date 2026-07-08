// SPDX-License-Identifier: Apache-2.0
#include "roamex/test/support/sso_test_server.h"

#include "base/functional/bind.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace roamex::test {

namespace {

std::unique_ptr<net::test_server::HttpResponse> Redirect(
    const std::string& location) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_FOUND);
  response->AddCustomHeader("Location", location);
  return response;
}

std::unique_ptr<net::test_server::HttpResponse> Page(const std::string& body) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type("text/html");
  response->set_content(body);
  return response;
}

}  // namespace

SsoTestServer::SsoTestServer() = default;
SsoTestServer::~SsoTestServer() = default;

bool SsoTestServer::Start() {
  // The IdP must be up first so the app's redirect can target it.
  idp_server_.RegisterRequestHandler(base::BindRepeating(
      [](SsoTestServer* self, const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url == "/login") {
          return Redirect(self->landing_url().spec());
        }
        if (request.relative_url == "/idp_page") {
          return Page("<html><body>idp page</body></html>");
        }
        return nullptr;
      },
      this));
  app_server_.RegisterRequestHandler(base::BindRepeating(
      [](SsoTestServer* self, const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url == "/dashboard") {
          return Redirect(self->idp_server_.GetURL("/login").spec());
        }
        if (request.relative_url == "/landing") {
          return Page("<html><body>landing</body></html>");
        }
        return nullptr;
      },
      this));
  // Start order: app first (so landing_url resolves), then idp — handlers
  // resolve URLs lazily at request time, so either order works; start both.
  return app_server_.Start() && idp_server_.Start();
}

GURL SsoTestServer::dashboard_url() const {
  return app_server_.GetURL("/dashboard");
}

GURL SsoTestServer::landing_url() const {
  return app_server_.GetURL("/landing");
}

GURL SsoTestServer::idp_page_url() const {
  return idp_server_.GetURL("/idp_page");
}

}  // namespace roamex::test
