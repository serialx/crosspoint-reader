#include "htmlEntities.h"

#include <xp_book.h>

const char* lookupHtmlEntity(const char* entity, const size_t len) {
  return xp_book_entity(reinterpret_cast<const uint8_t*>(entity), len);
}
