#pragma once

#include <HalStorage.h>
#include <xp_xtc_owner.h>

#include <functional>
#include <span>
#include <string_view>

#include "XtcTypes.h"

namespace xtc {

// Stable native envelope for Rust format resources and the physical HAL file.
class RustXtcParser {
  HalFile file;
  XpXtcOwnerStorage owner;
  static const XpXtcLeaves leaves;
  void diagnostic(uint32_t event, uint32_t a, uint32_t b, uint32_t c);
  XpXtcView view(bool chapters = false) const;

 public:
  RustXtcParser();
  ~RustXtcParser();
  RustXtcParser(const RustXtcParser&) = delete;
  RustXtcParser& operator=(const RustXtcParser&) = delete;
  RustXtcParser(RustXtcParser&&) = delete;
  RustXtcParser& operator=(RustXtcParser&&) = delete;

  XtcError open(const char* path);
  void close();
  bool isOpen() const { return view().state->opened; }
  // Borrowed header/text/rows expire on close/open/drop or lazy chapter mutation.
  const XtcHeader& getHeader() const { return view().state->header; }
  uint16_t getPageCount() const { return view().state->header.pageCount; }
  uint16_t getWidth() const { return view().state->width; }
  uint16_t getHeight() const { return view().state->height; }
  uint8_t getBitDepth() const { return view().state->depth; }
  XtcError getLastError() const { return static_cast<XtcError>(view().state->error); }
  std::string_view getTitle() const;
  std::string_view getAuthor() const;
  bool hasChapters() const { return view().state->has_chapters; }
  // Chapter names are NUL-terminated; lengths exclude the terminator.
  std::span<const XpXtcChapterRow> getChapters();
  bool getPageInfo(uint32_t index, PageInfo& info);
  size_t loadPage(uint32_t index, uint8_t* bytes, size_t capacity);
  XtcError loadPageStreaming(uint32_t index, std::function<void(const uint8_t*, size_t, size_t)> callback,
                             size_t chunkSize = 1024);
  static bool isValidXtcFile(const char* path);
};

#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(RustXtcParser) == 172);
#endif
}  // namespace xtc
