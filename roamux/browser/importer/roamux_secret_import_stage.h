// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_IMPORTER_ROAMUX_SECRET_IMPORT_STAGE_H_
#define ROAMUX_BROWSER_IMPORTER_ROAMUX_SECRET_IMPORT_STAGE_H_

#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "components/user_data_importer/common/importer_data_types.h"

class ProfileWriter;

namespace net {
class CanonicalCookie;
}

namespace crypto::apple {
class KeychainV2;
}

namespace roamux {

// The browser-side secret-import stage (roam-16 / I-3.2). It reads Edge's
// encrypted Login Data (passwords) from `source_path`, decrypts them with the
// Edge Keychain-derived key, and writes them to the destination via
// ProfileWriter (which re-encrypts under Roamux). Runs entirely browser-side —
// the Keychain read + decrypt are co-located here and only plaintext reaches
// ProfileWriter; it does NOT go through the utility-process importer or its
// host/bridge lifecycle (frozen analysis / plan finding 1). Cookies are added
// in a follow-up method once ProfileWriter::AddCookies lands.
class RoamuxSecretImportStage {
 public:
  // `keychain_for_testing` is nullptr in production.
  RoamuxSecretImportStage(base::FilePath source_path,
                          scoped_refptr<ProfileWriter> writer,
                          crypto::apple::KeychainV2* keychain_for_testing);
  RoamuxSecretImportStage(const RoamuxSecretImportStage&) = delete;
  RoamuxSecretImportStage& operator=(const RoamuxSecretImportStage&) = delete;
  ~RoamuxSecretImportStage();

  // The outcome of the secret half of an Edge import.
  struct Result {
    size_t passwords_imported = 0;
    size_t cookies_imported = 0;
    // False if the Keychain was declined/unavailable (the caller REPORTS this
    // — passwords/cookies were not imported, never silently dropped).
    bool keychain_available = false;
  };

  // The browser-side orchestration entry (roam-16 finding 1): given the
  // user-selected `items` mask, imports the requested secret items and reports
  // the Result. Invoked by the import flow (roam-20's first-run UI) alongside
  // the utility-process non-secret importer.
  void Run(uint16_t items, base::OnceCallback<void(Result)> done);

  // Decrypts and imports Edge passwords. Returns the number imported; a
  // declined/unavailable Keychain yields 0 and `keychain_available()` == false
  // (reported by the caller, never a silent drop).
  size_t ImportPasswords();

  // Decrypts and imports Edge first-party cookies. Runs `done` with the count
  // imported after all async CookieManager writes complete.
  void ImportCookies(base::OnceCallback<void(size_t)> done);

  bool keychain_available() const { return keychain_available_; }

  // Testing seam: the decrypted, v24-stripped, first-party-filtered cookies the
  // stage would write (bypasses the async CookieManager round-trip).
  std::vector<net::CanonicalCookie> ParseCookiesForTesting();

 private:
  const base::FilePath source_path_;
  const scoped_refptr<ProfileWriter> writer_;
  std::vector<net::CanonicalCookie> ParseCookies();

  const raw_ptr<crypto::apple::KeychainV2> keychain_for_testing_;
  bool keychain_available_ = false;
};

}  // namespace roamux

#endif  // ROAMUX_BROWSER_IMPORTER_ROAMUX_SECRET_IMPORT_STAGE_H_
