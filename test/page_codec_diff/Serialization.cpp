// Legacy C++ serialization is retained only as an independent host oracle.
#include <Epub/Page.h>
#include <Epub/SectionCodec.h>
#include <Logging.h>

namespace {
PageDropCap dropCapView(const XpPageElement& e) {
  char text[13]{};
  memcpy(text, e.first.data, std::min(e.first.len, sizeof(text) - 1));
  return PageDropCap(e.record.values[2], e.record.values[3], static_cast<EpdFontFamily::Style>(e.record.values[4]),
                     text, e.record.values[0], e.record.values[1]);
}
}  // namespace

bool ImageBlock::serialize(HalFile& file) {
  return sectioncodec::writeValue(file, XP_SEC_U32, imagePath.size()) &&
         file.write(imagePath.data(), imagePath.size()) == imagePath.size() &&
         sectioncodec::writeValue(file, XP_SEC_U32, srcPath.size()) &&
         file.write(srcPath.data(), srcPath.size()) == srcPath.size() &&
         sectioncodec::write(file, sectioncodec::record(XP_SEC_IMAGE_SIZE, {uint32_t(width), uint32_t(height)}));
}

bool PageLine::serialize(HalFile& file) {
  return sectioncodec::write(file, sectioncodec::record(XP_SEC_POSITION, {uint32_t(xPos), uint32_t(yPos)})) &&
         block->serialize(file);
}

bool PageImage::serialize(HalFile& file) {
  return sectioncodec::write(file, sectioncodec::record(XP_SEC_POSITION, {uint32_t(xPos), uint32_t(yPos)})) &&
         imageBlock->serialize(file);
}

bool PageHorizontalRule::serialize(HalFile& file) {
  return sectioncodec::write(file,
                             sectioncodec::record(XP_SEC_RULE, {uint32_t(xPos), uint32_t(yPos), width, thickness}));
}

bool PageDropCap::serialize(HalFile& file) {
  const auto length = static_cast<uint8_t>(strnlen(text, MAX_TEXT_BYTES));
  return sectioncodec::write(
             file, sectioncodec::record(XP_SEC_DROP_CAP, {uint32_t(xPos), uint32_t(yPos), uint32_t(fontId), scale256,
                                                          uint32_t(style), length})) &&
         file.write(text, length) == length;
}

bool PageBorderBox::serialize(HalFile& file) {
  return sectioncodec::write(
      file, sectioncodec::record(
                XP_SEC_BORDER, {uint32_t(xPos), uint32_t(yPos), width, height, sides[0].width, uint32_t(sides[0].style),
                                sides[1].width, uint32_t(sides[1].style), sides[2].width, uint32_t(sides[2].style),
                                sides[3].width, uint32_t(sides[3].style), shaded}));
}

bool Page::serialize(HalFile& file) const {
  const uint16_t count = elementCount();
  if (!sectioncodec::writeValue(file, XP_SEC_U16, count)) return false;

  for (uint16_t i = 0; i < count; ++i) {
    const auto el = element(i);
    // Use getTag() method to determine type
    if (!sectioncodec::writeValue(file, XP_SEC_ELEMENT_TAG, el.getTag())) return false;

    if (!el.serialize(file)) {
      return false;
    }
  }

  // Serialize footnotes (clamp to MAX_FOOTNOTES_PER_PAGE to match addFootnote/deserialize limits)
  const uint16_t fnCount = std::min<uint16_t>(footnoteCount(), MAX_FOOTNOTES_PER_PAGE);
  if (!sectioncodec::writeValue(file, XP_SEC_FOOTNOTE_COUNT, fnCount)) return false;
  for (uint16_t i = 0; i < fnCount; i++) {
    FootnoteEntry fn;
    footnote(i, fn);
    if (file.write(fn.number, sizeof(fn.number)) != sizeof(fn.number) ||
        file.write(fn.href, sizeof(fn.href)) != sizeof(fn.href)) {
      LOG_ERR("PGE", "Failed to write footnote");
      return false;
    }
  }

  const uint16_t linkCount = std::min<uint16_t>(this->linkCount(), MAX_LINKS_PER_PAGE);
  if (!sectioncodec::writeValue(file, XP_SEC_LINK_COUNT, linkCount)) return false;
  for (uint16_t i = 0; i < linkCount; i++) {
    PageLink link;
    this->link(i, link);
    if (file.write(link.href, sizeof(link.href)) != sizeof(link.href)) {
      LOG_ERR("PGE", "Failed to write link %u", i);
      return false;
    }
    if (!sectioncodec::write(
            file, sectioncodec::record(XP_SEC_LINK_GEOMETRY, {uint32_t(link.x), uint32_t(link.y), uint32_t(link.width),
                                                              uint32_t(link.height)})))
      return false;
  }

  return true;
}

bool PageElementView::serialize(HalFile& file) const {
  if (!page->arena) return page->elements[index]->serialize(file);
  switch (tag) {
    case TAG_PageLine:
      return sectioncodec::write(file, sectioncodec::record(XP_SEC_POSITION, {uint32_t(xPos), uint32_t(yPos)})) &&
             textBlock().serialize(file);
    case TAG_PageImage:
      return sectioncodec::write(file, sectioncodec::record(XP_SEC_POSITION, {uint32_t(xPos), uint32_t(yPos)})) &&
             imageBlock().serialize(file);
    case TAG_PageHorizontalRule:
      return sectioncodec::write(file, fields().record);
    case TAG_PageDropCap:
      return dropCapView(fields()).serialize(file);
    case TAG_PageBorderBox:
      return sectioncodec::write(file, fields().record);
  }
  return false;
}
