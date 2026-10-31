#pragma once
#include <array>
#include <cstring>

#include "CssStyle.h"
namespace css_oracle {
constexpr std::array STYLE_LENGTH_FIELDS = {
    &CssStyle::textIndent,   &CssStyle::marginTop,   &CssStyle::marginBottom,  &CssStyle::marginLeft,
    &CssStyle::marginRight,  &CssStyle::paddingTop,  &CssStyle::paddingBottom, &CssStyle::paddingLeft,
    &CssStyle::paddingRight, &CssStyle::imageHeight, &CssStyle::imageWidth,    &CssStyle::fontSize,
};
constexpr size_t STYLE_LENGTH_FIELD_COUNT = STYLE_LENGTH_FIELDS.size();
constexpr size_t STYLE_WIRE_BYTES =
    5 + STYLE_LENGTH_FIELD_COUNT * (sizeof(decltype(CssLength::value)) + 1) + 6 + 13 + sizeof(uint32_t);
constexpr uint32_t CSS_DEFINED_BITS_MASK = (1u << 31) - 1;
constexpr uint8_t MAX_INITIAL_LETTER = 6;

void encodeStyleWire(const CssStyle& style, uint8_t (&out)[STYLE_WIRE_BYTES]) {
  size_t offset = 0;
  out[offset++] = static_cast<uint8_t>(style.textAlign);
  out[offset++] = static_cast<uint8_t>(style.fontStyle);
  out[offset++] = static_cast<uint8_t>(style.fontWeight);
  out[offset++] = static_cast<uint8_t>(style.textDecoration);
  out[offset++] = static_cast<uint8_t>(style.direction);

  const auto putLength = [&out, &offset](const CssLength& length) {
    memcpy(out + offset, &length.value, sizeof(length.value));
    offset += sizeof(length.value);
    out[offset++] = static_cast<uint8_t>(length.unit);
  };
  for (const auto field : STYLE_LENGTH_FIELDS) {
    putLength(style.*field);
  }
  out[offset++] = static_cast<uint8_t>(style.display);
  out[offset++] = static_cast<uint8_t>(style.verticalAlign);
  out[offset++] = static_cast<uint8_t>(style.listStyleType);
  out[offset++] = style.smallCaps ? 1 : 0;
  out[offset++] = style.pageBreakBefore ? 1 : 0;
  out[offset++] = style.pageBreakAfter ? 1 : 0;
  for (const CssBorderSide* side : {&style.borderTop, &style.borderRight, &style.borderBottom, &style.borderLeft}) {
    out[offset++] = side->width;
    out[offset++] = static_cast<uint8_t>(side->style);
  }
  out[offset++] = style.borderWidthsDefined | (style.borderStylesDefined << 4);
  out[offset++] = style.shaded ? 1 : 0;
  out[offset++] = style.floatLeft ? 1 : 0;
  out[offset++] = style.initialLetter;
  out[offset++] = style.preserveWhitespace ? 1 : 0;

  uint32_t definedBits = 0;
  if (style.defined.textAlign) definedBits |= 1 << 0;
  if (style.defined.fontStyle) definedBits |= 1 << 1;
  if (style.defined.fontWeight) definedBits |= 1 << 2;
  if (style.defined.textDecoration) definedBits |= 1 << 3;
  if (style.defined.textIndent) definedBits |= 1 << 4;
  if (style.defined.marginTop) definedBits |= 1 << 5;
  if (style.defined.marginBottom) definedBits |= 1 << 6;
  if (style.defined.marginLeft) definedBits |= 1 << 7;
  if (style.defined.marginRight) definedBits |= 1 << 8;
  if (style.defined.paddingTop) definedBits |= 1 << 9;
  if (style.defined.paddingBottom) definedBits |= 1 << 10;
  if (style.defined.paddingLeft) definedBits |= 1 << 11;
  if (style.defined.paddingRight) definedBits |= 1 << 12;
  if (style.defined.imageHeight) definedBits |= 1 << 13;
  if (style.defined.imageWidth) definedBits |= 1 << 14;
  if (style.defined.display) definedBits |= 1 << 15;
  if (style.defined.direction) definedBits |= 1 << 16;
  if (style.defined.verticalAlign) definedBits |= 1 << 17;
  if (style.defined.listStyleType) definedBits |= 1 << 18;
  if (style.defined.fontSize) definedBits |= 1 << 19;
  if (style.defined.smallCaps) definedBits |= 1 << 20;
  if (style.defined.pageBreakBefore) definedBits |= 1 << 21;
  if (style.defined.pageBreakAfter) definedBits |= 1 << 22;
  if (style.defined.borderTop) definedBits |= 1 << 23;
  if (style.defined.borderRight) definedBits |= 1 << 24;
  if (style.defined.borderBottom) definedBits |= 1 << 25;
  if (style.defined.borderLeft) definedBits |= 1 << 26;
  if (style.defined.shaded) definedBits |= 1 << 27;
  if (style.defined.floatLeft) definedBits |= 1 << 28;
  if (style.defined.initialLetter) definedBits |= 1 << 29;
  if (style.defined.whiteSpace) definedBits |= 1u << 30;
  memcpy(out + offset, &definedBits, sizeof(definedBits));
}

}  // namespace css_oracle
