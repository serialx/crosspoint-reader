#pragma once
#include <string>
#include <string_view>
#include <xp_ffi.h>
struct TxtToHtml {
  static size_t escapedHtmlSize(std::string_view s) { return xp_book_txt_escaped_size(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
  static void appendEscapedHtml(std::string& out, std::string_view s) { xp_book_txt_escape(reinterpret_cast<const uint8_t*>(s.data()), s.size(), &out, [](void* ctx, const uint8_t* p, size_t n) { static_cast<std::string*>(ctx)->append(reinterpret_cast<const char*>(p), n); }); }
};
