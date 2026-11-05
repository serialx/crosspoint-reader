#pragma once

#include <cstdint>

#include "../css/CssStyle.h"

/**
 * BlockStyle - Block-level styling properties
 */
struct BlockStyle {
  CssTextAlign alignment = CssTextAlign::Justify;

  // Extra pixels between glyphs during layout and drawing.
  int8_t characterSpacing = 0;
  // Spacing (in pixels)
  int16_t marginTop = 0;
  int16_t marginBottom = 0;
  int16_t marginLeft = 0;
  int16_t marginRight = 0;
  int16_t paddingTop = 0;     // treated same as margin for rendering
  int16_t paddingBottom = 0;  // treated same as margin for rendering
  int16_t paddingLeft = 0;    // treated same as margin for rendering
  int16_t paddingRight = 0;   // treated same as margin for rendering
  int16_t textIndent = 0;
  bool textIndentDefined = false;  // true if text-indent was explicitly set in CSS
  bool textAlignDefined = false;   // true if text-align was explicitly set in CSS
  bool isRtl = false;              // true if resolved direction is RTL
  bool directionDefined = false;   // true if direction was explicitly set in CSS/HTML

  // Computed CSS font size relative to the reader's body size. Layout-time only.
  float fontScale = 1.0f;
  bool fontScaleDefined = false;
  // Renderer font for this block's lines; 0 means the section's reader font.
  int32_t fontId = 0;
  // Forced page breaks from CSS. Layout-time only; never inherited by children.
  bool pageBreakBefore = false;
  bool pageBreakAfter = false;
  // Headings: keep the block's last lines on the page with whatever follows. Layout-time only.
  bool keepWithNext = false;
};
