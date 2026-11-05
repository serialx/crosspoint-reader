// Exact parent 7f5e1cb5184d03957f1787ed06d872fab773b6db renderSpec and headerFields functions.
ReaderRenderSpec renderSpec(const XpEpubRenderSpec& value) {
  return {.fontId = value.font,
          .lineCompression = value.compression,
          .extraParagraphSpacing = value.paragraph_spacing != 0,
          .paragraphIndentSpaces = value.paragraph_indent,
          .characterSpacing = value.character_spacing,
          .wordSpacingPercent = value.word_spacing_percent,
          .paragraphAlignment = value.alignment,
          .viewportWidth = value.width,
          .viewportHeight = value.height,
          .hyphenationEnabled = value.hyphenation != 0,
          .embeddedStyle = value.embedded_style != 0,
          .imageRendering = value.images,
          .focusReadingEnabled = value.focus_reading != 0};
}

std::array<uint32_t, 20> headerFields(const ReaderRenderSpec& spec, uint16_t pages) {
  uint32_t compression;
  memcpy(&compression, &spec.lineCompression, sizeof(compression));
  return {0,
          static_cast<uint32_t>(spec.fontId),
          compression,
          spec.extraParagraphSpacing,
          spec.paragraphIndentSpaces,
          spec.paragraphAlignment,
          spec.viewportWidth,
          spec.viewportHeight,
          spec.hyphenationEnabled,
          spec.embeddedStyle,
          spec.imageRendering,
          spec.focusReadingEnabled,
          static_cast<uint8_t>(spec.characterSpacing),
          spec.wordSpacingPercent,
          pages,
          0,
          0,
          0,
          0,
          0};
}
