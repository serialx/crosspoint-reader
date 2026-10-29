#pragma once
#include <xp_book.h>

#include <cstdint>

// The resolved text-rendering configuration a reader hands to the layout
// engine. Section-cache validation keys on every field: a section file built
// with a different spec is discarded and rebuilt.
//
// Build one via CrossPointSettings::readerRenderSpec(width, height), which
// fills every field: the settings-derived ones from the store, the viewport
// from the caller. Taking the viewport as arguments is what keeps a spec from
// existing in a half-filled state — the 0 defaults below are a last-resort
// backstop (a 0x0 viewport lays out nothing), not an invitation to omit it.
struct ReaderRenderSpec {
  int fontId = 0;
  float lineCompression = 1.0f;
  bool extraParagraphSpacing = false;
  uint8_t paragraphIndentSpaces = 2;
  int8_t characterSpacing = 0;
  uint8_t wordSpacingPercent = 100;
  uint8_t paragraphAlignment = 0;
  uint16_t viewportWidth = 0;
  uint16_t viewportHeight = 0;
  bool hyphenationEnabled = false;
  bool embeddedStyle = true;
  uint8_t imageRendering = 0;
  bool focusReadingEnabled = false;
};

// Stable signature for clipping anchors. Page-local word ordinals are valid
// only for the exact layout that produced them.
inline uint32_t readerRenderSpecSignature(const ReaderRenderSpec& spec) {
  const uint32_t fields[12] = {static_cast<uint32_t>(spec.fontId),
                               0,
                               spec.extraParagraphSpacing,
                               static_cast<uint8_t>(spec.characterSpacing),
                               spec.wordSpacingPercent,
                               spec.paragraphAlignment,
                               spec.viewportWidth,
                               spec.viewportHeight,
                               spec.hyphenationEnabled,
                               spec.embeddedStyle,
                               spec.imageRendering,
                               spec.focusReadingEnabled};
  return xp_book_layout_signature(fields, spec.lineCompression);
}
