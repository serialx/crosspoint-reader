#pragma once
#include <string>
struct Epub {
  int count = 3;
  std::string getCachePath() const { return "/cache"; }
  int getSpineItemsCount() const { return count; }
};
