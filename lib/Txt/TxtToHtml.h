#pragma once

#include <Print.h>
#include <xp_ffi.h>

#include <cstddef>
#include <string>
#include <string_view>

class TxtToHtml {
 public:
  template <typename Append>
  static void streamEscapedHtml(Append&& append, std::string_view text) {
    struct Context {
      Append& append;
    } context{append};
    xp_book_txt_escape(
        reinterpret_cast<const uint8_t*>(text.data()), text.size(), &context,
        [](void* ctx, const uint8_t* bytes, size_t size) {
          static_cast<Context*>(ctx)->append(std::string_view(reinterpret_cast<const char*>(bytes), size));
        });
  }

  static const char* cacheVersionTag(std::string_view filename);
  static size_t escapedHtmlSize(std::string_view text);
  static void appendEscapedHtml(std::string& out, std::string_view text);
  static bool stream(std::string_view filename, void* readerCtx, int (*readFn)(void* ctx, uint8_t* buf, size_t size),
                     Print& out);
  static bool stream(std::string_view filename, std::string_view content, Print& out);
};
