#pragma once
#include <fontIds.h>

#include <cstdint>
struct CrossPointSettings {
  static constexpr int BOOK_STYLE = 4;
  int fontPointSize = 14, screenMargin = 12, font = NOTOSANS_14_FONT_ID;
  uint8_t paragraphAlignment = 0, paragraphIndentSpaces = 2, wordSpacing = 100;
  bool extraParagraphSpacing = false, focusReadingEnabled = false, hyphenationEnabled = false;
  float compression = 0.95f;
  int8_t tracking = 0;
  int getReaderFontId() const { return font; }
  float getReaderLineCompression() const { return compression; }
  int8_t getCharacterSpacing() const { return tracking; }
};
inline CrossPointSettings SETTINGS;
