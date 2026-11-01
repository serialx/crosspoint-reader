#pragma once

#include <xp_book.h>

#include <cstddef>
#include <string_view>

namespace VisibleTextUtils {

inline bool isNonVisibleElement(const std::string_view name) {
  return xp_book_tag_hidden(reinterpret_cast<const uint8_t*>(name.data()), name.size());
}

}  // namespace VisibleTextUtils
