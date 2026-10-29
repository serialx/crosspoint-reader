#pragma once

#include <xp_ffi.h>

#include <cstring>

inline const char* xmlLocalName(const char* qName) {
  return reinterpret_cast<const char*>(xp_book_xml_local(reinterpret_cast<const uint8_t*>(qName)));
}

inline bool xmlLocalNameEquals(const char* qName, const char* expected) {
  return std::strcmp(xmlLocalName(qName), expected) == 0;
}
