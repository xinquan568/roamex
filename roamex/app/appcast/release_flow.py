# SPDX-License-Identifier: Apache-2.0
"""The roam-34 draft/staging/publish invariants (K2/K3).

- require_sparkle_key: the Sparkle EdDSA private key must exist for ANY release
  (K3 — no unsigned-by-Sparkle update path); the update leg is NOT gated on the
  Apple signing mode.
- assert_publishable: never publish a prerelease, and never publish before the
  draft passed staging validation.
- plan_release: the three distinct URLs (per-tag staging feed, production
  /latest/ feed, per-tag artifact download).
"""

REPO = "https://github.com/xinquan568/roamex"


class MissingSparkleKeyError(RuntimeError):
    pass


class NotPublishableError(RuntimeError):
    pass


def require_sparkle_key(env):
    key = env.get("SPARKLE_ED_PRIVATE_KEY", "").strip()
    if not key:
        raise MissingSparkleKeyError(
            "SPARKLE_ED_PRIVATE_KEY is required for every release "
            "(the Sparkle EdDSA signature is mandatory in every mode — K3)")
    return key


def assert_publishable(is_prerelease, staging_validated):
    if is_prerelease:
        raise NotPublishableError("refusing to publish a prerelease as latest")
    if not staging_validated:
        raise NotPublishableError(
            "refusing to publish before staging validation passed")


def plan_release(tag, artifact_name):
    return {
        "staging_feed_url":
            f"{REPO}/releases/download/{tag}/appcast.xml",
        "production_feed_url":
            f"{REPO}/releases/latest/download/appcast.xml",
        "enclosure_url":
            f"{REPO}/releases/download/{tag}/{artifact_name}",
    }
