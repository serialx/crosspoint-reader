#pragma once
#include <xp_ffi.h>

// Direct table-scanner access is linked only with the chapter-test Cargo feature.
struct XpChapterMeasure {
  uint16_t min[4], preferred[4];
  uint8_t columns;
};
extern "C" uint8_t xp_chapter_measure_table(XpChapterState*, void*, XpChapterRead, XpChapterMeasure*);
extern "C" void xp_chapter_plan_columns(XpChapterState*, const XpChapterMeasure*);
