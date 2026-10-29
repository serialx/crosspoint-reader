#pragma once

#include <xp_book.h>

#include <cstddef>
#include <string_view>

namespace VisibleTextUtils {

inline bool equalsTag(const std::string_view name, const std::string_view tag) {
  return xp_book_tag_equals(reinterpret_cast<const uint8_t*>(name.data()), name.size(),
                            reinterpret_cast<const uint8_t*>(tag.data()), tag.size());
}

inline bool isNonVisibleElement(const std::string_view name) {
  return xp_book_tag_hidden(reinterpret_cast<const uint8_t*>(name.data()), name.size());
}

}  // namespace VisibleTextUtils
