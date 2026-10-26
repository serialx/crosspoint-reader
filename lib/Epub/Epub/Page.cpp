#include "Page.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include "SectionCodec.h"

namespace {

template <typename Predicate>
void renderFilteredPageElements(const std::vector<std::unique_ptr<PageElement>>& elements, GfxRenderer& renderer,
                                const int fontId, const int xOffset, const int yOffset, Predicate&& predicate) {
  for (const auto& element : elements) {
    if (predicate(*element)) {
      element->render(renderer, fontId, xOffset, yOffset);
    }
  }
}

bool isBorderBox(const PageElement& element) { return element.getTag() == TAG_PageBorderBox; }

void renderBoxBackgrounds(const std::vector<std::unique_ptr<PageElement>>& elements, GfxRenderer& renderer,
                          const int xOffset, const int yOffset) {
  for (const auto& element : elements) {
    if (isBorderBox(*element)) {
      static_cast<const PageBorderBox&>(*element).renderBackground(renderer, xOffset, yOffset);
    }
  }
}

// Draws one border edge as a strip along a box side.
void drawBorderEdge(const GfxRenderer& renderer, const int x, const int y, const int length, const CssBorderSide& side,
                    const bool horizontal) {
  const int thickness = side.width;
  const auto strip = [&](const int offset, const int along, const int span, const int depth) {
    if (horizontal) {
      renderer.fillRect(x + along, y + offset, span, depth, true);
    } else {
      renderer.fillRect(x + offset, y + along, depth, span, true);
    }
  };
  switch (side.style) {
    case CssBorderStyle::Double:
      if (thickness >= 3) {
        const int line = std::max(1, thickness / 3);
        strip(0, 0, length, line);
        strip(thickness - line, 0, length, line);
        return;
      }
      break;
    case CssBorderStyle::Dotted:
    case CssBorderStyle::Dashed: {
      const int dash = side.style == CssBorderStyle::Dotted ? thickness : std::max(3, thickness * 3);
      const int gap = side.style == CssBorderStyle::Dotted ? std::max(2, thickness) : std::max(2, thickness * 2);
      for (int along = 0; along < length; along += dash + gap)
        strip(0, along, std::min(dash, length - along), thickness);
      return;
    }
    default:
      break;
  }
  strip(0, 0, length, thickness);
}

}  // namespace

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageLine::serialize(HalFile& file) {
  return sectioncodec::write(file, sectioncodec::record(XP_SEC_POSITION, {uint32_t(xPos), uint32_t(yPos)})) &&
         block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(HalFile& file) {
  XpSecRecord position;
  if (!sectioncodec::read(file, XP_SEC_POSITION, position)) return nullptr;
  auto block = TextBlock::deserialize(file);
  if (!block) return nullptr;
  auto element = makeUniqueNoThrow<PageLine>(std::move(block), position.values[0], position.values[1]);
  if (!element) LOG_ERR("PGE", "Could not allocate PageLine");
  return element;
}

void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  // Images don't use fontId or text rendering
  imageBlock->render(renderer, xPos + xOffset, yPos + yOffset);
}

void PageImage::renderPlaceholder(GfxRenderer& renderer, const int xOffset, const int yOffset) const {
  imageBlock->renderPlaceholder(renderer, xPos + xOffset, yPos + yOffset);
}

bool PageImage::serialize(HalFile& file) {
  return sectioncodec::write(file, sectioncodec::record(XP_SEC_POSITION, {uint32_t(xPos), uint32_t(yPos)})) &&
         imageBlock->serialize(file);
}

std::unique_ptr<PageImage> PageImage::deserialize(HalFile& file) {
  XpSecRecord position;
  if (!sectioncodec::read(file, XP_SEC_POSITION, position)) return nullptr;
  auto block = ImageBlock::deserialize(file);
  if (!block) return nullptr;
  auto element = makeUniqueNoThrow<PageImage>(std::move(block), position.values[0], position.values[1]);
  if (!element) LOG_ERR("PGE", "Could not allocate PageImage");
  return element;
}

void PageHorizontalRule::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  (void)fontId;
  if (width == 0 || thickness == 0) {
    return;
  }

  renderer.drawLine(xPos + xOffset, yPos + yOffset, xPos + xOffset + width - 1, yPos + yOffset, thickness, true);
}

bool PageHorizontalRule::serialize(HalFile& file) {
  return sectioncodec::write(file,
                             sectioncodec::record(XP_SEC_RULE, {uint32_t(xPos), uint32_t(yPos), width, thickness}));
}

std::unique_ptr<PageHorizontalRule> PageHorizontalRule::deserialize(HalFile& file) {
  XpSecRecord record;
  if (!sectioncodec::read(file, XP_SEC_RULE, record)) return nullptr;
  auto element =
      makeUniqueNoThrow<PageHorizontalRule>(record.values[2], record.values[3], record.values[0], record.values[1]);
  if (!element) LOG_ERR("PGE", "Could not allocate PageHorizontalRule");
  return element;
}

PageDropCap::PageDropCap(const int32_t fontId, const uint16_t scale256, const EpdFontFamily::Style style,
                         const char* utf8, const int16_t xPos, const int16_t yPos)
    : PageElement(xPos, yPos), fontId(fontId), scale256(scale256), style(style) {
  strncpy(text, utf8, MAX_TEXT_BYTES);
}

void PageDropCap::render(GfxRenderer& renderer, const int sectionFontId, const int xOffset, const int yOffset) {
  // A sized variant that can no longer be loaded draws from the section font instead.
  const int drawFontId = renderer.ensureFontLoaded(fontId) ? fontId : sectionFontId;
  int x = xPos + xOffset;
  const auto* cursor = reinterpret_cast<const unsigned char*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&cursor)) {
    x += renderer.drawScaledCodepoint(drawFontId, cp, style, x, yPos + yOffset, scale256);
  }
}

bool PageDropCap::serialize(HalFile& file) {
  const auto length = static_cast<uint8_t>(strnlen(text, MAX_TEXT_BYTES));
  return sectioncodec::write(
             file, sectioncodec::record(XP_SEC_DROP_CAP, {uint32_t(xPos), uint32_t(yPos), uint32_t(fontId), scale256,
                                                          uint32_t(style), length})) &&
         file.write(text, length) == length;
}

std::unique_ptr<PageDropCap> PageDropCap::deserialize(HalFile& file) {
  XpSecRecord record;
  if (!sectioncodec::read(file, XP_SEC_DROP_CAP, record)) return nullptr;
  char text[MAX_TEXT_BYTES + 1] = {};
  if (file.read(text, record.values[5]) != record.values[5]) return nullptr;
  auto element = makeUniqueNoThrow<PageDropCap>(record.values[2], record.values[3],
                                                static_cast<EpdFontFamily::Style>(record.values[4]), text,
                                                record.values[0], record.values[1]);
  if (!element) LOG_ERR("PGE", "Could not allocate PageDropCap");
  return element;
}

void PageBorderBox::renderBackground(GfxRenderer& renderer, const int xOffset, const int yOffset) const {
  if (shaded) renderer.fillRectDither(xPos + xOffset, yPos + yOffset, width, height, Color::LightGray);
}

void PageBorderBox::render(GfxRenderer& renderer, const int, const int xOffset, const int yOffset) {
  const int x = xPos + xOffset;
  const int y = yPos + yOffset;
  const CssBorderSide& top = sides[0];
  const CssBorderSide& right = sides[1];
  const CssBorderSide& bottom = sides[2];
  const CssBorderSide& left = sides[3];
  if (top.visible()) drawBorderEdge(renderer, x, y, width, top, true);
  if (bottom.visible()) drawBorderEdge(renderer, x, y + height - bottom.width, width, bottom, true);
  if (left.visible()) drawBorderEdge(renderer, x, y, height, left, false);
  if (right.visible()) drawBorderEdge(renderer, x + width - right.width, y, height, right, false);
}

bool PageBorderBox::serialize(HalFile& file) {
  return sectioncodec::write(
      file, sectioncodec::record(
                XP_SEC_BORDER, {uint32_t(xPos), uint32_t(yPos), width, height, sides[0].width, uint32_t(sides[0].style),
                                sides[1].width, uint32_t(sides[1].style), sides[2].width, uint32_t(sides[2].style),
                                sides[3].width, uint32_t(sides[3].style), shaded}));
}

std::unique_ptr<PageBorderBox> PageBorderBox::deserialize(HalFile& file) {
  XpSecRecord record;
  if (!sectioncodec::read(file, XP_SEC_BORDER, record)) return nullptr;
  CssBorderSide sides[4];
  for (size_t i = 0; i < 4; ++i) {
    sides[i].width = record.values[4 + i * 2];
    sides[i].style = static_cast<CssBorderStyle>(record.values[5 + i * 2]);
  }
  auto element = makeUniqueNoThrow<PageBorderBox>(record.values[2], record.values[3], sides, record.values[12] != 0,
                                                  record.values[0], record.values[1]);
  if (!element) LOG_ERR("PGE", "Could not allocate PageBorderBox");
  return element;
}

// Backgrounds precede every border, including borders inside shaded containers.
void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  renderBoxBackgrounds(elements, renderer, xOffset, yOffset);
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset, isBorderBox);
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset,
                             [](const PageElement& element) { return !isBorderBox(element); });
}

void Page::renderImages(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset,
                             [](const PageElement& element) { return element.getTag() == TAG_PageImage; });
}

void Page::renderWithImagePlaceholders(GfxRenderer& renderer, const int fontId, const int xOffset,
                                       const int yOffset) const {
  renderBoxBackgrounds(elements, renderer, xOffset, yOffset);
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset, isBorderBox);
  for (const auto& element : elements) {
    if (isBorderBox(*element)) continue;
    if (element->getTag() == TAG_PageImage) {
      static_cast<const PageImage&>(*element).renderPlaceholder(renderer, xOffset, yOffset);
    } else {
      element->render(renderer, fontId, xOffset, yOffset);
    }
  }
}

bool Page::serialize(HalFile& file) const {
  const uint16_t count = elements.size();
  if (!sectioncodec::writeValue(file, XP_SEC_U16, count)) return false;

  for (const auto& el : elements) {
    // Use getTag() method to determine type
    if (!sectioncodec::writeValue(file, XP_SEC_ELEMENT_TAG, el->getTag())) return false;

    if (!el->serialize(file)) {
      return false;
    }
  }

  // Serialize footnotes (clamp to MAX_FOOTNOTES_PER_PAGE to match addFootnote/deserialize limits)
  const uint16_t fnCount = std::min<uint16_t>(footnotes.size(), MAX_FOOTNOTES_PER_PAGE);
  if (!sectioncodec::writeValue(file, XP_SEC_FOOTNOTE_COUNT, fnCount)) return false;
  for (uint16_t i = 0; i < fnCount; i++) {
    const auto& fn = footnotes[i];
    if (file.write(fn.number, sizeof(fn.number)) != sizeof(fn.number) ||
        file.write(fn.href, sizeof(fn.href)) != sizeof(fn.href)) {
      LOG_ERR("PGE", "Failed to write footnote");
      return false;
    }
  }

  const uint16_t linkCount = std::min<uint16_t>(links.size(), MAX_LINKS_PER_PAGE);
  if (!sectioncodec::writeValue(file, XP_SEC_LINK_COUNT, linkCount)) return false;
  for (uint16_t i = 0; i < linkCount; i++) {
    const auto& link = links[i];
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

std::unique_ptr<Page> Page::deserialize(HalFile& file) {
  auto page = makeUniqueNoThrow<Page>();
  if (!page) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate Page");
    return nullptr;
  }

  uint32_t count;
  if (!sectioncodec::readValue(file, XP_SEC_U16, count)) return nullptr;

  // Reserve up front so a page load costs one allocation for the element vector
  // instead of a grow-copy-free cycle every doubling. `count` is untrusted (it
  // comes straight off the SD cache), so clamp it: a real page holds a few dozen
  // elements, while a corrupt header could ask for 65535 * sizeof(unique_ptr) and
  // abort() on the failed allocation (vector's operator new is throwing, and this
  // firmware builds with -fno-exceptions). Under-reserving is harmless -- the
  // push_back path below still grows normally.
  static constexpr uint16_t RESERVE_CAP = 256;
  page->elements.reserve(std::min<uint32_t>(count, RESERVE_CAP));

  for (uint16_t i = 0; i < count; i++) {
    uint32_t tag;
    if (!sectioncodec::readValue(file, XP_SEC_ELEMENT_TAG, tag)) return nullptr;

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      if (!pl) {
        return nullptr;
      }
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      if (!pi) {
        return nullptr;
      }
      page->elements.push_back(std::move(pi));
    } else if (tag == TAG_PageHorizontalRule) {
      auto rule = PageHorizontalRule::deserialize(file);
      if (!rule) {
        return nullptr;
      }
      page->elements.push_back(std::move(rule));
    } else if (tag == TAG_PageDropCap) {
      auto dropCap = PageDropCap::deserialize(file);
      if (!dropCap) {
        return nullptr;
      }
      page->elements.push_back(std::move(dropCap));
    } else if (tag == TAG_PageBorderBox) {
      auto box = PageBorderBox::deserialize(file);
      if (!box) {
        return nullptr;
      }
      page->elements.push_back(std::move(box));
    } else {
      LOG_ERR("PGE", "Deserialization failed: Unknown tag %u", tag);
      return nullptr;
    }
  }

  // Deserialize footnotes
  uint32_t fnCount;
  if (!sectioncodec::readValue(file, XP_SEC_FOOTNOTE_COUNT, fnCount)) return nullptr;
  page->footnotes.resize(fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    auto& entry = page->footnotes[i];
    if (file.read(entry.number, sizeof(entry.number)) != sizeof(entry.number) ||
        file.read(entry.href, sizeof(entry.href)) != sizeof(entry.href)) {
      LOG_ERR("PGE", "Failed to read footnote %u", i);
      return nullptr;
    }
    entry.number[sizeof(entry.number) - 1] = '\0';
    entry.href[sizeof(entry.href) - 1] = '\0';
  }

  uint32_t linkCount;
  if (!sectioncodec::readValue(file, XP_SEC_LINK_COUNT, linkCount)) return nullptr;
  page->links.resize(linkCount);
  for (uint16_t i = 0; i < linkCount; i++) {
    auto& link = page->links[i];
    if (file.read(link.href, sizeof(link.href)) != sizeof(link.href)) {
      LOG_ERR("PGE", "Failed to read link %u", i);
      return nullptr;
    }
    link.href[sizeof(link.href) - 1] = '\0';
    XpSecRecord geometry;
    if (!sectioncodec::read(file, XP_SEC_LINK_GEOMETRY, geometry)) return nullptr;
    link.x = geometry.values[0];
    link.y = geometry.values[1];
    link.width = geometry.values[2];
    link.height = geometry.values[3];
    if (link.href[0] == '\0') {
      LOG_ERR("PGE", "Invalid link geometry %u", i);
      return nullptr;
    }
  }

  return page;
}
