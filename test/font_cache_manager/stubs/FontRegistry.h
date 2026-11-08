#pragma once
#include <EpdFontFamily.h>

#include <map>
// This fixture substitutes font tables and physical caches; no production renderer.
class FontRegistry : public std::map<int, EpdFontFamily> {
 public:
  using std::map<int, EpdFontFamily>::map;
  const EpdFontData* data(int id, EpdFontFamily::Style style) const {
    const auto found = find(id);
    return found == end() ? nullptr : found->second.getData(style);
  }
};
