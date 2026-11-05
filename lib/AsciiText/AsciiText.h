#pragma once

#include <xp_ffi.h>

#include <string_view>

// ASCII whitespace as defined by WHATWG (space, tab, LF, CR, FF), which is
// also the CSS whitespace set.
inline bool isAsciiWhitespace(const char c) { return xp_book_ascii_space(static_cast<uint8_t>(c)); }

// Returned view aliases s; it is not null-terminated unless s ends at a terminator.
inline std::string_view trimAsciiWhitespace(std::string_view s) {
  size_t start, end;
  xp_book_ascii_trim(reinterpret_cast<const uint8_t*>(s.data()), s.size(), &start, &end);
  return s.substr(start, end - start);
}
