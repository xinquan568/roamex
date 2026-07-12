// SPDX-License-Identifier: Apache-2.0
// roam-19 (I-3.5): the Edge import coordinator end-to-end — flag-gate → detect
// → validate(source running + destination no-clobber) → per-carrier apply →
// best-effort report. Every negative path (Edge running, destination already
// initialized, unsupported/absent carrier, feature disabled) must yield a
// reduced import + a truthful report + an uncorrupted, un-clobbered
// destination. Modeled on roamux_three_carrier_survival_browsertest.cc.

#include "roamux/browser/importer/roamux_edge_import_coordinator.h"

#include <string>

#include "base/containers/flat_set.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/importer/profile_writer.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/services/storage/privileged/mojom/indexed_db_control.mojom.h"
#include "components/services/storage/privileged/mojom/indexed_db_control_test.mojom.h"
#include "components/services/storage/public/mojom/local_storage_control.mojom.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/dns/mock_host_resolver.h"
#include "roamux/browser/importer/edge_import_report.h"
#include "roamux/browser/importer/edge_import_types.h"
#include "roamux/common/roamux_features.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace roamux {
namespace {

constexpr char kSeedIdbJs[] = R"(
  new Promise((resolve, reject) => {
    const o = indexedDB.open('rx', 1);
    o.onupgradeneeded = () => o.result.createObjectStore('s');
    o.onsuccess = () => {
      const db = o.result, tx = db.transaction('s', 'readwrite');
      tx.objectStore('s').put(arguments_value, 'k');
      tx.oncomplete = () => { db.close(); resolve('ok'); };
      tx.onerror = () => reject('idb');
    };
    o.onerror = () => reject('open');
  })
)";
constexpr char kReadIdbJs[] = R"(
  new Promise((resolve) => {
    const o = indexedDB.open('rx', 1);
    o.onsuccess = () => {
      const g = o.result.transaction('s', 'readonly').objectStore('s').get('k');
      g.onsuccess = () => { resolve(g.result || '<none>'); o.result.close(); };
    };
    o.onerror = () => resolve('<openfail>');
  })
)";

// Shared helpers; feature state is set by the derived fixtures.
class RoamuxEdgeImportCoordinatorTestBase : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  content::WebContents* web() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }
  content::StoragePartition* partition() {
    return browser()->profile()->GetDefaultStoragePartition();
  }
  base::FilePath EdgeDefaultDir(const base::FilePath& app_data_root) {
    return app_data_root.Append(FILE_PATH_LITERAL("Microsoft Edge"))
        .Append(FILE_PATH_LITERAL("Default"));
  }

  // Seeds one IndexedDB record (key 'k' = `value`) + a localStorage entry in
  // the live browser and flushes both to disk.
  void SeedLiveCarriers(const GURL& origin, const std::string& value) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
    ASSERT_TRUE(
        content::ExecJs(web(), "localStorage.setItem('auth','lsval');"));
    ASSERT_EQ("ok", content::EvalJs(
                        web(), content::JsReplace(
                                   std::string("const arguments_value = $1;") +
                                       kSeedIdbJs,
                                   value)));
    partition()->GetLocalStorageControl()->Flush();
    // Drain the IDB sequence so the store is on disk.
    mojo::Remote<storage::mojom::IndexedDBControlTest> t;
    partition()->GetIndexedDBControl().BindTestInterfaceForTesting(
        t.BindNewPipeAndPassReceiver());
    base::test::TestFuture<const base::FilePath&> p;
    t->GetBaseDataPathForTesting(p.GetCallback());
    ASSERT_FALSE(p.Get().empty());
  }

  // Copies the live profile's Local Storage + IndexedDB into a fake Edge
  // profile dir and writes a `Last Version` for it.
  void SnapshotEdge(const base::FilePath& app_data_root,
                    const std::string& last_version) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    const base::FilePath src = browser()->profile()->GetPath();
    const base::FilePath dst = EdgeDefaultDir(app_data_root);
    ASSERT_TRUE(base::CreateDirectory(dst));
    ASSERT_TRUE(base::CopyDirectory(
        src.Append(FILE_PATH_LITERAL("Local Storage")),
        dst.Append(FILE_PATH_LITERAL("Local Storage")), true));
    ASSERT_TRUE(base::CopyDirectory(src.Append(FILE_PATH_LITERAL("IndexedDB")),
                                    dst.Append(FILE_PATH_LITERAL("IndexedDB")),
                                    true));
    ASSERT_TRUE(base::WriteFile(
        app_data_root.Append(FILE_PATH_LITERAL("Microsoft Edge"))
            .Append(FILE_PATH_LITERAL("Last Version")),
        last_version));
  }

  void ClearOriginStorage(const GURL& origin) {
    base::test::TestFuture<void> c1, c2;
    partition()->ClearDataForOrigin(
        content::StoragePartition::REMOVE_DATA_MASK_LOCAL_STORAGE,
        origin.DeprecatedGetOriginAsURL(), c1.GetCallback());
    ASSERT_TRUE(c1.Wait());
    partition()->ClearDataForOrigin(
        content::StoragePartition::REMOVE_DATA_MASK_INDEXEDDB,
        origin.DeprecatedGetOriginAsURL(), c2.GetCallback());
    ASSERT_TRUE(c2.Wait());
  }

  void ForceInitIdb() {
    mojo::Remote<storage::mojom::IndexedDBControlTest> t;
    partition()->GetIndexedDBControl().BindTestInterfaceForTesting(
        t.BindNewPipeAndPassReceiver());
    base::test::TestFuture<void> done;
    t->ForceInitializeFromFilesForTesting(done.GetCallback());
    ASSERT_TRUE(done.Wait());
  }

  EdgeImportReport RunCoordinator(const base::FilePath& app_data_root,
                                  base::flat_set<EdgeCarrier> carriers) {
    auto writer = base::MakeRefCounted<ProfileWriter>(browser()->profile());
    RoamuxEdgeImportCoordinator coordinator(
        app_data_root, browser()->profile(), std::move(writer),
        std::move(carriers), /*keychain_for_testing=*/nullptr);
    base::test::TestFuture<EdgeImportReport> future;
    coordinator.Run(future.GetCallback());
    EXPECT_TRUE(future.Wait());
    return future.Take();
  }
};

// The Edge-import feature enabled.
class RoamuxEdgeImportCoordinatorTest
    : public RoamuxEdgeImportCoordinatorTestBase {
 public:
  RoamuxEdgeImportCoordinatorTest() {
    scoped_features_.InitAndEnableFeature(features::kEdgeImport);
  }

 private:
  base::test::ScopedFeatureList scoped_features_;
};

// Happy path: localStorage + IndexedDB import into a fresh destination and both
// carriers survive a round-trip; the report is a clean import.
IN_PROC_BROWSER_TEST_F(RoamuxEdgeImportCoordinatorTest,
                       OriginStorageCarriersImportedAndSurvive) {
  // Declared first so it outlives (covers the teardown of) the ScopedTempDir.
  base::ScopedAllowBlockingForTesting allow_blocking;
  const GURL origin = embedded_test_server()->GetURL("/title1.html");
  SeedLiveCarriers(origin, "idbval");

  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  SnapshotEdge(edge.GetPath(), "150.0.3478.97");
  ClearOriginStorage(origin);

  EdgeImportReport report = RunCoordinator(
      edge.GetPath(), {EdgeCarrier::kLocalStorage, EdgeCarrier::kIndexedDb});

  EXPECT_TRUE(report.version_supported);
  EXPECT_FALSE(report.edge_running_detected);
  ASSERT_TRUE(report.Find(EdgeCarrier::kLocalStorage));
  EXPECT_EQ(CarrierStatus::kImported,
            report.Find(EdgeCarrier::kLocalStorage)->status);
  ASSERT_TRUE(report.Find(EdgeCarrier::kIndexedDb));
  EXPECT_EQ(CarrierStatus::kImported,
            report.Find(EdgeCarrier::kIndexedDb)->status);
  EXPECT_GE(report.Find(EdgeCarrier::kIndexedDb)->count, 1u);

  ForceInitIdb();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
  EXPECT_EQ("lsval", content::EvalJs(web(), "localStorage.getItem('auth')"));
  EXPECT_EQ("idbval", content::EvalJs(web(), kReadIdbJs));
}

// Edge running (SingletonLock present) hard-blocks the IndexedDB carrier; the
// destination is never touched.
IN_PROC_BROWSER_TEST_F(RoamuxEdgeImportCoordinatorTest,
                       EdgeRunningBlocksIndexedDbNoCorruption) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  const GURL origin = embedded_test_server()->GetURL("/title1.html");
  SeedLiveCarriers(origin, "idbval");

  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  SnapshotEdge(edge.GetPath(), "150.0.0.0");
  ClearOriginStorage(origin);
  ASSERT_TRUE(base::CreateSymbolicLink(
      base::FilePath(FILE_PATH_LITERAL("host-4321")),
      edge.GetPath()
          .Append(FILE_PATH_LITERAL("Microsoft Edge"))
          .Append(FILE_PATH_LITERAL("SingletonLock"))));

  EdgeImportReport report =
      RunCoordinator(edge.GetPath(), {EdgeCarrier::kIndexedDb});

  EXPECT_TRUE(report.edge_running_detected);
  ASSERT_TRUE(report.Find(EdgeCarrier::kIndexedDb));
  EXPECT_EQ(CarrierStatus::kBlocked,
            report.Find(EdgeCarrier::kIndexedDb)->status);
  EXPECT_EQ(0u, report.Find(EdgeCarrier::kIndexedDb)->count);
  EXPECT_TRUE(report.any_degraded());
}

// A destination that already holds an IndexedDB store blocks the carrier (the
// stage's "dest uninitialized" precondition) and the existing data is intact.
IN_PROC_BROWSER_TEST_F(RoamuxEdgeImportCoordinatorTest,
                       DestinationInitializedBlocksIndexedDbNoClobber) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  const GURL origin = embedded_test_server()->GetURL("/title1.html");
  SeedLiveCarriers(origin, "sourceval");

  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  SnapshotEdge(edge.GetPath(), "150.0.0.0");

  // Do NOT clear: the destination keeps a store. Overwrite the destination
  // value so a wrongful import would be observable.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
  ASSERT_EQ("ok", content::EvalJs(
                      web(), content::JsReplace(
                                 std::string("const arguments_value = $1;") +
                                     kSeedIdbJs,
                                 "destval")));

  EdgeImportReport report =
      RunCoordinator(edge.GetPath(), {EdgeCarrier::kIndexedDb});

  ASSERT_TRUE(report.Find(EdgeCarrier::kIndexedDb));
  EXPECT_EQ(CarrierStatus::kBlocked,
            report.Find(EdgeCarrier::kIndexedDb)->status);

  ForceInitIdb();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
  // The destination's own value survives — never clobbered by the source.
  EXPECT_EQ("destval", content::EvalJs(web(), kReadIdbJs));
}

// A source with no IndexedDB and an unsupported version reports unsupported for
// the carrier and version_supported=false; nothing is imported or corrupted.
IN_PROC_BROWSER_TEST_F(RoamuxEdgeImportCoordinatorTest,
                       UnsupportedVersionAndAbsentCarrierReported) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  // A profile dir + an unsupported Last Version, but no carrier data.
  ASSERT_TRUE(base::CreateDirectory(EdgeDefaultDir(edge.GetPath())));
  ASSERT_TRUE(base::WriteFile(edge.GetPath()
                                  .Append(FILE_PATH_LITERAL("Microsoft Edge"))
                                  .Append(FILE_PATH_LITERAL("Last Version")),
                              "152.0.1.2"));

  EdgeImportReport report =
      RunCoordinator(edge.GetPath(), {EdgeCarrier::kIndexedDb});

  EXPECT_FALSE(report.version_supported);
  ASSERT_TRUE(report.Find(EdgeCarrier::kIndexedDb));
  EXPECT_EQ(CarrierStatus::kUnsupported,
            report.Find(EdgeCarrier::kIndexedDb)->status);
}

// A destination that already holds localStorage data blocks the carrier
// (queried via the live store, not the always-present leveldb infra) and the
// existing value is intact.
IN_PROC_BROWSER_TEST_F(RoamuxEdgeImportCoordinatorTest,
                       DestinationInitializedBlocksLocalStorageNoClobber) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  const GURL origin = embedded_test_server()->GetURL("/title1.html");
  SeedLiveCarriers(origin, "idbval");

  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  SnapshotEdge(edge.GetPath(), "150.0.0.0");

  // Do NOT clear: the destination keeps localStorage. Set a distinct value so a
  // wrongful import would be observable.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
  ASSERT_TRUE(content::ExecJs(web(), "localStorage.setItem('auth','destls');"));
  partition()->GetLocalStorageControl()->Flush();

  EdgeImportReport report =
      RunCoordinator(edge.GetPath(), {EdgeCarrier::kLocalStorage});

  ASSERT_TRUE(report.Find(EdgeCarrier::kLocalStorage));
  EXPECT_EQ(CarrierStatus::kBlocked,
            report.Find(EdgeCarrier::kLocalStorage)->status);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
  EXPECT_EQ("destls", content::EvalJs(web(), "localStorage.getItem('auth')"));
}

// A corrupt source carrier is a reduced import — reported degraded, the other
// carrier still imports, and the destination stays valid (no corruption).
IN_PROC_BROWSER_TEST_F(RoamuxEdgeImportCoordinatorTest,
                       CorruptLocalStorageDegradesOtherCarrierSurvives) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  const GURL origin = embedded_test_server()->GetURL("/title1.html");
  SeedLiveCarriers(origin, "idbval");

  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  SnapshotEdge(edge.GetPath(), "150.0.0.0");

  // Corrupt the source localStorage LevelDB: the dir still exists (so the
  // carrier is "available"), but open fails (a bogus CURRENT, no MANIFEST) →
  // the reader soft-fails to empty. IndexedDB remains a valid source.
  const base::FilePath ls_leveldb =
      EdgeDefaultDir(edge.GetPath())
          .Append(FILE_PATH_LITERAL("Local Storage"))
          .Append(FILE_PATH_LITERAL("leveldb"));
  ASSERT_TRUE(base::DeletePathRecursively(ls_leveldb));
  ASSERT_TRUE(base::CreateDirectory(ls_leveldb));
  ASSERT_TRUE(base::WriteFile(ls_leveldb.AppendASCII("CURRENT"), "garbage\n"));

  ClearOriginStorage(origin);

  EdgeImportReport report = RunCoordinator(
      edge.GetPath(), {EdgeCarrier::kLocalStorage, EdgeCarrier::kIndexedDb});

  // localStorage: source present but nothing imported → reduced (degraded).
  ASSERT_TRUE(report.Find(EdgeCarrier::kLocalStorage));
  EXPECT_EQ(CarrierStatus::kDegraded,
            report.Find(EdgeCarrier::kLocalStorage)->status);
  EXPECT_EQ(0u, report.Find(EdgeCarrier::kLocalStorage)->count);
  EXPECT_TRUE(report.any_degraded());
  // IndexedDB (valid source) still imports.
  ASSERT_TRUE(report.Find(EdgeCarrier::kIndexedDb));
  EXPECT_EQ(CarrierStatus::kImported,
            report.Find(EdgeCarrier::kIndexedDb)->status);

  // The destination remains valid: the IndexedDB carrier round-trips.
  ForceInitIdb();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
  EXPECT_EQ("idbval", content::EvalJs(web(), kReadIdbJs));
}

// Feature disabled: every requested carrier reports feature-disabled and
// nothing runs. Uses its own fixture so the flag stays default-disabled.
class RoamuxEdgeImportCoordinatorDisabledTest
    : public RoamuxEdgeImportCoordinatorTestBase {
 public:
  RoamuxEdgeImportCoordinatorDisabledTest() {
    disable_features_.InitAndDisableFeature(features::kEdgeImport);
  }

 private:
  base::test::ScopedFeatureList disable_features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxEdgeImportCoordinatorDisabledTest,
                       FeatureDisabledImportsNothing) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  EdgeImportReport report = RunCoordinator(
      edge.GetPath(), {EdgeCarrier::kLocalStorage, EdgeCarrier::kIndexedDb});

  ASSERT_TRUE(report.Find(EdgeCarrier::kLocalStorage));
  EXPECT_EQ(CarrierStatus::kFeatureDisabled,
            report.Find(EdgeCarrier::kLocalStorage)->status);
  ASSERT_TRUE(report.Find(EdgeCarrier::kIndexedDb));
  EXPECT_EQ(CarrierStatus::kFeatureDisabled,
            report.Find(EdgeCarrier::kIndexedDb)->status);
}

}  // namespace
}  // namespace roamux
