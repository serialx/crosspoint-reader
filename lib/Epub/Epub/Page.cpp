#include "Page.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include "SectionCodec.h"

#ifdef XP_PAGE_CODEC_TEST
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

#endif

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

#ifdef XP_PAGE_CODEC_TEST
void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  // Images don't use fontId or text rendering
  imageBlock->render(renderer, xPos + xOffset, yPos + yOffset);
}

void PageImage::renderPlaceholder(GfxRenderer& renderer, const int xOffset, const int yOffset) const {
  imageBlock->renderPlaceholder(renderer, xPos + xOffset, yPos + yOffset);
}

void PageHorizontalRule::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  (void)fontId;
  if (width == 0 || thickness == 0) {
    return;
  }

  renderer.drawLine(xPos + xOffset, yPos + yOffset, xPos + xOffset + width - 1, yPos + yOffset, thickness, true);
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

#endif

void Page::renderArena(GfxRenderer& renderer, const int fontId, const int x, const int y,
                       const PageRenderMode mode) const {
  struct Images {
    const Page& page;
    GfxRenderer& renderer;
  } images{*this, renderer};
  renderer.drawPage(arena, arenaSize, fontId, x, y, mode, &images,
                    [](void* raw, uint16_t index, int32_t x, int32_t y, uint8_t placeholder) {
                      const auto& c = *static_cast<Images*>(raw);
                      auto block = c.page.element(index).imageBlock();
                      if (placeholder)
                        block.renderPlaceholder(c.renderer, x, y);
                      else
                        block.render(c.renderer, x, y);
                    });
}

void Page::render(GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  if (arena) {
    renderArena(renderer, fontId, x, y, PageRenderMode::All);
    return;
  }
  // Plain dictionary pages own PageLine objects; cached and rich pages use the arena.
  for (const auto& element : elements) element->render(renderer, fontId, x, y);
}

void Page::renderImages(GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  if (arena) renderArena(renderer, fontId, x, y, PageRenderMode::Images);
}

void Page::renderWithImagePlaceholders(GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  if (arena)
    renderArena(renderer, fontId, x, y, PageRenderMode::Placeholders);
  else
    render(renderer, fontId, x, y);
}

std::unique_ptr<Page> Page::allocate(const XpPageInfo& info) {
  const size_t alignment = alignof(ElementData);
  if (info.arena_size > SIZE_MAX - sizeof(Page) - alignment + 1) return nullptr;
  const size_t viewOffset = (sizeof(Page) + info.arena_size + alignment - 1) & ~(alignment - 1);
  if (info.elements > (SIZE_MAX - viewOffset) / sizeof(ElementData)) return nullptr;
  const size_t bytes = viewOffset + info.elements * sizeof(ElementData);
  // Page owns the header and trailing records; its unsized delete matches this allocation.
  void* allocation = ::operator new(bytes, std::nothrow);
  if (!allocation) {
    LOG_ERR("PGE", "OOM: page %u bytes", static_cast<unsigned>(bytes));
    return nullptr;
  }
  std::unique_ptr<Page> page(::new (allocation) Page);
  page->arena = static_cast<uint8_t*>(allocation) + sizeof(Page);
  page->arenaSize = info.arena_size;
  page->views = reinterpret_cast<ElementData*>(static_cast<uint8_t*>(allocation) + viewOffset);
  page->info = info;
  return page;
}

bool Page::resolveViews() {
  for (uint16_t i = 0; i < info.elements; ++i) {
    auto& view = *new (&views[i]) ElementData{};
    view.position = xp_page_position(arena, arenaSize, i);
    if (!view.position.tag) return false;
    if (view.position.tag == TAG_PageLine) {
      if (!TextBlock::resolveView(arena, arenaSize, i, view.line)) return false;
      continue;
    }
    XpPageElement value{};
    if (xp_page_element(arena, arenaSize, i, &value)) return false;
    const auto* v = value.record.values;
    switch (view.position.tag) {
      case TAG_PageImage:
        view.image = {value.first, value.second, value.width, value.height};
        break;
      case TAG_PageHorizontalRule:
        view.rule = {static_cast<uint16_t>(v[2]), static_cast<uint8_t>(v[3])};
        break;
      case TAG_PageDropCap:
        view.cap = {value.first, static_cast<int32_t>(v[2]), static_cast<uint16_t>(v[3]), static_cast<uint8_t>(v[4])};
        break;
      case TAG_PageBorderBox:
        view.border.width = v[2];
        view.border.height = v[3];
        for (size_t j = 0; j < 8; ++j) view.border.sides[j] = v[4 + j];
        view.border.shaded = v[12];
        break;
    }
  }
  return true;
}

std::unique_ptr<Page> Page::load(HalFile& file, const size_t bytes, const uint32_t visibleOffset) {
  const size_t start = file.position();
  if (bytes < 6 || start > file.size() || bytes > file.size() - start) return nullptr;
  struct Input {
    HalFile& file;
    size_t start;
  } input{file, start};
  const auto read = [](void* context, size_t offset, uint8_t* data, size_t len) -> uint8_t {
    auto& in = *static_cast<Input*>(context);
    return in.file.seek(in.start + offset) && in.file.read(data, len) == len;
  };
  XpPageInfo sized{};
  if (xp_page_probe(&input, read, bytes, &sized)) return nullptr;
  auto page = allocate(sized);
  if (!page) return nullptr;
  const size_t offset = sized.arena_size - sized.consumed;
  if (!file.seek(start) || file.read(page->arena + offset, sized.consumed) != sized.consumed ||
      xp_page_decode_in_place(page->arena, page->arenaSize, offset, sized.consumed, visibleOffset, &page->info) ||
      page->info.elements != sized.elements || !page->resolveViews())
    return nullptr;
  page->visibleTextOffset = visibleOffset;
  return page;
}

std::unique_ptr<Page> Page::fromBytes(const uint8_t* bytes, const size_t size, const uint32_t visibleOffset) {
  XpPageInfo info{};
  if (xp_page_arena_size(bytes, size, &info)) return nullptr;
  auto page = allocate(info);
  if (!page || xp_page_decode(bytes, size, visibleOffset, page->arena, page->arenaSize, &page->info) ||
      !page->resolveViews())
    return nullptr;
  page->visibleTextOffset = visibleOffset;
  return page;
}

void Page::footnote(const uint16_t index, FootnoteEntry& out) const {
  if (!arena) {
    out = footnotes[index];
    return;
  }
  XpPageSpan number{}, href{};
  if (xp_page_footnote(arena, arenaSize, index, &number, &href)) {
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
  if (xp_page_link(arena, arenaSize, index, &view)) {
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

XpPageElement PageElementView::fields() const {
  const auto& view = page->views[index];
  XpPageElement value{};
  value.tag = tag;
  auto* v = value.record.values;
  v[0] = static_cast<uint32_t>(xPos);
  v[1] = static_cast<uint32_t>(yPos);
  switch (tag) {
    case TAG_PageImage:
      value.first = view.image.path;
      value.second = view.image.source;
      value.width = view.image.width;
      value.height = view.image.height;
      break;
    case TAG_PageHorizontalRule:
      value.record.kind = XP_SEC_RULE;
      v[2] = view.rule.width;
      v[3] = view.rule.thickness;
      break;
    case TAG_PageDropCap:
      value.record.kind = XP_SEC_DROP_CAP;
      value.first = view.cap.text;
      v[2] = view.cap.font;
      v[3] = view.cap.scale;
      v[4] = view.cap.style;
      v[5] = view.cap.text.len;
      break;
    case TAG_PageBorderBox:
      value.record.kind = XP_SEC_BORDER;
      v[2] = view.border.width;
      v[3] = view.border.height;
      for (size_t i = 0; i < 8; ++i) v[4 + i] = view.border.sides[i];
      v[12] = view.border.shaded;
      break;
    case TAG_PageLine:
      break;
  }
  return value;
}

TextBlock PageElementView::textBlock() const {
  if (page->arena) return TextBlock(TextBlock::View{}, page->arena, page->views[index].line);
  return TextBlock(TextBlock::View{}, *static_cast<const PageLine&>(*page->elements[index]).getBlock());
}

ImageBlock PageElementView::imageBlock() const {
  if (!page->arena)
    return ImageBlock(ImageBlock::View{}, static_cast<const PageImage&>(*page->elements[index]).getImageBlock());
  const auto& e = fields();
  return ImageBlock(ImageBlock::View{}, {reinterpret_cast<const char*>(e.first.data), e.first.len},
                    {reinterpret_cast<const char*>(e.second.data), e.second.len}, e.width, e.height);
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
