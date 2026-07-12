// SPDX-License-Identifier: Apache-2.0
// roam-32: the Ed25519 appcast-verification matrix over the committed fixed
// pre-signed test fixture (plan §13.6/K3 — dev/test key only) plus an
// in-process keypair sanity row. TDD/P6: written RED (verifier stub only).

#include <string>

#include "base/base64.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "roamux/app/appcast_verifier.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

namespace {

base::FilePath FixtureDir() {
  base::FilePath src;
  base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &src);
  return src.AppendASCII("roamux/test/data/sparkle");
}

std::string ReadTrimmed(const std::string& name) {
  std::string content;
  EXPECT_TRUE(base::ReadFileToString(FixtureDir().AppendASCII(name), &content));
  return std::string(base::TrimWhitespaceASCII(content, base::TRIM_ALL));
}

TEST(RoamuxAppcastVerifierTest, FixtureSignatureVerifies) {
  std::string artifact;
  ASSERT_TRUE(base::ReadFileToString(
      FixtureDir().AppendASCII("Roamux-99.0.0.zip"), &artifact));
  EXPECT_TRUE(roamux::app::VerifyAppcastSignature(
      base::as_byte_span(artifact), ReadTrimmed("artifact_signature.b64"),
      ReadTrimmed("test_public_ed_key.b64")));
}

TEST(RoamuxAppcastVerifierTest, TamperedArtifactRejected) {
  std::string artifact;
  ASSERT_TRUE(base::ReadFileToString(
      FixtureDir().AppendASCII("Roamux-99.0.0.zip"), &artifact));
  artifact[0] ^= 0x01;
  EXPECT_FALSE(roamux::app::VerifyAppcastSignature(
      base::as_byte_span(artifact), ReadTrimmed("artifact_signature.b64"),
      ReadTrimmed("test_public_ed_key.b64")));
}

TEST(RoamuxAppcastVerifierTest, EmptyOrGarbageSignatureRejected) {
  std::string artifact = "whatever";
  EXPECT_FALSE(roamux::app::VerifyAppcastSignature(
      base::as_byte_span(artifact), "", ReadTrimmed("test_public_ed_key.b64")));
  EXPECT_FALSE(roamux::app::VerifyAppcastSignature(
      base::as_byte_span(artifact), "!!!not-base64!!!",
      ReadTrimmed("test_public_ed_key.b64")));
}

TEST(RoamuxAppcastVerifierTest, WrongKeyRejectedAndInProcessRoundTrip) {
  uint8_t pub[ED25519_PUBLIC_KEY_LEN];
  uint8_t priv[ED25519_PRIVATE_KEY_LEN];
  ED25519_keypair(pub, priv);
  const std::string payload = "in-process payload";
  uint8_t sig[ED25519_SIGNATURE_LEN];
  ASSERT_TRUE(ED25519_sign(sig, base::as_byte_span(payload).data(),
                           payload.size(), priv));
  const std::string sig_b64 = base::Base64Encode(sig);
  const std::string pub_b64 = base::Base64Encode(pub);
  EXPECT_TRUE(roamux::app::VerifyAppcastSignature(base::as_byte_span(payload),
                                                  sig_b64, pub_b64));
  // The fixture's signature must NOT verify under this fresh key.
  std::string artifact;
  ASSERT_TRUE(base::ReadFileToString(
      FixtureDir().AppendASCII("Roamux-99.0.0.zip"), &artifact));
  EXPECT_FALSE(roamux::app::VerifyAppcastSignature(
      base::as_byte_span(artifact), ReadTrimmed("artifact_signature.b64"),
      pub_b64));
}

}  // namespace
