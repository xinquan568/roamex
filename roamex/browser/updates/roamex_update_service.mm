// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/updates/roamex_update_service.h"

#import <Sparkle/Sparkle.h>

#include <utility>

#include "base/functional/callback.h"
#include "base/strings/sys_string_conversions.h"
#include "roamex/app/roamex_sparkle_updater.h"

namespace roamex::updates {

namespace {

// Callback the conformer uses to hand a translated event back to the service.
using EventCallback = base::RepeatingCallback<void(const UpdateEvent&)>;

}  // namespace

}  // namespace roamex::updates

// The custom SPUUserDriver: translates each Sparkle callback into an
// UpdateEvent (roam-85) and stores the found/ready reply blocks so the service
// commands can drive them (Sparkle has no Download/Install methods).
@interface RoamexUpdateUserDriver : NSObject <SPUUserDriver>
@property(nonatomic, copy) void (^foundReply)(SPUUserUpdateChoice);
@property(nonatomic, copy) void (^readyReply)(SPUUserUpdateChoice);
- (instancetype)initWithCallback:(roamex::updates::EventCallback)cb;
- (void)commandDownload;
- (void)commandInstallAndRelaunch;
- (void)commandSkipVersion:(NSString*)version;
@end

@implementation RoamexUpdateUserDriver {
  roamex::updates::EventCallback _cb;
  uint64_t _expectedLength;
  uint64_t _receivedLength;
}
@synthesize foundReply = _foundReply;
@synthesize readyReply = _readyReply;

- (instancetype)initWithCallback:(roamex::updates::EventCallback)cb {
  if ((self = [super init])) {
    _cb = std::move(cb);
  }
  return self;
}

- (void)emit:(roamex::updates::UpdateEventType)type {
  roamex::updates::UpdateEvent e;
  e.type = type;
  _cb.Run(e);
}

- (void)commandDownload {
  if (self.foundReply) {
    self.foundReply(SPUUserUpdateChoiceInstall);
    self.foundReply = nil;
  }
}

- (void)commandInstallAndRelaunch {
  if (self.readyReply) {
    self.readyReply(SPUUserUpdateChoiceInstall);
    self.readyReply = nil;
  }
}

- (void)commandSkipVersion:(NSString*)version {
  if (self.foundReply) {
    self.foundReply(SPUUserUpdateChoiceSkip);
    self.foundReply = nil;
  }
}

// SPUUserDriver — the state-driving callbacks:
- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest*)request
                              reply:
                                  (void (^)(SUUpdatePermissionResponse*))reply {
  reply([[SUUpdatePermissionResponse alloc] initWithAutomaticUpdateChecks:YES
                                                        sendSystemProfile:NO]);
}
- (void)showUserInitiatedUpdateCheckWithCancellation:
    (void (^)(void))cancellation {
  [self emit:roamex::updates::UpdateEventType::kCheckStarted];
}
- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem*)appcastItem
                                 state:(SPUUserUpdateState*)state
                                 reply:(void (^)(SPUUserUpdateChoice))reply {
  self.foundReply = reply;
  roamex::updates::UpdateEvent e;
  e.type = roamex::updates::UpdateEventType::kUpdateFound;
  e.version = base::SysNSStringToUTF8(appcastItem.displayVersionString
                                          ?: appcastItem.versionString);
  e.notes = base::SysNSStringToUTF8(appcastItem.itemDescription ?: @"");
  _cb.Run(e);
}
- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData*)downloadData {
}
- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError*)error {
}
- (void)showUpdateNotFoundWithError:(NSError*)error
                    acknowledgement:(void (^)(void))acknowledgement {
  [self emit:roamex::updates::UpdateEventType::kUpToDate];
  acknowledgement();
}
- (void)showUpdaterError:(NSError*)error
         acknowledgement:(void (^)(void))acknowledgement {
  roamex::updates::UpdateEvent e;
  e.type = roamex::updates::UpdateEventType::kError;
  e.error = base::SysNSStringToUTF8(error.localizedDescription);
  _cb.Run(e);
  acknowledgement();
}
- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancellation {
  [self emit:roamex::updates::UpdateEventType::kDownloadStarted];
}
- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)length {
  _expectedLength = length;
}
- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length {
  _receivedLength += length;
  roamex::updates::UpdateEvent e;
  e.type = roamex::updates::UpdateEventType::kDownloadProgress;
  e.received = static_cast<int64_t>(_receivedLength);
  e.total = static_cast<int64_t>(_expectedLength);
  _cb.Run(e);
}
- (void)showDownloadDidStartExtractingUpdate {
}
- (void)showExtractionReceivedProgress:(double)progress {
}
- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply {
  self.readyReply = reply;
  [self emit:roamex::updates::UpdateEventType::kReadyToInstall];
}
- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)terminated
                          retryTerminatingApplication:(void (^)(void))retry {
}
- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched
                         acknowledgement:(void (^)(void))acknowledgement {
  acknowledgement();
}
- (void)dismissUpdateInstallation {
}
- (void)showUpdateInFocus {
}

@end

namespace roamex::updates {

// The process-wide Sparkle owner: exactly one SPUUpdater + conformer for the
// whole app (updates are app-wide), shared by every per-profile service.
// One process-wide Sparkle owner shared by every per-profile facade (updates
// are app-wide). Ref-counted by the facades; the single SPUUpdater is created
// on first Acquire and torn down when the last facade Releases. The event
// callback is re-pointed at whichever facade currently holds the reference via
// a shared sink, so a facade going away never leaves a dangling callback.
class SparkleOwner {
 public:
  static SparkleOwner* AcquireShared(EventCallback cb);
  void Release();

  void SetEventSink(EventCallback cb) { *sink_ = std::move(cb); }

  void CheckForUpdates() { [updater_ checkForUpdates]; }
  void Download() { [driver_ commandDownload]; }
  void InstallAndRelaunch() { [driver_ commandInstallAndRelaunch]; }
  void Skip(const std::string& version) {
    [driver_ commandSkipVersion:base::SysUTF8ToNSString(version)];
  }

 private:
  SparkleOwner() {
    sink_ = std::make_shared<EventCallback>();
    auto sink = sink_;
    driver_ = [[RoamexUpdateUserDriver alloc]
        initWithCallback:base::BindRepeating(
                             [](std::shared_ptr<EventCallback> s,
                                const UpdateEvent& e) {
                               if (*s) {
                                 s->Run(e);
                               }
                             },
                             sink)];
    updater_ = [[SPUUpdater alloc] initWithHostBundle:[NSBundle mainBundle]
                                    applicationBundle:[NSBundle mainBundle]
                                           userDriver:driver_
                                             delegate:nil];
    NSError* error = nil;
    [updater_ startUpdater:&error];
  }
  ~SparkleOwner() = default;

  int refcount_ = 0;
  std::shared_ptr<EventCallback> sink_;
  RoamexUpdateUserDriver* driver_ = nil;
  SPUUpdater* updater_ = nil;
};

SparkleOwner* g_process_owner = nullptr;

// static
SparkleOwner* SparkleOwner::AcquireShared(EventCallback cb) {
  if (!g_process_owner) {
    g_process_owner = new SparkleOwner();
  }
  ++g_process_owner->refcount_;
  g_process_owner->SetEventSink(std::move(cb));
  return g_process_owner;
}

void SparkleOwner::Release() {
  if (--refcount_ <= 0) {
    if (g_process_owner == this) {
      g_process_owner = nullptr;
    }
    delete this;
  }
}

namespace {
// The menu "Check for Updates…" seam routes here (roam-85: one owner).
void RouteMenuCheckToOwner() {
  if (g_process_owner) {
    g_process_owner->CheckForUpdates();
  }
}
}  // namespace

RoamexUpdateService::RoamexUpdateService() {
  shared_owner_ = SparkleOwner::AcquireShared(base::BindRepeating(
      &RoamexUpdateService::OnUpdateEvent, weak_factory_.GetWeakPtr()));
  // Route the menu "Check for Updates…" to the single owner (roam-32 retires
  // its standard-controller path while a service exists).
  roamex::app::SetCheckForUpdatesHandler(&RouteMenuCheckToOwner);
}

RoamexUpdateService::~RoamexUpdateService() {
  if (shared_owner_) {
    shared_owner_->Release();
    shared_owner_ = nullptr;
  }
  // F2: no owner left -> the menu falls back to the standard controller.
  if (!g_process_owner) {
    roamex::app::SetCheckForUpdatesHandler(nullptr);
  }
}

void RoamexUpdateService::BindFactory(
    mojo::PendingReceiver<mojom::UpdatePageHandlerFactory> receiver) {
  factory_receiver_.reset();
  factory_receiver_.Bind(std::move(receiver));
}

void RoamexUpdateService::CreatePageHandler(
    mojo::PendingRemote<mojom::UpdatePage> page,
    mojo::PendingReceiver<mojom::UpdatePageHandler> handler) {
  page_.reset();
  page_.Bind(std::move(page));
  handler_receiver_.reset();
  handler_receiver_.Bind(std::move(handler));
  PushSnapshot(state_machine_.snapshot());
}

void RoamexUpdateService::CheckForUpdates() {
  shared_owner_->CheckForUpdates();
}
void RoamexUpdateService::Download() {
  shared_owner_->Download();
}
void RoamexUpdateService::InstallAndRelaunch() {
  shared_owner_->InstallAndRelaunch();
}
void RoamexUpdateService::Skip(const std::string& version) {
  state_machine_.SetSkippedVersion(version);
  shared_owner_->Skip(version);
}

void RoamexUpdateService::OnUpdateEvent(const UpdateEvent& event) {
  PushSnapshot(state_machine_.OnEvent(event));
}

void RoamexUpdateService::PushSnapshot(const UpdateSnapshot& snapshot) {
  if (!page_) {
    return;
  }
  auto out = mojom::UpdateSnapshot::New();
  out->status = static_cast<mojom::UpdateStatus>(snapshot.status);
  out->version = snapshot.version;
  out->date = snapshot.date;
  out->notes = snapshot.notes;
  out->error = snapshot.error;
  out->progress = snapshot.progress;
  page_->OnStateChanged(std::move(out));
}

}  // namespace roamex::updates
