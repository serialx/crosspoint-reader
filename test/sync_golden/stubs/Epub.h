#pragma once

#include <Print.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class Epub {
 public:
  struct SpineItem {
    std::string href;
  };

  explicit Epub(std::vector<std::string> spineContents) : spineContents(std::move(spineContents)) {}

  size_t testChunkSize = 0;
  bool failRead = false;
  size_t getBookSize() const { return getCumulativeSpineItemSize(getSpineItemsCount()-1); }
  size_t getCumulativeSpineItemSize(int n) const { size_t total=0; for(int i=0;i<=n && i<getSpineItemsCount();++i) total+=spineContents[i].size(); return total; }
  int getSpineIndexForSize(size_t n) const { for(int i=0;i<getSpineItemsCount();++i) if(n<getCumulativeSpineItemSize(i)) return i; return -1; }
  bool getItemSize(const std::string& href,size_t* n) const { for(int i=0;i<getSpineItemsCount();++i) if(href==getSpineItem(i).href) { *n=spineContents[i].size(); return true;} return false; }
  float calculateProgress(int spine,float intra) const { const size_t prev=spine>0?getCumulativeSpineItemSize(spine-1):0; return getBookSize()?(prev+(getCumulativeSpineItemSize(spine)-prev)*intra)/getBookSize():0; }


  int getSpineItemsCount() const { return static_cast<int>(spineContents.size()); }

  SpineItem getSpineItem(const int spineIndex) const {
    if (spineIndex < 0 || spineIndex >= getSpineItemsCount()) {
      return {};
    }
    return {"chapter" + std::to_string(spineIndex) + ".xhtml"};
  }

  bool readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize, bool = false) const {
    if (failRead) return false;
    for (int spineIndex = 0; spineIndex < getSpineItemsCount(); spineIndex++) {
      if (itemHref != getSpineItem(spineIndex).href) {
        continue;
      }

      const auto& contents = spineContents[spineIndex];
      size_t offset = 0;
      while (offset < contents.size()) {
        const size_t size = std::min(testChunkSize ? testChunkSize : chunkSize, contents.size() - offset);
        out.write(reinterpret_cast<const uint8_t*>(contents.data() + offset), size);
        offset += size;
      }
      return true;
    }
    return false;
  }

 private:
  std::vector<std::string> spineContents;
};
