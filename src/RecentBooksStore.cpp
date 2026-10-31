#include "RecentBooksStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <StoreBridge.h>

#include <algorithm>

namespace {
template <typename T>
std::string& rowField(T& row, uint32_t field) {
  switch (field) {
    case 0:
      return row.path;
    case 1:
      return row.title;
    case 2:
      return row.author;
    case 3:
      return row.coverBmpPath;
    default:
      return row.path;
  }
}

}  // namespace

void RecentBooksStore::toJson(JsonDocument& doc) const {
  store_bridge::Rows<RecentBook, XP_STOREKIND_RECENT> rows{const_cast<std::vector<RecentBook>&>(recentBooks), nullptr,
                                                           rowField<RecentBook>, nullptr};
  store_bridge::save(XP_STOREKIND_RECENT, doc, rows);
}

bool RecentBooksStore::fromJson(JsonVariantConst doc) {
  RecentBook scratch;
  store_bridge::Rows<RecentBook, XP_STOREKIND_RECENT> rows{recentBooks, &scratch, rowField<RecentBook>, nullptr};
  const auto diagnostic = +[](void* ctx, uint32_t event, size_t a, size_t b, size_t) {
    (void)ctx;
    (void)b;
    switch (event) {
      case XP_ROWEVENT_LOADED:
        LOG_DBG("RBS", "Recent books loaded from file (%d entries)", static_cast<int>(a));
        break;
      default:
        break;
    }
  };
  if (store_bridge::load(XP_STOREKIND_RECENT, doc, rows, diagnostic)) requestResave();
  return true;
}

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath) {
  pruneMissing();
  RecentBook scratch;
  store_bridge::Rows<RecentBook, XP_STOREKIND_RECENT> rows{recentBooks, &scratch, rowField<RecentBook>};
  const auto ops = rows.ops(nullptr);
  const XpStoreBytes args[] = {store_bridge::bytes(path), store_bridge::bytes(title), store_bridge::bytes(author),
                               store_bridge::bytes(coverBmpPath)};
  xp_store_mutate(&ops, XP_MUTATION_RECENTADD, args);
  saveToFile();
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath) {
  store_bridge::Rows<RecentBook, XP_STOREKIND_RECENT> rows{recentBooks, nullptr, rowField<RecentBook>};
  const auto ops = rows.ops(nullptr);
  const XpStoreBytes args[] = {store_bridge::bytes(path), store_bridge::bytes(title), store_bridge::bytes(author),
                               store_bridge::bytes(coverBmpPath)};
  if (xp_store_mutate(&ops, XP_MUTATION_RECENTUPDATE, args) != 0) saveToFile();
}

bool RecentBooksStore::removeByPath(const std::string& path) {
  store_bridge::Rows<RecentBook, XP_STOREKIND_RECENT> rows{recentBooks, nullptr, rowField<RecentBook>};
  const auto ops = rows.ops(nullptr);
  const auto key = store_bridge::bytes(path);
  if (xp_store_mutate(&ops, XP_MUTATION_REMOVE, &key) == XP_MUTATIONRESULT_REJECTED) return false;
  if (!saveToFile()) LOG_ERR("RBS", "Failed to persist removal of recent book: %s", path.c_str());
  return true;
}

void RecentBooksStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                  const std::string& oldCachePath, const std::string& newCachePath) {
  store_bridge::Rows<RecentBook, XP_STOREKIND_RECENT> rows{recentBooks, nullptr, rowField<RecentBook>};
  const auto ops = rows.ops(nullptr);
  const auto index = xp_store_find(&ops, 0, store_bridge::bytes(oldPath));
  if (index == SIZE_MAX) return;
  auto& book = recentBooks[index];
  book.path = newPath;
  if (xp_store_prefix(store_bridge::bytes(book.coverBmpPath), store_bridge::bytes(oldCachePath))) {
    book.coverBmpPath = newCachePath + book.coverBmpPath.substr(oldCachePath.size());
  }
  saveToFile();
}

bool RecentBooksStore::isMissing(const RecentBook& book) { return !Storage.exists(book.path.c_str()); }

bool RecentBooksStore::pruneMissing() {
  store_bridge::Rows<RecentBook, XP_STOREKIND_RECENT> rows{recentBooks, nullptr, rowField<RecentBook>};
  const auto ops = rows.ops(nullptr);
  return xp_store_prune(
      &ops,
      [](void* ctx, size_t index) {
        return Storage.exists(static_cast<store_bridge::Rows<RecentBook, XP_STOREKIND_RECENT>*>(
                                  static_cast<store_bridge::RowStrings*>(ctx))
                                  ->values[index]
                                  .path.c_str());
      },
      [](void* ctx, size_t dst, size_t src) {
        auto& values = static_cast<store_bridge::Rows<RecentBook, XP_STOREKIND_RECENT>*>(
                           static_cast<store_bridge::RowStrings*>(ctx))
                           ->values;
        values[dst] = std::move(values[src]);
      });
}
