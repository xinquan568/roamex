// SPDX-License-Identifier: Apache-2.0
// roam-20 (I-3.6): the production import driver end-to-end — it runs the
// browser-side carriers (secrets = the roam-16 carry-forward, via the roam-19
// coordinator) for the user-selected items, imports origin storage into the
// destination profile, and reports. Selected secrets are HANDLED (reported),
// not silently skipped. Flag-gated. Models
// roamux_edge_import_coordinator_browsertest.cc.

#include "roamux/browser/importer/roamux_edge_import_driver.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/services/storage/privileged/mojom/indexed_db_control.mojom.h"
#include "components/services/storage/privileged/mojom/indexed_db_control_test.mojom.h"
#include "components/services/storage/public/mojom/local_storage_control.mojom.h"
#include "components/user_data_importer/common/importer_data_types.h"
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

class RoamuxEdgeImportDriverTestBase : public InProcessBrowserTest {
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

  void SeedOriginStorage(const GURL& origin) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
    ASSERT_TRUE(
        content::ExecJs(web(), "localStorage.setItem('auth','lsval');"));
    ASSERT_EQ("ok", content::EvalJs(web(), R"(
      new Promise((resolve, reject) => {
        const o = indexedDB.open('rx', 1);
        o.onupgradeneeded = () => o.result.createObjectStore('s');
        o.onsuccess = () => {
          const db = o.result, tx = db.transaction('s', 'readwrite');
          tx.objectStore('s').put('idbval', 'k');
          tx.oncomplete = () => { db.close(); resolve('ok'); };
          tx.onerror = () => reject('idb');
        };
        o.onerror = () => reject('open');
      }))"));
    partition()->GetLocalStorageControl()->Flush();
    mojo::Remote<storage::mojom::IndexedDBControlTest> t;
    partition()->GetIndexedDBControl().BindTestInterfaceForTesting(
        t.BindNewPipeAndPassReceiver());
    base::test::TestFuture<const base::FilePath&> p;
    t->GetBaseDataPathForTesting(p.GetCallback());
    ASSERT_FALSE(p.Get().empty());
  }

  void SnapshotEdge(const base::FilePath& app_data_root) {
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
        "150.0.0.0"));
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

  EdgeImportReport RunDriver(const base::FilePath& app_data_root,
                             uint16_t items) {
    RoamuxEdgeImportDriver driver(browser()->profile(), app_data_root, items,
                                  /*keychain_for_testing=*/nullptr);
    base::test::TestFuture<EdgeImportReport> future;
    driver.Start(future.GetCallback());
    EXPECT_TRUE(future.Wait());
    return future.Take();
  }
};

class RoamuxEdgeImportDriverTest : public RoamuxEdgeImportDriverTestBase {
 public:
  RoamuxEdgeImportDriverTest() {
    features_.InitAndEnableFeature(features::kEdgeImport);
  }

 private:
  base::test::ScopedFeatureList features_;
};

// The driver imports the browser-side origin-storage carriers into the
// destination profile and reports them.
IN_PROC_BROWSER_TEST_F(RoamuxEdgeImportDriverTest,
                       ImportsBrowserSideCarriersAndReports) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  const GURL origin = embedded_test_server()->GetURL("/title1.html");
  SeedOriginStorage(origin);

  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  SnapshotEdge(edge.GetPath());
  ClearOriginStorage(origin);

  // Only non-secret items selected; the driver always imports origin storage.
  EdgeImportReport report =
      RunDriver(edge.GetPath(), user_data_importer::HISTORY);

  ASSERT_TRUE(report.Find(EdgeCarrier::kLocalStorage));
  EXPECT_EQ(CarrierStatus::kImported,
            report.Find(EdgeCarrier::kLocalStorage)->status);
  ASSERT_TRUE(report.Find(EdgeCarrier::kIndexedDb));
  EXPECT_EQ(CarrierStatus::kImported,
            report.Find(EdgeCarrier::kIndexedDb)->status);

  ForceInitIdb();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
  EXPECT_EQ("lsval", content::EvalJs(web(), "localStorage.getItem('auth')"));
  EXPECT_EQ("idbval", content::EvalJs(web(), kReadIdbJs));
}

// When PASSWORDS|COOKIES are selected they are HANDLED (reported), not silently
// skipped — even with no source secrets (reported unsupported, no Keychain).
IN_PROC_BROWSER_TEST_F(RoamuxEdgeImportDriverTest,
                       SecretsSelectedAreHandledNotSkipped) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  const GURL origin = embedded_test_server()->GetURL("/title1.html");
  SeedOriginStorage(origin);

  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  SnapshotEdge(edge.GetPath());  // origin storage only; no Login Data / Cookies
  ClearOriginStorage(origin);

  const uint16_t items = user_data_importer::HISTORY |
                         user_data_importer::PASSWORDS |
                         user_data_importer::COOKIES;
  EdgeImportReport report = RunDriver(edge.GetPath(), items);

  // The secret carriers appear in the report (handled, not skipped); with no
  // source secrets they are unsupported — never dropped.
  ASSERT_TRUE(report.Find(EdgeCarrier::kPasswords));
  EXPECT_EQ(CarrierStatus::kUnsupported,
            report.Find(EdgeCarrier::kPasswords)->status);
  ASSERT_TRUE(report.Find(EdgeCarrier::kCookies));
  EXPECT_EQ(CarrierStatus::kUnsupported,
            report.Find(EdgeCarrier::kCookies)->status);
  // Origin storage still imports alongside.
  ASSERT_TRUE(report.Find(EdgeCarrier::kIndexedDb));
  EXPECT_EQ(CarrierStatus::kImported,
            report.Find(EdgeCarrier::kIndexedDb)->status);
}

class RoamuxEdgeImportDriverDisabledTest
    : public RoamuxEdgeImportDriverTestBase {
 public:
  RoamuxEdgeImportDriverDisabledTest() {
    features_.InitAndDisableFeature(features::kEdgeImport);
  }

 private:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(RoamuxEdgeImportDriverDisabledTest,
                       FeatureDisabledImportsNothing) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());

  EdgeImportReport report =
      RunDriver(edge.GetPath(), user_data_importer::PASSWORDS);
  ASSERT_TRUE(report.Find(EdgeCarrier::kIndexedDb));
  EXPECT_EQ(CarrierStatus::kFeatureDisabled,
            report.Find(EdgeCarrier::kIndexedDb)->status);
}

}  // namespace
}  // namespace roamux
