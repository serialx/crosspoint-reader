#pragma once
#include "src/ClippingStore.h"

// Test-only adapter preserves the original operation corpus and result dump.
class RustStore {
 public:
  using AddResult = ClippingStore::AddResult;
  RustStore();
  ~RustStore();
  bool loadForBook(std::string_view, std::string_view, std::string_view, std::string_view);
  void unload();
  size_t clippingCount() const;
  const Clipping* clippingAt(size_t) const;
  AddResult addClipping(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, const char*, uint16_t,
                        const std::string&, uint32_t, uint32_t, uint32_t);
  bool removeClippingAt(size_t);
  bool saveToFile();
  bool prepareSync();
  bool finishUploads();
  bool applyRemote(const Clipping&, const std::string&, bool);
  bool nextDeletion(uint32_t&, char (&)[65]) const;
  bool finishDeletions();
  bool readClippingText(size_t, std::string&) const;
  bool readClippingPreview(size_t, char*, size_t, size_t&) const;
  static bool moveBook(const std::string&, const std::string&);
  static bool deleteForFilePath(const std::string&, const std::string&);

 private:
  void* owner;
  mutable Clipping snapshot;
};
