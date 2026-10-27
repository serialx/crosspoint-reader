#include <Epub/SectionCodec.h>
#include <Epub/blocks/TextBlock.h>
#include <Logging.h>

bool TextBlock::serialize(HalFile& file) const {
  const auto& blockStyle = getBlockStyle();
  if (!isValid) {
    LOG_ERR("TXB", "Serialization failed: invalid block");
    return false;
  }

  // Word data: scalars, then the arena verbatim -- its in-memory layout is
  // exactly the on-disk layout (see TextBlock.h), so one write covers all
  // per-word arrays and the text blob.
  if (!sectioncodec::write(
          file, sectioncodec::record(XP_SEC_TEXT_HEADER, {numWords, focusPresent, textBytes, paragraphStartWord})))
    return false;
  if (numWords > 0) {
    const size_t size = xp_sec_arena_size(numWords, focusPresent, textBytes);
    if (file.write(sourceRanges, size) != size) {
      LOG_ERR("TXB", "Serialization failed: arena write (%u bytes)", static_cast<uint32_t>(size));
      return false;
    }
  }

  // Ruby text data
  for (size_t i = 0; i < numWords; i++) {
    const auto ruby = rubyText(i);
    if (!sectioncodec::writeValue(file, XP_SEC_U32, ruby.size()) || file.write(ruby.data(), ruby.size()) != ruby.size())
      return false;
  }

  // Style (alignment + margins/padding/indent)
  if (!sectioncodec::write(
          file, sectioncodec::record(XP_SEC_BLOCK_STYLE,
                                     {uint32_t(blockStyle.alignment), uint32_t(blockStyle.textAlignDefined),
                                      uint32_t(blockStyle.marginTop), uint32_t(blockStyle.marginBottom),
                                      uint32_t(blockStyle.marginLeft), uint32_t(blockStyle.marginRight),
                                      uint32_t(blockStyle.paddingTop), uint32_t(blockStyle.paddingBottom),
                                      uint32_t(blockStyle.paddingLeft), uint32_t(blockStyle.paddingRight),
                                      uint32_t(blockStyle.textIndent), uint32_t(blockStyle.textIndentDefined),
                                      uint32_t(blockStyle.isRtl), uint32_t(blockStyle.directionDefined),
                                      uint32_t(blockStyle.characterSpacing), uint32_t(blockStyle.fontId)})))
    return false;

  // Per-word fonts: a flag, then the font table and one slot per word.
  const uint8_t hasWordFonts = fontData ? 1 : 0;
  if (!sectioncodec::writeValue(file, XP_SEC_WORD_FONT_FLAG, hasWordFonts)) return false;
  if (hasWordFonts) {
    const size_t size = sizeof(int32_t) * MAX_WORD_FONTS + numWords;
    if (file.write(fontData, size) != size) {
      LOG_ERR("TXB", "Serialization failed: word fonts");
      return false;
    }
  }

  return true;
}
