#pragma once

#include <HalStorage.h>

#include <string>
#include <vector>

class CssParser;

// OPF/ZIP discovery is done by run.py; chapter parsing and serialization stay in C++.
class Epub {
 public:
  struct SpineItem {
    std::string href;
  };
  struct TocItem {
    int spineIndex;
    std::string anchor;
  };
  std::vector<SpineItem> spine;
  std::vector<TocItem> toc;
  std::string language;
  CssParser* css = nullptr;

  std::string getCachePath() const { return "cache"; }
  const SpineItem& getSpineItem(int i) const { return spine.at(i); }
  int getTocItemsCount() const { return static_cast<int>(toc.size()); }
  const TocItem& getTocItem(int i) const { return toc.at(i); }
  int getTocIndexForSpineIndex(int spineIndex) const {
    for (size_t i = 0; i < toc.size(); ++i) {
      if (toc[i].spineIndex == spineIndex) return static_cast<int>(i);
    }
    return -1;
  }
  CssParser* getCssParser() const { return css; }
  const std::string& getLanguage() const { return language; }

  template <typename Output>
  bool readItemContentsToStream(const std::string& path, Output& output, size_t chunkSize,
                                bool allowEarlyStop = false) const {
    HalFile file;
    if (!file.open(("book/" + path).c_str(), "rb")) return false;
    std::vector<uint8_t> buffer(chunkSize);
    while (file.available()) {
      const size_t got = file.read(buffer.data(), buffer.size());
      if (!got) return false;
      if (output.write(buffer.data(), got) != got) return allowEarlyStop;
    }
    return true;
  }
};
