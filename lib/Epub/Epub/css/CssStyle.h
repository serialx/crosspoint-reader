#pragma once

#include <cstdint>

// Matches order of PARAGRAPH_ALIGNMENT in CrossPointSettings
enum class CssTextAlign : uint8_t { Justify = 0, Left = 1, Center = 2, Right = 3, None = 4 };
enum class CssUnit : uint8_t { Pixels = 0, Em = 1, Rem = 2, Points = 3, Percent = 4 };
enum class CssTextDirection : uint8_t { Ltr = 0, Rtl = 1 };

// Represents a CSS length value with its unit, allowing deferred resolution to pixels
struct CssLength {
  float value = 0.0f;
  CssUnit unit = CssUnit::Pixels;

  CssLength() = default;
  CssLength(const float v, const CssUnit u) : value(v), unit(u) {}

  // Convenience constructor for pixel values (most common case)
  explicit CssLength(const float pixels) : value(pixels) {}
};

// Font style options matching CSS font-style property
enum class CssFontStyle : uint8_t { Normal = 0, Italic = 1, Inherit = 2 };

// Font weight options - CSS supports 100-900, we simplify to normal/bold
enum class CssFontWeight : uint8_t { Normal = 0, Bold = 1, Inherit = 2 };

// Text decoration options. Values are bit flags so CSS can combine multiple line decorations.
enum class CssTextDecoration : uint8_t { None = 0, Underline = 1, LineThrough = 2 };

constexpr CssTextDecoration operator|(const CssTextDecoration a, const CssTextDecoration b) {
  return static_cast<CssTextDecoration>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr CssTextDecoration operator&(const CssTextDecoration a, const CssTextDecoration b) {
  return static_cast<CssTextDecoration>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr uint8_t CSS_TEXT_DECORATION_MASK =
    static_cast<uint8_t>(CssTextDecoration::Underline) | static_cast<uint8_t>(CssTextDecoration::LineThrough);

// Display options - only None and Block are relevant for e-ink rendering
enum class CssDisplay : uint8_t { Block = 0, None = 1 };

// Vertical alignment options for inline elements (e.g. superscript/subscript)
enum class CssVerticalAlign : uint8_t { Baseline = 0, Super = 1, Sub = 2 };

// list-style-type — only None and Disc (bullet) are relevant for rendering
enum class CssListStyleType : uint8_t { Disc = 0, None = 1 };

enum class CssBorderStyle : uint8_t { None = 0, Solid = 1, Double = 2, Dotted = 3, Dashed = 4 };

// One box edge. Width is in CSS pixels; a side draws only when both width and style are set.
struct CssBorderSide {
  uint8_t width = 0;
  CssBorderStyle style = CssBorderStyle::None;
};

// Bitmask for tracking which properties have been explicitly set
struct CssPropertyFlags {
  uint16_t textAlign : 1;
  uint16_t fontStyle : 1;
  uint16_t fontWeight : 1;
  uint16_t textDecoration : 1;
  uint16_t textIndent : 1;
  uint16_t marginTop : 1;
  uint16_t marginBottom : 1;
  uint16_t marginLeft : 1;
  uint16_t marginRight : 1;
  uint16_t paddingTop : 1;
  uint16_t paddingBottom : 1;
  uint16_t paddingLeft : 1;
  uint16_t paddingRight : 1;
  uint16_t imageHeight : 1;
  uint16_t imageWidth : 1;
  uint16_t display : 1;
  uint16_t direction : 1;
  uint16_t verticalAlign : 1;
  uint16_t listStyleType : 1;
  uint16_t fontSize : 1;
  uint16_t smallCaps : 1;
  uint16_t pageBreakBefore : 1;
  uint16_t pageBreakAfter : 1;
  uint16_t borderTop : 1;
  uint16_t borderRight : 1;
  uint16_t borderBottom : 1;
  uint16_t borderLeft : 1;
  uint16_t shaded : 1;
  uint16_t floatLeft : 1;
  uint16_t initialLetter : 1;
  uint16_t whiteSpace : 1;

  CssPropertyFlags()
      : textAlign(0),
        fontStyle(0),
        fontWeight(0),
        textDecoration(0),
        textIndent(0),
        marginTop(0),
        marginBottom(0),
        marginLeft(0),
        marginRight(0),
        paddingTop(0),
        paddingBottom(0),
        paddingLeft(0),
        paddingRight(0),
        imageHeight(0),
        imageWidth(0),
        display(0),
        direction(0),
        verticalAlign(0),
        listStyleType(0),
        fontSize(0),
        smallCaps(0),
        pageBreakBefore(0),
        pageBreakAfter(0),
        borderTop(0),
        borderRight(0),
        borderBottom(0),
        borderLeft(0),
        shaded(0),
        floatLeft(0),
        initialLetter(0),
        whiteSpace(0) {}

  [[nodiscard]] bool anySet() const {
    return textAlign || fontStyle || fontWeight || textDecoration || textIndent || marginTop || marginBottom ||
           marginLeft || marginRight || paddingTop || paddingBottom || paddingLeft || paddingRight || imageHeight ||
           imageWidth || display || direction || verticalAlign || listStyleType || fontSize || smallCaps ||
           pageBreakBefore || pageBreakAfter || borderTop || borderRight || borderBottom || borderLeft || shaded ||
           floatLeft || initialLetter || whiteSpace;
  }
};

// Cache serializes defined flags as uint32_t with bit indices 0..30.
static_assert(sizeof(CssPropertyFlags) <= sizeof(uint32_t),
              "CssPropertyFlags exceeds 32 bits; update cache read/write in CssParser.cpp");
static_assert(sizeof(CssPropertyFlags) * 8 >= 31,
              "CssPropertyFlags has fewer bits than properties; update bitfield widths");

// Represents a collection of CSS style properties
// Only stores properties relevant to e-ink text rendering
// Length values are stored as CssLength (value + unit) for deferred resolution
struct CssStyle {
  CssTextAlign textAlign = CssTextAlign::Left;
  CssFontStyle fontStyle = CssFontStyle::Normal;
  CssFontWeight fontWeight = CssFontWeight::Normal;
  CssTextDecoration textDecoration = CssTextDecoration::None;
  CssTextDirection direction = CssTextDirection::Ltr;

  CssLength textIndent;     // First-line indent (deferred resolution)
  CssLength marginTop;      // Vertical spacing before block
  CssLength marginBottom;   // Vertical spacing after block
  CssLength marginLeft;     // Horizontal spacing left of block
  CssLength marginRight;    // Horizontal spacing right of block
  CssLength paddingTop;     // Padding before
  CssLength paddingBottom;  // Padding after
  CssLength paddingLeft;    // Padding left
  CssLength paddingRight;   // Padding right
  CssLength imageHeight;    // Height for img (e.g. 2em) – width derived from aspect ratio when only height set
  CssLength imageWidth;     // Width for img when both or only width set
  CssLength fontSize;       // Multiplier: Em is relative to the parent size, Rem to the reader's body size
  CssDisplay display = CssDisplay::Block;                       // display property (Block or None)
  CssVerticalAlign verticalAlign = CssVerticalAlign::Baseline;  // vertical-align (super/sub positioning)
  CssListStyleType listStyleType = CssListStyleType::Disc;      // list-style-type (Disc or None)
  bool smallCaps = false;                                       // font-variant(-caps): small-caps
  bool pageBreakBefore = false;                                 // (page-)break-before forces a new page
  bool pageBreakAfter = false;                                  // (page-)break-after forces a new page
  CssBorderSide borderTop, borderRight, borderBottom, borderLeft;
  uint8_t borderWidthsDefined = 0;  // one bit per side, top/right/bottom/left
  uint8_t borderStylesDefined = 0;
  bool shaded = false;        // light background-color, drawn as a gray fill
  bool floatLeft = false;     // float: left (marks drop-cap spans and ::first-letter rules)
  uint8_t initialLetter = 0;  // initial-letter: lines a drop cap spans, 0 = normal

  bool preserveWhitespace = false;  // white-space: pre, pre-wrap or break-spaces

  CssPropertyFlags defined;  // Tracks which properties were explicitly set

  [[nodiscard]] bool hasTextAlign() const { return defined.textAlign; }
  [[nodiscard]] bool hasFontStyle() const { return defined.fontStyle; }
  [[nodiscard]] bool hasFontWeight() const { return defined.fontWeight; }
  [[nodiscard]] bool hasTextDecoration() const { return defined.textDecoration; }
  [[nodiscard]] bool hasTextIndent() const { return defined.textIndent; }
  [[nodiscard]] bool hasMarginTop() const { return defined.marginTop; }
  [[nodiscard]] bool hasMarginBottom() const { return defined.marginBottom; }
  [[nodiscard]] bool hasMarginLeft() const { return defined.marginLeft; }
  [[nodiscard]] bool hasMarginRight() const { return defined.marginRight; }
  [[nodiscard]] bool hasPaddingTop() const { return defined.paddingTop; }
  [[nodiscard]] bool hasPaddingBottom() const { return defined.paddingBottom; }
  [[nodiscard]] bool hasPaddingLeft() const { return defined.paddingLeft; }
  [[nodiscard]] bool hasPaddingRight() const { return defined.paddingRight; }
  [[nodiscard]] bool hasImageHeight() const { return defined.imageHeight; }
  [[nodiscard]] bool hasImageWidth() const { return defined.imageWidth; }
  [[nodiscard]] bool hasDisplay() const { return defined.display; }
  [[nodiscard]] bool hasDirection() const { return defined.direction; }
  [[nodiscard]] bool hasVerticalAlign() const { return defined.verticalAlign; }
  [[nodiscard]] bool hasListStyleType() const { return defined.listStyleType; }
  [[nodiscard]] bool hasFontSize() const { return defined.fontSize; }
  [[nodiscard]] bool hasSmallCaps() const { return defined.smallCaps; }
  [[nodiscard]] bool hasPageBreakBefore() const { return defined.pageBreakBefore; }
  [[nodiscard]] bool hasPageBreakAfter() const { return defined.pageBreakAfter; }

  void reset() { *this = CssStyle{}; }
};
