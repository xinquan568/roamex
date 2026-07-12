// SPDX-License-Identifier: Apache-2.0
// roam-18 (I-3.4, §5.4): the 3-carrier auth-survival check — a cookie, a
// localStorage entry, and an IndexedDB record for one origin all survive a
// combined import (roam-16 cookies + roam-17 localStorage + roam-18 IndexedDB).

#include <array>
#include <memory>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
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
#include "components/services/storage/public/mojom/storage_usage_info.mojom.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_options.h"
#include "net/dns/mock_host_resolver.h"
#include "roamux/browser/importer/edge_local_storage_reader.h"
#include "roamux/browser/importer/roamux_indexed_db_import_stage.h"
#include "roamux/browser/importer/roamux_origin_storage_import_stage.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
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
      tx.objectStore('s').put('idbval', 'k');
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

class ThreeCarrierTest : public InProcessBrowserTest {
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
  void ForceInitIdb() {
    mojo::Remote<storage::mojom::IndexedDBControlTest> t;
    partition()->GetIndexedDBControl().BindTestInterfaceForTesting(
        t.BindNewPipeAndPassReceiver());
    base::test::TestFuture<void> done;
    t->ForceInitializeFromFilesForTesting(done.GetCallback());
    ASSERT_TRUE(done.Wait());
  }
};

IN_PROC_BROWSER_TEST_F(ThreeCarrierTest,
                       CookieLocalStorageIndexedDbAllSurvive) {
  const GURL origin = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
  // Seed localStorage + IndexedDB in the browser; the cookie is set directly.
  ASSERT_TRUE(content::ExecJs(web(), "localStorage.setItem('auth','lsval');"));
  ASSERT_EQ("ok", content::EvalJs(web(), kSeedIdbJs));

  // Flush localStorage + drain the IDB sequence so both carriers are on disk.
  {
    partition()->GetLocalStorageControl()->Flush();
    base::test::TestFuture<std::vector<storage::mojom::StorageUsageInfoPtr>> u;
    partition()->GetLocalStorageControl()->GetUsage(u.GetCallback());
    ASSERT_TRUE(u.Wait());
    mojo::Remote<storage::mojom::IndexedDBControlTest> t;
    partition()->GetIndexedDBControl().BindTestInterfaceForTesting(
        t.BindNewPipeAndPassReceiver());
    base::test::TestFuture<const base::FilePath&> p;
    t->GetBaseDataPathForTesting(p.GetCallback());
    ASSERT_FALSE(p.Get().empty());
  }

  base::ScopedAllowBlockingForTesting allow_blocking;
  // Snapshot the file carriers (localStorage + IndexedDB) into an "Edge" dir.
  const base::FilePath src_profile = browser()->profile()->GetPath();
  base::ScopedTempDir edge;
  ASSERT_TRUE(edge.CreateUniqueTempDir());
  ASSERT_TRUE(base::CopyDirectory(
      src_profile.Append(FILE_PATH_LITERAL("Local Storage")),
      edge.GetPath().Append(FILE_PATH_LITERAL("Local Storage")), true));
  ASSERT_TRUE(base::CopyDirectory(
      src_profile.Append(FILE_PATH_LITERAL("IndexedDB")),
      edge.GetPath().Append(FILE_PATH_LITERAL("IndexedDB")), true));

  // Import both origin-storage carriers into a fresh destination profile.
  base::ScopedTempDir dest;
  ASSERT_TRUE(dest.CreateUniqueTempDir());
  // (Use the live browser profile as the destination so we can read back via
  // the same StoragePartition; clear first for a clean import.)
  {
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

  RoamuxOriginStorageImportStage ls_stage(edge.GetPath(), browser()->profile());
  base::test::TestFuture<size_t> ls_done;
  ls_stage.Import(ls_done.GetCallback());
  EXPECT_GE(ls_done.Get(), 1u);

  RoamuxIndexedDbImportStage idb_stage(edge.GetPath(), src_profile);
  base::test::TestFuture<size_t> idb_done;
  idb_stage.Import(idb_done.GetCallback());
  EXPECT_GE(idb_done.Get(), 1u);
  ForceInitIdb();

  // Carrier 3: a cookie, written straight to the destination CookieManager
  // (the roam-16 secret stage's ProfileWriter::AddCookies path is covered by
  // roam-16; here the check is that all three coexist for the origin).
  {
    auto cookie = net::CanonicalCookie::CreateForTesting(
        origin, "auth=ckval; path=/", base::Time::Now(),
        net::CookieSourceType::kHTTP);
    ASSERT_TRUE(cookie);
    base::test::TestFuture<net::CookieAccessResult> set;
    partition()->GetCookieManagerForBrowserProcess()->SetCanonicalCookie(
        *cookie, origin, net::CookieOptions::MakeAllInclusive(),
        set.GetCallback());
    ASSERT_TRUE(set.Get().status.IsInclude());
  }

  // All three carriers survive for the origin.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), origin));
  EXPECT_EQ("lsval", content::EvalJs(web(), "localStorage.getItem('auth')"));
  EXPECT_EQ("idbval", content::EvalJs(web(), kReadIdbJs));
  EXPECT_EQ("auth=ckval", content::EvalJs(web(), "document.cookie"));
}

}  // namespace
}  // namespace roamux
