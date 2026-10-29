#include "TxtToHtml.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>
#include <xp_ffi.h>

#include <algorithm>
#include <cstring>

const char* TxtToHtml::cacheVersionTag(std::string_view filename) {
  return FsHelpers::hasMarkdownExtension(filename) ? "<!-- MD_CACHE_VERSION: 1 -->" : "<!-- TXT_CACHE_VERSION: 1 -->";
}

size_t TxtToHtml::escapedHtmlSize(const std::string_view text) {
  return xp_book_txt_escaped_size(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

void TxtToHtml::appendEscapedHtml(std::string& out, const std::string_view text) {
  TxtToHtml::streamEscapedHtml([&out](std::string_view fragment) { out.append(fragment); }, text);
}

bool TxtToHtml::stream(std::string_view filename, void* readerCtx, int (*readFn)(void* ctx, uint8_t* buf, size_t size),
                       Print& out) {
  constexpr size_t IN_BUF_SIZE = 8192;
  constexpr size_t OUT_BUF_SIZE = 8192;

  auto inBuf = makeUniqueNoThrow<uint8_t[]>(IN_BUF_SIZE);
  auto outBuf = makeUniqueNoThrow<uint8_t[]>(OUT_BUF_SIZE);
  if (!inBuf || !outBuf) {
    LOG_ERR("TXT", "OOM: TXT/MD HTML streaming buffers");
    return false;
  }

  struct StreamContext {
    void* reader;
    int (*read)(void*, uint8_t*, size_t);
    Print& output;
  } context{readerCtx, readFn, out};
  const std::string title = FsHelpers::getFileNameWithoutExtension(filename);
  const char* tag = cacheVersionTag(filename);
  const int32_t status = xp_book_txt_stream(
      reinterpret_cast<const uint8_t*>(title.data()), title.size(), reinterpret_cast<const uint8_t*>(tag), strlen(tag),
      inBuf.get(), outBuf.get(), &context,
      [](void* ctx, uint8_t* bytes, size_t count) -> int32_t {
        auto& stream = *static_cast<StreamContext*>(ctx);
        return stream.read(stream.reader, bytes, count);
      },
      [](void* ctx, const uint8_t* bytes, size_t count) -> size_t {
        return static_cast<StreamContext*>(ctx)->output.write(bytes, count);
      });

  if (status == -1) {
    LOG_ERR("TXT", "Read error while streaming TXT/MD: %.*s", static_cast<int>(filename.size()), filename.data());
    return false;
  }

  if (status != 0) {
    LOG_ERR("TXT", "Failed to stream complete HTML (write error or disk full)");
    return false;
  }
  return true;
}

bool TxtToHtml::stream(std::string_view filename, std::string_view content, Print& out) {
  struct ViewReader {
    std::string_view s;
    size_t pos = 0;
  } reader{content, 0};

  return stream(
      filename, &reader,
      [](void* ctx, uint8_t* buf, size_t size) -> int {
        auto* r = static_cast<ViewReader*>(ctx);
        if (r->pos >= r->s.size()) return 0;
        const size_t n = std::min(size, r->s.size() - r->pos);
        memcpy(buf, r->s.data() + r->pos, n);
        r->pos += n;
        return static_cast<int>(n);
      },
      out);
}
