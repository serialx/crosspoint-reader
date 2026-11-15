#pragma once
#include <cstddef>
#include <cstdint>
extern "C" {
struct XpFtSource {
  uint8_t mode;
  const uint8_t* data;
  uint32_t length;
  void* file;
  uint32_t size;
};
struct XpFtConfig {
  uint16_t pixels;
  int32_t weight;
  bool italic, autoHint, stemDarkening;
  size_t gposBudget, gsubBudget;
};
struct XpFtRaster {
  const uint8_t* pixels;
  uint16_t width, height;
  int16_t left, yoff;
  int32_t advance, metrics;
  bool haveMetrics;
};
struct alignas(void*) XpFtSlot {
  uintptr_t words[40];
};
bool xp_native_ft_open(XpFtSlot*, const XpFtSource*, const XpFtConfig*);
void xp_native_ft_close(XpFtSlot*);
uint32_t xp_native_ft_glyph_id(XpFtSlot*, uint32_t);
void xp_native_ft_raster(XpFtSlot*, uint32_t, uint32_t, XpFtRaster*);
int32_t xp_native_ft_kern(XpFtSlot*, uint32_t, uint32_t, uint32_t);
void xp_native_ft_line_metrics(XpFtSlot*, uint32_t, uint16_t, int32_t*, int32_t*);
uint32_t xp_native_ft_ligature(XpFtSlot*, const uint32_t*, size_t);
void xp_native_ft_release_gsub(XpFtSlot*);
}
