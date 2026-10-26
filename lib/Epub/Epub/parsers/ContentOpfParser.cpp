#include "ContentOpfParser.h"

#include <Logging.h>
#include <Serialization.h>

#include "Epub/BookMetadataCache.h"

namespace {
constexpr char itemCacheFile[] = "/.items.bin";
std::string value(const XpPkgSpan& span) { return {reinterpret_cast<const char*>(span.data), span.len}; }
}  // namespace
bool ContentOpfParser::setup() {
  xp_pkg_destroy(parser);
  parser = nullptr;
  if (xp_pkg_opf_create(reinterpret_cast<const uint8_t*>(baseContentPath.data()), baseContentPath.size(), metadataOnly,
                        cache != nullptr, &parser) != 0) {
    LOG_ERR("COF", "Could not allocate OPF parser");
    return false;
  }
  return true;
}
ContentOpfParser::~ContentOpfParser() {
  xp_pkg_destroy(parser);
  if (metadataOnly || !cache) return;
  if (tempItemStore) tempItemStore.close();
  const auto path = cachePath + itemCacheFile;
  if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
}
size_t ContentOpfParser::write(uint8_t byte) { return write(&byte, 1); }
size_t ContentOpfParser::write(const uint8_t* bytes, size_t size) {
  if (!parser) return 0;
  size_t offset = 0;
  while (offset < size) {
    const size_t count = std::min(size - offset, size_t{1024});
    size_t consumed = 0;
    bool ok = xp_pkg_feed(parser, bytes + offset, count, remainingSize == count, &consumed) == 0;
    while (ok) {
      XpPkgRecord record{};
      if (xp_pkg_next(parser, &record) != 0 || record.kind == XP_PKG_ERROR) {
        ok = false;
        break;
      }
      if (record.kind == XP_PKG_NEED_INPUT || record.kind == XP_PKG_DONE) break;
      if (record.kind == XP_PKG_OPF) apply(record);
    }
    if (!ok || consumed != count) {
      LOG_ERR("COF", "OPF parse failed");
      xp_pkg_destroy(parser);
      parser = nullptr;
      return 0;
    }
    offset += count;
    remainingSize -= count;
    if (metadataOnly && metadataComplete) return offset < size ? offset : size - 1;
  }
  return size;
}
void ContentOpfParser::apply(const XpPkgRecord& record) {
  std::string* fields[] = {&title,
                           &author,
                           &language,
                           &isbn,
                           &asin,
                           &series,
                           &tocNcxPath,
                           &tocNavPath,
                           &coverItemHref,
                           &guideCoverPageHref,
                           &textReferenceHref};
  if (record.field <= XP_PKG_TEXT) {
    fields[record.field]->assign(reinterpret_cast<const char*>(record.value.data), record.value.len);
    if (record.field == XP_PKG_SERIES)
      seriesIndex = record.number_present ? std::optional<float>(record.number) : std::nullopt;
    return;
  }
  switch (record.field) {
    case XP_PKG_MANIFEST_BEGIN:
      if (cache && !Storage.openFileForWrite("COF", cachePath + itemCacheFile, tempItemStore))
        LOG_ERR("COF", "Could not open temporary item store for writing");
      break;
    case XP_PKG_SPINE_BEGIN:
      if (cache && !Storage.openFileForRead("COF", cachePath + itemCacheFile, tempItemStore))
        LOG_ERR("COF", "Could not open temporary item store for reading");
      if (!itemIndex.empty()) {
        std::sort(itemIndex.begin(), itemIndex.end(), [](const ItemIndexEntry& a, const ItemIndexEntry& b) {
          return a.idHash < b.idHash || (a.idHash == b.idHash && a.idLen < b.idLen);
        });
        useItemIndex = true;
      }
      break;
    case XP_PKG_GUIDE_BEGIN:
      if (cache && !Storage.openFileForRead("COF", cachePath + itemCacheFile, tempItemStore))
        LOG_ERR("COF", "Could not open temporary item store for reading");
      break;
    case XP_PKG_STORE_END:
      if (tempItemStore) tempItemStore.close();
      break;
    case XP_PKG_MANIFEST:
      if (tempItemStore) {
        const auto id = value(record.value);
        const auto href = value(record.href);
        itemIndex.push_back(
            {fnvHash(id), static_cast<uint16_t>(id.size()), static_cast<uint32_t>(tempItemStore.position())});
        serialization::writeString(tempItemStore, id);
        serialization::writeString(tempItemStore, href);
      }
      break;
    case XP_PKG_SPINE: {
      const auto idref = value(record.value);
      std::string href;
      bool found = false;
      if (useItemIndex) {
        const auto hash = fnvHash(idref);
        const auto length = static_cast<uint16_t>(idref.size());
        auto it = std::lower_bound(itemIndex.begin(), itemIndex.end(), ItemIndexEntry{hash, length, 0},
                                   [](const ItemIndexEntry& a, const ItemIndexEntry& b) {
                                     return a.idHash < b.idHash || (a.idHash == b.idHash && a.idLen < b.idLen);
                                   });
        while (it != itemIndex.end() && it->idHash == hash) {
          tempItemStore.seek(it->fileOffset);
          std::string id;
          serialization::readString(tempItemStore, id);
          if (id == idref) {
            serialization::readString(tempItemStore, href);
            found = true;
            break;
          }
          ++it;
        }
      } else {
        tempItemStore.seek(0);
        std::string id;
        while (tempItemStore.available()) {
          serialization::readString(tempItemStore, id);
          serialization::readString(tempItemStore, href);
          if (id == idref) {
            found = true;
            break;
          }
        }
      }
      if (found && cache) cache->createSpineEntry(href);
      break;
    }
    case XP_PKG_CSS:
      cssFiles.push_back(value(record.href));
      break;
    case XP_PKG_COMPLETE:
      metadataComplete = true;
      break;
  }
}
