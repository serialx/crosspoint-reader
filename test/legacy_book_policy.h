#pragma once
// Original C++ helpers retained only for host reference tests.
#include <algorithm>

#include "../lib/Epub/Epub/blocks/BlockStyle.h"
#include "../lib/Epub/Epub/css/CssStyle.h"
namespace legacy_book_policy {
inline bool visible(const CssBorderSide& side) { return side.width > 0 && side.style != CssBorderStyle::None; }
inline bool hasVisibleBorder(const CssStyle& style) {
  return visible(style.borderTop) || visible(style.borderRight) || visible(style.borderBottom) ||
         visible(style.borderLeft);
}
inline BlockStyle combineHorizontal(const BlockStyle& parent, const BlockStyle& child) {
  struct Original : BlockStyle {
    enum class CombineAxis : uint8_t { Horizontal = 1, Vertical = 2 };
    [[nodiscard]] BlockStyle getCombinedBlockStyle(const BlockStyle& child, CombineAxis axis) const {
      BlockStyle result = child;

      if (axis == CombineAxis::Horizontal) {
        result.marginLeft = static_cast<int16_t>(child.marginLeft + marginLeft);
        result.marginRight = static_cast<int16_t>(child.marginRight + marginRight);
        result.paddingLeft = static_cast<int16_t>(child.paddingLeft + paddingLeft);
        result.paddingRight = static_cast<int16_t>(child.paddingRight + paddingRight);
        if (!child.textIndentDefined && textIndentDefined) {
          result.textIndent = textIndent;
          result.textIndentDefined = true;
        }
        if (!child.textAlignDefined && textAlignDefined) {
          result.alignment = alignment;
          result.textAlignDefined = true;
        }
      } else {
        result.marginTop = std::max(child.marginTop, marginTop);
        result.marginBottom = std::max(child.marginBottom, marginBottom);
        result.paddingTop = static_cast<int16_t>(child.paddingTop + paddingTop);
        result.paddingBottom = static_cast<int16_t>(child.paddingBottom + paddingBottom);
        // A container's break lands on its first child when they share the empty block.
        result.pageBreakBefore = child.pageBreakBefore || pageBreakBefore;
      }

      // Direction is not axis-specific. Inherit from parent when child doesn't define it.
      if (!child.directionDefined && directionDefined) {
        result.isRtl = isRtl;
        result.directionDefined = true;
      }
      if (!child.fontScaleDefined && fontScaleDefined) {
        result.fontScale = fontScale;
        result.fontScaleDefined = true;
      }

      return result;
    }
  };
  Original original;
  static_cast<BlockStyle&>(original) = parent;
  return original.getCombinedBlockStyle(child, Original::CombineAxis::Horizontal);
}
inline void applyOver(CssStyle& value, const CssStyle& base) {
  struct Original : CssStyle {
    void applyOver(const CssStyle& base) {
      if (base.hasTextAlign()) {
        textAlign = base.textAlign;
        defined.textAlign = 1;
      }
      if (base.hasFontStyle()) {
        fontStyle = base.fontStyle;
        defined.fontStyle = 1;
      }
      if (base.hasFontWeight()) {
        fontWeight = base.fontWeight;
        defined.fontWeight = 1;
      }
      if (base.hasTextDecoration()) {
        textDecoration = base.textDecoration;
        defined.textDecoration = 1;
      }
      if (base.hasTextIndent()) {
        textIndent = base.textIndent;
        defined.textIndent = 1;
      }
      if (base.hasMarginTop()) {
        marginTop = base.marginTop;
        defined.marginTop = 1;
      }
      if (base.hasMarginBottom()) {
        marginBottom = base.marginBottom;
        defined.marginBottom = 1;
      }
      if (base.hasMarginLeft()) {
        marginLeft = base.marginLeft;
        defined.marginLeft = 1;
      }
      if (base.hasMarginRight()) {
        marginRight = base.marginRight;
        defined.marginRight = 1;
      }
      if (base.hasPaddingTop()) {
        paddingTop = base.paddingTop;
        defined.paddingTop = 1;
      }
      if (base.hasPaddingBottom()) {
        paddingBottom = base.paddingBottom;
        defined.paddingBottom = 1;
      }
      if (base.hasPaddingLeft()) {
        paddingLeft = base.paddingLeft;
        defined.paddingLeft = 1;
      }
      if (base.hasPaddingRight()) {
        paddingRight = base.paddingRight;
        defined.paddingRight = 1;
      }
      if (base.hasImageHeight()) {
        imageHeight = base.imageHeight;
        defined.imageHeight = 1;
      }
      if (base.hasImageWidth()) {
        imageWidth = base.imageWidth;
        defined.imageWidth = 1;
      }
      if (base.hasDisplay()) {
        display = base.display;
        defined.display = 1;
      }
      if (base.hasDirection()) {
        direction = base.direction;
        defined.direction = 1;
      }
      if (base.hasVerticalAlign()) {
        verticalAlign = base.verticalAlign;
        defined.verticalAlign = 1;
      }
      if (base.hasListStyleType()) {
        listStyleType = base.listStyleType;
        defined.listStyleType = 1;
      }
      if (base.hasFontSize()) {
        fontSize = base.fontSize;
        defined.fontSize = 1;
      }
      if (base.hasSmallCaps()) {
        smallCaps = base.smallCaps;
        defined.smallCaps = 1;
      }
      if (base.hasPageBreakBefore()) {
        pageBreakBefore = base.pageBreakBefore;
        defined.pageBreakBefore = 1;
      }
      if (base.hasPageBreakAfter()) {
        pageBreakAfter = base.pageBreakAfter;
        defined.pageBreakAfter = 1;
      }
      const auto applyBorder = [&](CssBorderSide& side, const CssBorderSide& incoming, const uint8_t bit) {
        if (base.borderWidthsDefined & bit) side.width = incoming.width;
        if (base.borderStylesDefined & bit) side.style = incoming.style;
      };
      borderWidthsDefined |= base.borderWidthsDefined;
      borderStylesDefined |= base.borderStylesDefined;
      if (base.defined.borderTop) {
        applyBorder(borderTop, base.borderTop, 1);
        defined.borderTop = 1;
      }
      if (base.defined.borderRight) {
        applyBorder(borderRight, base.borderRight, 2);
        defined.borderRight = 1;
      }
      if (base.defined.borderBottom) {
        applyBorder(borderBottom, base.borderBottom, 4);
        defined.borderBottom = 1;
      }
      if (base.defined.borderLeft) {
        applyBorder(borderLeft, base.borderLeft, 8);
        defined.borderLeft = 1;
      }
      if (base.defined.shaded) {
        shaded = base.shaded;
        defined.shaded = 1;
      }
      if (base.defined.floatLeft) {
        floatLeft = base.floatLeft;
        defined.floatLeft = 1;
      }
      if (base.defined.whiteSpace) {
        preserveWhitespace = base.preserveWhitespace;
        defined.whiteSpace = 1;
      }
      if (base.defined.initialLetter) {
        initialLetter = base.initialLetter;
        defined.initialLetter = 1;
      }
    }
  };
  Original original;
  static_cast<CssStyle&>(original) = value;
  original.applyOver(base);
  value = original;
}
constexpr bool allowsBreak(bool continues, bool noSpaceBefore) { return !continues || noSpaceBefore; }
constexpr bool isJustifiableGap(bool continues, bool stretchable) { return !continues || stretchable; }
}  // namespace legacy_book_policy
