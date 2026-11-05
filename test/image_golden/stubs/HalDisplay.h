#pragma once
#include <Print.h>
struct PngTestDisplay {
  int getDisplayWidth() const { return 792; }
  int getDisplayHeight() const { return 528; }
};
inline PngTestDisplay display;
