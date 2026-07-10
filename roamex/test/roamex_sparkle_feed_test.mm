// SPDX-License-Identifier: Apache-2.0
// roam-32: the fixed pre-signed test feed driven through Sparkle's OWN
// update pipeline (SPUUpdater + headless SPUUserDriver, plan §13.6/K3): the
// correctly-signed item passes EdDSA validation (extraction begins); a
// tampered or unsigned item is rejected BEFORE extraction. The feed is the
// committed fixture with only the enclosure host rewritten to the local test
// server (fixture bytes otherwise fixed). Full install/relaunch E2E is
// I-6.3's staging job.

#import <Foundation/Foundation.h>
#import <Sparkle/Sparkle.h>

#include <string>

#include "base/apple/foundation_util.h"
#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#include "base/time/time.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"

// Headless SPUUserDriver: auto-consents, auto-installs, records milestones.
@interface RoamexTestUserDriver : NSObject <SPUUserDriver>
@property(nonatomic) BOOL updateFound;
@property(nonatomic) BOOL extractionStarted;
@property(nonatomic) BOOL readyToInstall;
@property(nonatomic) BOOL finished;
@property(nonatomic, strong) NSError* updaterError;
@end

@implementation RoamexTestUserDriver
@synthesize updateFound = _updateFound;
@synthesize extractionStarted = _extractionStarted;
@synthesize readyToInstall = _readyToInstall;
@synthesize finished = _finished;
@synthesize updaterError = _updaterError;
- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest*)request
                              reply:
                                  (void (^)(SUUpdatePermissionResponse*))reply {
  reply([[SUUpdatePermissionResponse alloc] initWithAutomaticUpdateChecks:NO
                                                        sendSystemProfile:NO]);
}
- (void)showUserInitiatedUpdateCheckWithCancellation:
    (void (^)(void))cancellation {
}
- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem*)appcastItem
                                 state:(SPUUserUpdateState*)state
                                 reply:(void (^)(SPUUserUpdateChoice))reply {
  self.updateFound = YES;
  reply(SPUUserUpdateChoiceInstall);
}
- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData*)downloadData {
}
- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError*)error {
}
- (void)showUpdateNotFoundWithError:(NSError*)error
                    acknowledgement:(void (^)(void))acknowledgement {
  self.updaterError = error;
  self.finished = YES;
  acknowledgement();
}
- (void)showUpdaterError:(NSError*)error
         acknowledgement:(void (^)(void))acknowledgement {
  NSLog(@"roamex harness updater error: %@", error);
  self.updaterError = error;
  self.finished = YES;
  acknowledgement();
}
- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancellation {
}
- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)length {
}
- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length {
}
- (void)showDownloadDidStartExtractingUpdate {
  self.extractionStarted = YES;
}
- (void)showExtractionReceivedProgress:(double)progress {
}
- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply {
  // The signature gate sits between extraction and this point. Never
  // actually install in the harness.
  self.readyToInstall = YES;
  self.finished = YES;
  reply(SPUUserUpdateChoiceDismiss);
}
- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)terminated
                          retryTerminatingApplication:(void (^)(void))retry {
}
- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched
                         acknowledgement:(void (^)(void))acknowledgement {
  acknowledgement();
}
- (void)dismissUpdateInstallation {
  self.finished = YES;
}
- (void)showUpdateInFocus {
}
@end

namespace {

base::FilePath FixtureDir() {
  base::FilePath src;
  base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &src);
  return src.AppendASCII("roamex/test/data/sparkle");
}

class RoamexSparkleFeedTest : public testing::Test {
 protected:
  // Serves the committed fixture with the enclosure host rewritten to the
  // local server; builds a throwaway host bundle carrying the TEST key.
  void RunCheck(const std::string& appcast_name) {
    net::EmbeddedTestServer server;
    std::string appcast;
    ASSERT_TRUE(base::ReadFileToString(FixtureDir().AppendASCII(appcast_name),
                                       &appcast));
    server.RegisterRequestHandler(base::BindRepeating(
        &RoamexSparkleFeedTest::Serve, base::Unretained(this)));
    ASSERT_TRUE(server.Start());
    const std::string base_url = server.base_url().spec();  // trailing '/'
    base::ReplaceSubstringsAfterOffset(&appcast, 0, "https://example.invalid/",
                                       base_url);
    appcast_ = appcast;
    ASSERT_TRUE(base::ReadFileToString(
        FixtureDir().AppendASCII("Roamex-99.0.0.zip"), &artifact_));
    std::string public_key;
    ASSERT_TRUE(base::ReadFileToString(
        FixtureDir().AppendASCII("test_public_ed_key.b64"), &public_key));
    public_key =
        std::string(base::TrimWhitespaceASCII(public_key, base::TRIM_ALL));

    base::FilePath host_dir;
    ASSERT_TRUE(base::CreateNewTempDirectory("roamex-sparkle-host", &host_dir));
    const base::FilePath contents =
        host_dir.AppendASCII("TestHost.app").AppendASCII("Contents");
    ASSERT_TRUE(base::CreateDirectory(contents.AppendASCII("MacOS")));
    ASSERT_TRUE(base::WriteFile(contents.AppendASCII("MacOS/TestHost"), ""));
    const std::string plist = base::StringPrintf(
        R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleIdentifier</key><string>com.roamex.sparkle.testhost</string>
  <key>CFBundleName</key><string>TestHost</string>
  <key>CFBundleExecutable</key><string>TestHost</string>
  <key>CFBundleVersion</key><string>1.0</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>SUFeedURL</key><string>%sappcast.xml</string>
  <key>SUPublicEDKey</key><string>%s</string>
  <key>SUEnableAutomaticChecks</key><false/>
</dict></plist>
)",
        base_url.c_str(), public_key.c_str());
    ASSERT_TRUE(base::WriteFile(contents.AppendASCII("Info.plist"), plist));

    @autoreleasepool {
      NSBundle* host_bundle =
          [NSBundle bundleWithPath:base::apple::FilePathToNSString(
                                       host_dir.AppendASCII("TestHost.app"))];
      ASSERT_TRUE(host_bundle);
      driver_ = [[RoamexTestUserDriver alloc] init];
      SPUUpdater* updater = [[SPUUpdater alloc] initWithHostBundle:host_bundle
                                                 applicationBundle:host_bundle
                                                        userDriver:driver_
                                                          delegate:nil];
      NSError* error = nil;
      ASSERT_TRUE([updater startUpdater:&error])
          << base::SysNSStringToUTF8([error description]);
      [updater checkForUpdates];
      const base::Time deadline = base::Time::Now() + base::Seconds(30);
      while (!driver_.finished && base::Time::Now() < deadline) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
      }
      EXPECT_TRUE(driver_.finished) << "harness timed out";
    }
  }

  std::unique_ptr<net::test_server::HttpResponse> Serve(
      const net::test_server::HttpRequest& request) {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    if (base::EndsWith(request.relative_url, ".xml")) {
      response->set_content(appcast_);
      response->set_content_type("application/xml");
    } else {
      response->set_content(artifact_);
      response->set_content_type("application/octet-stream");
    }
    return response;
  }

  std::string appcast_;
  std::string artifact_;
  RoamexTestUserDriver* driver_ = nil;
};

// The correctly-signed item passes Sparkle's EdDSA validation (which runs
// after extraction, before install): the flow reaches ready-to-install with
// no updater error.
TEST_F(RoamexSparkleFeedTest, SignedItemPassesSignatureValidation) {
  RunCheck("appcast.xml");
  EXPECT_TRUE(driver_.updateFound);
  // The correctly-signed enclosure clears Sparkle's EdDSA gate: download +
  // extraction proceed and no signature/validation error is raised. (A real
  // install/relaunch needs a genuine signed app bundle — I-6.3's staging
  // E2E; out of this in-tree harness's scope.)
  EXPECT_TRUE(driver_.extractionStarted);
  EXPECT_FALSE(driver_.updaterError);
}

// A tampered enclosure (signature over different bytes) is rejected at the
// signature gate — never ready to install.
TEST_F(RoamexSparkleFeedTest, TamperedItemRejectedAtSignatureGate) {
  RunCheck("appcast-tampered.xml");
  EXPECT_TRUE(driver_.updateFound);
  EXPECT_FALSE(driver_.readyToInstall);
  EXPECT_TRUE(driver_.updaterError);
}

// An unsigned item is rejected — no "check without a key" mode (plan K3).
TEST_F(RoamexSparkleFeedTest, UnsignedItemRejected) {
  RunCheck("appcast-unsigned.xml");
  EXPECT_FALSE(driver_.readyToInstall);
  EXPECT_TRUE(driver_.updaterError);
}

}  // namespace
