// SPDX-License-Identifier: Apache-2.0
// roam-158: chrome://theme/current-channel-logo must serve the Roamux glyph,
// not the Chromium pinwheel (the settings toolbar, chrome://version, the
// settings drawer, the management/extensions banners all render this URL).
//
// The test drives ThemeSource::StartDataRequest — the handler behind
// chrome://theme — for current-channel-logo@1x/@2x against the browser's real
// ResourceBundle, so it proves the whole serving chain the tier-1 asset gate
// (check_toolbar_logo.py) cannot: the roamux_theme_resources.grd registration,
// the resource_ids allocation, the per-scale pak repack, the
// CurrentChannelLogoResourceId() repoint (patch 0037), and ThemeSource's scale
// parsing. Expected art is the committed //roamux per-scale source of truth,
// compared pixel-wise (grit strips/reorders ancillary PNG chunks, so byte
// equality would be wrong); the pinwheel inequality check pins the failure
// mode this issue reports.

#include <optional>
#include <string>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/memory/ref_counted_memory.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/test/bind.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/webui/theme_source.h"
#include "chrome/common/url_constants.h"
#include "content/public/test/browser_test.h"
#include "roamux/test/support/roamux_browser_test.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"
#include "url/gurl.h"

namespace {

// Committed per-scale Roamux glyph sources (what the grd packs).
constexpr char kRoamuxGlyph1x[] =
    "roamux/app/resources/theme/default_100_percent/product_logo_32.png";
constexpr char kRoamuxGlyph2x[] =
    "roamux/app/resources/theme/default_200_percent/product_logo_32.png";
// The upstream art this issue evicts from current-channel-logo surfaces.
constexpr char kChromiumLogo1x[] =
    "chrome/app/theme/default_100_percent/chromium/product_logo_32.png";

SkBitmap DecodePng(base::span<const uint8_t> bytes) {
  return gfx::PNGCodec::Decode(bytes);
}

// Decodes a PNG committed in the source tree (browsertests run with the
// checkout present; DIR_SRC_TEST_DATA_ROOT is the src/ root).
SkBitmap LoadCommittedPng(const std::string& src_relative) {
  // The test body runs on the UI thread, where blocking IO is disallowed;
  // reading the committed art is test-only setup.
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath src_root;
  if (!base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &src_root)) {
    return SkBitmap();
  }
  std::string contents;
  if (!base::ReadFileToString(src_root.AppendASCII(src_relative), &contents)) {
    return SkBitmap();
  }
  return DecodePng(base::as_byte_span(contents));
}

bool PixelsEqual(const SkBitmap& a, const SkBitmap& b) {
  if (a.drawsNothing() || b.drawsNothing() ||
      a.dimensions() != b.dimensions()) {
    return false;
  }
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      if (a.getColor(x, y) != b.getColor(x, y)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

class RoamuxThemeLogoBrowserTest : public roamux::test::RoamuxBrowserTest {
 protected:
  // Serves one chrome://theme path through the real handler and decodes the
  // returned PNG. An empty bitmap means "served nothing/undecodable".
  SkBitmap ServeThemePng(const std::string& path) {
    ThemeSource source(browser()->profile());
    scoped_refptr<base::RefCountedMemory> bytes;
    base::RunLoop loop;
    source.StartDataRequest(
        GURL(base::StrCat({content::kChromeUIScheme, "://",
                           chrome::kChromeUIThemeHost, "/", path})),
        base::BindLambdaForTesting([this]() { return web_contents(); }),
        base::BindLambdaForTesting(
            [&](scoped_refptr<base::RefCountedMemory> data) {
              bytes = std::move(data);
              loop.Quit();
            }));
    loop.Run();
    if (!bytes || bytes->size() == 0u) {
      return SkBitmap();
    }
    return DecodePng(base::span<const uint8_t>(*bytes));
  }

 private:
  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }
};

IN_PROC_BROWSER_TEST_F(RoamuxThemeLogoBrowserTest,
                       CurrentChannelLogoServesRoamuxGlyph) {
  const SkBitmap expected_1x = LoadCommittedPng(kRoamuxGlyph1x);
  const SkBitmap expected_2x = LoadCommittedPng(kRoamuxGlyph2x);
  ASSERT_FALSE(expected_1x.drawsNothing())
      << kRoamuxGlyph1x << " is missing or undecodable";
  ASSERT_FALSE(expected_2x.drawsNothing())
      << kRoamuxGlyph2x << " is missing or undecodable";
  ASSERT_EQ(32, expected_1x.width());
  ASSERT_EQ(64, expected_2x.width());

  const SkBitmap served_1x = ServeThemePng("current-channel-logo@1x");
  const SkBitmap served_2x = ServeThemePng("current-channel-logo@2x");
  ASSERT_FALSE(served_1x.drawsNothing()) << "@1x served nothing";
  ASSERT_FALSE(served_2x.drawsNothing()) << "@2x served nothing";

  EXPECT_EQ(32, served_1x.width());
  EXPECT_EQ(32, served_1x.height());
  EXPECT_EQ(64, served_2x.width());
  EXPECT_EQ(64, served_2x.height());

  EXPECT_TRUE(PixelsEqual(served_1x, expected_1x))
      << "@1x does not match the committed Roamux glyph";
  EXPECT_TRUE(PixelsEqual(served_2x, expected_2x))
      << "@2x does not match the committed Roamux glyph";

  // The regression this issue reports: the Chromium pinwheel on Roamux
  // surfaces. Guard the negative explicitly.
  const SkBitmap pinwheel_1x = LoadCommittedPng(kChromiumLogo1x);
  ASSERT_FALSE(pinwheel_1x.drawsNothing())
      << kChromiumLogo1x << " is missing or undecodable";
  EXPECT_FALSE(PixelsEqual(served_1x, pinwheel_1x))
      << "@1x still serves the Chromium product logo";
}
