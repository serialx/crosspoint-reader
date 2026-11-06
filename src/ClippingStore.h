#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#if defined(ARDUINO_ARCH_ESP32)
#include <sdkconfig.h>
#endif

inline constexpr size_t CLIPPING_CHAPTER_TITLE_MAX = 48;
// Clipping text lives on the SD card rather than in every in-memory clipping
// record. Match the reader's bounded selection-text budget so previews retain
// a complete multi-paragraph selection without growing the saved-item index.
inline constexpr size_t CLIPPING_TEXT_MAX = 4U * 1024U;
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
inline constexpr uint16_t CLIPPING_MAX_PER_BOOK = 1024;
#else
inline constexpr uint16_t CLIPPING_MAX_PER_BOOK = 256;
#endif

struct Clipping {
  uint16_t spineIndex = 0;
  uint16_t startPage = 0;
  uint16_t endPage = 0;
  uint16_t pageCount = 1;
  uint16_t startWordIndex = 0;
  uint16_t endWordIndex = 0;
  uint16_t wordCount = 0;
  uint16_t paragraphIndex = UINT16_MAX;
  uint32_t timestamp = 0;
  uint32_t layoutSignature = 0;
  uint32_t startOffset = UINT32_MAX;
  uint32_t endOffset = UINT32_MAX;
  uint64_t syncRevision = 0;
  char id[65] = {};
  bool pendingUpload = true;
  uint32_t textOffset = 0;
  uint16_t textLength = 0;
  char chapterTitle[CLIPPING_CHAPTER_TITLE_MAX] = {};
};

#if defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3
static_assert(sizeof(Clipping) == 168, "C3 clipping index layout used by the Rust ownership migration");
#endif

struct XpClippingPhysical;
extern const XpClippingPhysical CLIPPING_PHYSICAL;

// Stateless copy-out boundary for native callers; retired in P17/P18.
class ClippingStoreFacade {
 public:
  static bool loadForBook(std::string_view path, std::string_view title, std::string_view author,
                          std::string_view kind);
  static void unload();
  static size_t clippingCount();
  static bool clippingAt(size_t index, Clipping& output);
  static bool readClippingText(size_t index, std::string& output);
  static bool prepareSync();
  static bool finishUploads();
  static bool applyRemote(const Clipping& clipping, const std::string& text, bool deleted);
  static bool nextDeletion(uint32_t& offset, char (&id)[65]);
  static bool finishDeletions();
  static bool moveBook(const std::string& from, const std::string& to);
  static bool deleteForFilePath(const std::string& path, const std::string& kind);
};
