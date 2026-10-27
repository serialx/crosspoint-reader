#include "Page.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include "SectionCodec.h"

namespace {

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

// Backgrounds precede every border, including borders inside shaded containers.
void Page::render(GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  for (uint16_t i = 0; i < elementCount(); ++i) element(i).renderBackground(renderer, x, y);
  for (uint16_t i = 0; i < elementCount(); ++i) {
    const auto el = element(i);
    if (el.getTag() == TAG_PageBorderBox) el.render(renderer, fontId, x, y);
  }
  for (uint16_t i = 0; i < elementCount(); ++i) {
    const auto el = element(i);
    if (el.getTag() != TAG_PageBorderBox) el.render(renderer, fontId, x, y);
  }
}

void Page::renderImages(GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  for (uint16_t i = 0; i < elementCount(); ++i) {
    const auto el = element(i);
    if (el.getTag() == TAG_PageImage) el.render(renderer, fontId, x, y);
  }
}

void Page::renderWithImagePlaceholders(GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  for (uint16_t i = 0; i < elementCount(); ++i) element(i).renderBackground(renderer, x, y);
  for (uint16_t i = 0; i < elementCount(); ++i) {
    const auto el = element(i);
    if (el.getTag() == TAG_PageBorderBox) el.render(renderer, fontId, x, y);
  }
  for (uint16_t i = 0; i < elementCount(); ++i) {
    const auto el = element(i);
    if (el.getTag() == TAG_PageBorderBox) continue;
    if (el.getTag() == TAG_PageImage)
      el.renderPlaceholder(renderer, x, y);
    else
      el.render(renderer, fontId, x, y);
  }
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

std::unique_ptr<Page> Page::load(HalFile& file, const size_t bytes, const uint32_t visibleOffset) {
  const size_t start = file.position();
  if (bytes < 6 || start > file.size() || bytes > file.size() - start) return nullptr;
  auto input = makeUniqueNoThrow<uint8_t[]>(bytes);
  if (!input) {
    LOG_ERR("PGE", "OOM: page input %u bytes", static_cast<unsigned>(bytes));
    return nullptr;
  }
  if (file.read(input.get(), bytes) != bytes) return nullptr;
  auto page = fromBytes(input.get(), bytes, visibleOffset);
  if (page && !file.seek(start + page->info.consumed)) return nullptr;
  return page;
}

std::unique_ptr<Page> Page::fromBytes(const uint8_t* bytes, const size_t size, const uint32_t visibleOffset) {
  XpPageInfo info{};
  if (xp_page_arena_size(bytes, size, &info)) return nullptr;
  auto page = makeUniqueNoThrow<Page>();
  if (!page) {
    LOG_ERR("PGE", "OOM: Page %u bytes", static_cast<unsigned>(sizeof(Page)));
    return nullptr;
  }
  // Plain descriptors share the Rust arena allocation; no element owns heap storage.
  const size_t alignment = alignof(ElementData);
  if (info.arena_size > SIZE_MAX - alignment + 1) return nullptr;
  const size_t viewOffset = (info.arena_size + alignment - 1) & ~(alignment - 1);
  if (info.elements > (SIZE_MAX - viewOffset) / sizeof(ElementData)) return nullptr;
  const size_t allocationSize = viewOffset + info.elements * sizeof(ElementData);
  page->arena = makeUniqueNoThrow<uint8_t[]>(allocationSize);
  if (!page->arena) {
    LOG_ERR("PGE", "OOM: page arena %u bytes", static_cast<unsigned>(allocationSize));
    return nullptr;
  }
  if (xp_page_decode(bytes, size, visibleOffset, page->arena.get(), info.arena_size, &page->info)) return nullptr;
  page->arenaSize = info.arena_size;
  page->views = reinterpret_cast<ElementData*>(page->arena.get() + viewOffset);
  for (uint16_t i = 0; i < info.elements; ++i) {
    auto& view = *new (&page->views[i]) ElementData{};
    view.position = xp_page_position(page->arena.get(), page->arenaSize, i);
    if (!view.position.tag) return nullptr;
    if (view.position.tag == TAG_PageLine) {
      new (&view.line) TextBlock::ViewData{};
      if (!TextBlock::resolveView(page->arena.get(), page->arenaSize, i, view.line)) return nullptr;
    } else if (xp_page_element(page->arena.get(), page->arenaSize, i, &view.fields)) {
      return nullptr;
    }
  }
  page->visibleTextOffset = visibleOffset;
  return page;
}

void Page::footnote(const uint16_t index, FootnoteEntry& out) const {
  if (!arena) {
    out = footnotes[index];
    return;
  }
  XpPageSpan number{}, href{};
  if (xp_page_footnote(arena.get(), arenaSize, index, &number, &href)) {
    out = {};
    return;
  }
  memcpy(out.number, number.data, sizeof(out.number));
  memcpy(out.href, href.data, sizeof(out.href));
}

void Page::link(const uint16_t index, PageLink& out) const {
  if (!arena) {
    out = links[index];
    return;
  }
  XpPageLink view{};
  if (xp_page_link(arena.get(), arenaSize, index, &view)) {
    out = {};
    return;
  }
  memcpy(out.href, view.href.data, sizeof(out.href));
  out.x = view.x;
  out.y = view.y;
  out.width = view.width;
  out.height = view.height;
}

void Page::copyFootnotes(std::vector<FootnoteEntry>& out) const {
  out.resize(footnoteCount());
  for (uint16_t i = 0; i < out.size(); ++i) footnote(i, out[i]);
}

void Page::copyLinks(std::vector<PageLink>& out) const {
  out.resize(linkCount());
  for (uint16_t i = 0; i < out.size(); ++i) link(i, out[i]);
}

PageElementView::PageElementView(const Page& owner, const uint16_t index) : page(&owner), index(index) {
  if (page->arena) {
    const auto& e = page->views[index].position;
    tag = static_cast<PageElementTag>(e.tag);
    xPos = e.x;
    yPos = e.y;
  } else {
    const auto& e = *page->elements[index];
    tag = e.getTag();
    xPos = e.xPos;
    yPos = e.yPos;
  }
}

const XpPageElement& PageElementView::fields() const { return page->views[index].fields; }

TextBlock PageElementView::textBlock() const {
  if (page->arena) return TextBlock(TextBlock::View{}, page->arena.get(), page->views[index].line);
  return TextBlock(TextBlock::View{}, *static_cast<const PageLine&>(*page->elements[index]).getBlock());
}

ImageBlock PageElementView::imageBlock() const {
  if (!page->arena)
    return ImageBlock(ImageBlock::View{}, static_cast<const PageImage&>(*page->elements[index]).getImageBlock());
  const auto& e = fields();
  return ImageBlock(ImageBlock::View{}, {reinterpret_cast<const char*>(e.first.data), e.first.len},
                    {reinterpret_cast<const char*>(e.second.data), e.second.len}, e.width, e.height);
}

namespace {
PageBorderBox borderView(const XpPageElement& e) {
  const auto& v = e.record.values;
  CssBorderSide sides[4];
  for (size_t i = 0; i < 4; ++i) {
    sides[i].width = v[4 + 2 * i];
    sides[i].style = static_cast<CssBorderStyle>(v[5 + 2 * i]);
  }
  return PageBorderBox(v[2], v[3], sides, v[12] != 0, v[0], v[1]);
}
PageDropCap dropCapView(const XpPageElement& e) {
  char text[PageDropCap::MAX_TEXT_BYTES + 1]{};
  memcpy(text, e.first.data, e.first.len);
  const auto& v = e.record.values;
  return PageDropCap(v[2], v[3], static_cast<EpdFontFamily::Style>(v[4]), text, v[0], v[1]);
}
}  // namespace

void PageElementView::render(GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  if (!page->arena) {
    page->elements[index]->render(renderer, fontId, x, y);
    return;
  }
  switch (tag) {
    case TAG_PageLine:
      textBlock().render(renderer, fontId, xPos + x, yPos + y);
      return;
    case TAG_PageImage:
      imageBlock().render(renderer, xPos + x, yPos + y);
      return;
    case TAG_PageHorizontalRule: {
      const auto& e = fields();
      PageHorizontalRule(e.record.values[2], e.record.values[3], xPos, yPos).render(renderer, fontId, x, y);
      return;
    }
    case TAG_PageDropCap:
      dropCapView(fields()).render(renderer, fontId, x, y);
      return;
    case TAG_PageBorderBox:
      borderView(fields()).render(renderer, fontId, x, y);
      return;
  }
}

void PageElementView::renderBackground(GfxRenderer& renderer, const int x, const int y) const {
  if (tag != TAG_PageBorderBox) return;
  if (page->arena)
    borderView(fields()).renderBackground(renderer, x, y);
  else
    static_cast<const PageBorderBox&>(*page->elements[index]).renderBackground(renderer, x, y);
}

void PageElementView::renderPlaceholder(GfxRenderer& renderer, const int x, const int y) const {
  imageBlock().renderPlaceholder(renderer, xPos + x, yPos + y);
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

bool Page::hasImages() const {
  for (uint16_t i = 0; i < elementCount(); ++i)
    if (element(i).getTag() == TAG_PageImage) return true;
  return false;
}

bool Page::hasImagesNeedingDecode() const {
  for (uint16_t i = 0; i < elementCount(); ++i) {
    const auto e = element(i);
    if (e.getTag() == TAG_PageImage && e.imageBlock().needsDecode()) return true;
  }
  return false;
}

bool Page::getImageBoundingBox(int16_t& x, int16_t& y, int16_t& w, int16_t& h) const {
  bool found = false;
  int16_t minX = INT16_MAX, minY = INT16_MAX, maxX = INT16_MIN, maxY = INT16_MIN;
  for (uint16_t i = 0; i < elementCount(); ++i) {
    const auto e = element(i);
    if (e.getTag() != TAG_PageImage) continue;
    const auto img = e.imageBlock();
    const int16_t right = e.xPos + img.getWidth(), bottom = e.yPos + img.getHeight();
    minX = std::min(minX, e.xPos);
    minY = std::min(minY, e.yPos);
    maxX = std::max(maxX, right);
    maxY = std::max(maxY, bottom);
    found = true;
  }
  if (found) {
    x = minX;
    y = minY;
    w = maxX - minX;
    h = maxY - minY;
  }
  return found;
}

std::string_view PageElementView::dropCapText() const {
  if (!page->arena) return static_cast<const PageDropCap&>(*page->elements[index]).getText();
  const auto& e = fields();
  return {reinterpret_cast<const char*>(e.first.data), e.first.len};
}
