#pragma once
#include <HalStorage.h>
#include <HalDisplay.h>
#include <cstdint>
#include <vector>
class GfxRenderer {
 public:
  mutable std::vector<int> rectangles;
  const uint8_t* getFrameBuffer() const { return nullptr; }
  int getDisplayWidth() const { return 792; }
  int getDisplayHeight() const { return 528; }
  int getScreenWidth() const { return 528; }
  int getScreenHeight() const { return 792; }
  bool storeBwBuffer() { return false; }
  void restoreBwBuffer() {}
  void getOrientedViewableTRBL(int* a,int* b,int* c,int* d) { *a=*b=*c=*d=0; }
  void drawRect(int,int,int,int,int,bool) {}
  void fillRect(int x,int y,int w,int h,bool b) const { rectangles.insert(rectangles.end(),{x,y,w,h,int(b)}); }
  void displayBuffer(HalDisplay::RefreshMode = HalDisplay::RefreshMode::HALF_REFRESH) {}
};
