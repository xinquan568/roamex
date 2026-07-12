// SPDX-License-Identifier: Apache-2.0
// roam-18 (I-3.4, §5.4): the IndexedDB auth carrier survives import — a real
// IndexedDB-API round-trip. A page seeds an IDB record + Blob; the store is
// snapshotted as the "Edge" fixture, the destination is cleared, the stage
// imports the fixture, and the record + blob are read back through the
// IndexedDB API.

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/services/storage/privileged/mojom/indexed_db_control.mojom.h"
#include "components/services/storage/privileged/mojom/indexed_db_control_test.mojom.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/dns/mock_host_resolver.h"
#include "roamux/browser/importer/roamux_indexed_db_import_stage.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace roamux {
namespace {

constexpr char kSeedJs[] = R"(
  new Promise((resolve, reject) => {
    const open = indexedDB.open('roamuxdb', 1);
    open.onupgradeneeded = () => open.result.createObjectStore('store');
    open.onerror = () => reject('open failed');
    open.onsuccess = () => {
      const db = open.result;
      const tx = db.transaction('store', 'readwrite');
      const os = tx.objectStore('store');
      os.put('token', 'auth');
      os.put(new Blob(['blobbytes'], {type: 'text/plain'}), 'blobkey');
      tx.oncomplete = () => { db.close(); resolve('seeded'); };
      tx.onerror = () => reject('tx failed');
    };
  })
)";

constexpr char kReadJs[] = R"(
  new Promise((resolve, reject) => {
    const open = indexedDB.open('roamuxdb', 1);
    open.onerror = () => reject('open failed');
    open.onsuccess = () => {
      const db = open.result;
      const tx = db.transaction('store', 'readonly');
      const os = tx.objectStore('store');
      const g = os.get('auth');
      g.onsuccess = () => {
        const b = os.get('blobkey');
        b.onsuccess = () => {
          const blob = b.result;
          if (!blob) { resolve(g.result + '|<noblob>'); db.close(); return; }
          blob.text().then(t => { resolve(g.result + '|' + t); db.close(); });
        };
      };
      tx.onerror = () => reject('tx failed');
    };
  })
)";

class RoamuxIndexedDbImportTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }
  content::StoragePartition* partition() {
    return browser()->profile()->GetDefaultStoragePartition();
  }
  mojo::Remote<storage::mojom::IndexedDBControlTest> ControlTest() {
    mojo::Remote<storage::mojom::IndexedDBControlTest> t;
    partition()->GetIndexedDBControl().BindTestInterfaceForTesting(
        t.BindNewPipeAndPassReceiver());
    return t;
  }
  base::FilePath BaseDataPath() {
    auto control = ControlTest();  // Held alive until the reply.
    base::test::TestFuture<const base::FilePath&> path;
    control->GetBaseDataPathForTesting(path.GetCallback());
    return path.Get();
  }
  void ForceInit() {
    auto control = ControlTest();  // Held alive until the reply.
    base::test::TestFuture<void> done;
    control->ForceInitializeFromFilesForTesting(done.GetCallback());
    ASSERT_TRUE(done.Wait());
  }
};

IN_PROC_BROWSER_TEST_F(RoamuxIndexedDbImportTest,
                       AuthCarrierSurvivesRoundTrip) {
  const GURL origin = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
  ASSERT_EQ("seeded", content::EvalJs(web_contents(), kSeedJs));

  // Snapshot the destination profile's IndexedDB (GetBaseDataPathForTesting
  // also drains the IDB task sequence) into a temp "Edge" profile dir.
  base::ScopedAllowBlockingForTesting allow_blocking;
  const base::FilePath idb_base = BaseDataPath();
  ASSERT_FALSE(idb_base.empty());
  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  const base::FilePath edge_idb =
      edge.GetPath().Append(FILE_PATH_LITERAL("IndexedDB"));
  ASSERT_TRUE(base::CopyDirectory(idb_base, edge_idb, /*recursive=*/true));

  // Reset the destination origin's IndexedDB so it is "fresh".
  {
    base::test::TestFuture<void> cleared;
    partition()->ClearDataForOrigin(
        content::StoragePartition::REMOVE_DATA_MASK_INDEXEDDB,
        origin.DeprecatedGetOriginAsURL(), cleared.GetCallback());
    ASSERT_TRUE(cleared.Wait());
  }

  // Import the "Edge" fixture into the destination profile.
  RoamuxIndexedDbImportStage stage(edge.GetPath(),
                                   browser()->profile()->GetPath());
  base::test::TestFuture<size_t> imported;
  stage.Import(imported.GetCallback());
  EXPECT_GE(imported.Get(), 1u);

  // Force IndexedDB to discover the copied files, then read back via the API.
  ForceInit();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
  EXPECT_EQ("token|blobbytes", content::EvalJs(web_contents(), kReadJs));
}

}  // namespace
}  // namespace roamux
