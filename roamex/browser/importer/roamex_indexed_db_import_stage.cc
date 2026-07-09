// SPDX-License-Identifier: Apache-2.0
#include "roamex/browser/importer/roamex_indexed_db_import_stage.h"

#include <map>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "roamex/browser/importer/edge_indexed_db_reader.h"

namespace roamex {

namespace {

// The origin "base" for grouping related store dirs: for legacy
// `<id>.indexeddb.leveldb` / `<id>.indexeddb.blob`, the base is `<id>`; for a
// self-contained SQLite `<id>` dir, the base is the dir name itself. Grouping
// makes a legacy leveldb+blob pair publish atomically together.
base::FilePath::StringType OriginBase(const base::FilePath& store) {
  base::FilePath name = store.BaseName();
  if (name.Extension() == FILE_PATH_LITERAL(".leveldb") ||
      name.Extension() == FILE_PATH_LITERAL(".blob")) {
    base::FilePath stripped = name.RemoveExtension();
    if (stripped.Extension() == FILE_PATH_LITERAL(".indexeddb")) {
      return stripped.RemoveExtension().value();
    }
  }
  return name.value();
}

size_t CopyStores(base::FilePath source_path, base::FilePath dest_profile_dir) {
  const base::FilePath dest_idb =
      dest_profile_dir.Append(FILE_PATH_LITERAL("IndexedDB"));
  if (!base::CreateDirectory(dest_idb)) {
    return 0;
  }
  // Group the source store dirs by origin base.
  std::map<base::FilePath::StringType, std::vector<base::FilePath>> groups;
  for (const base::FilePath& store :
       ListFirstPartyIndexedDbStores(source_path)) {
    groups[OriginBase(store)].push_back(store);
  }

  size_t count = 0;
  int seq = 0;
  for (const auto& [origin, members] : groups) {
    // no-clobber: never overwrite a live destination store for this origin.
    bool any_dest_exists = false;
    for (const base::FilePath& src : members) {
      if (base::PathExists(dest_idb.Append(src.BaseName()))) {
        any_dest_exists = true;
        break;
      }
    }
    if (any_dest_exists) {
      continue;
    }

    // Stage every member to a unique temp, then publish all; roll back
    // everything (temps + any final path created) on ANY failure.
    std::vector<std::pair<base::FilePath, base::FilePath>>
        staged;  // staging→final
    std::vector<base::FilePath> published;
    bool ok = true;
    for (const base::FilePath& src : members) {
      const base::FilePath staging = dest_idb.Append(
          src.BaseName().value() + FILE_PATH_LITERAL(".roamex-tmp-") +
          base::NumberToString(seq++).c_str());
      if (!base::CopyDirectory(src, staging, /*recursive=*/true)) {
        ok = false;
        break;
      }
      staged.emplace_back(staging, dest_idb.Append(src.BaseName()));
    }
    if (ok) {
      for (const auto& [staging, final_path] : staged) {
        if (!base::Move(staging, final_path)) {
          ok = false;
          break;
        }
        published.push_back(final_path);
      }
    }
    if (!ok) {
      for (const auto& [staging, final_path] : staged) {
        base::DeletePathRecursively(staging);
      }
      for (const base::FilePath& p : published) {
        base::DeletePathRecursively(p);
      }
      continue;  // don't count a half-published origin.
    }
    ++count;
  }
  return count;
}

}  // namespace

RoamexIndexedDbImportStage::RoamexIndexedDbImportStage(
    base::FilePath source_path,
    base::FilePath dest_profile_dir)
    : source_path_(std::move(source_path)),
      dest_profile_dir_(std::move(dest_profile_dir)) {}

RoamexIndexedDbImportStage::~RoamexIndexedDbImportStage() = default;

void RoamexIndexedDbImportStage::Import(
    base::OnceCallback<void(size_t stores)> done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&CopyStores, source_path_, dest_profile_dir_),
      std::move(done));
}

}  // namespace roamex
