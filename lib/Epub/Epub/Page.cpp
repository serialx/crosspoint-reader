#include "Page.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include "SectionCodec.h"

#ifdef XP_PAGE_CODEC_TEST
PageDropCap::PageDropCap(const int32_t fontId, const uint16_t scale256, const EpdFontFamily::Style style,
                         const char* utf8, const int16_t xPos, const int16_t yPos)
    : PageElement(xPos, yPos), fontId(fontId), scale256(scale256), style(style) {
  strncpy(text, utf8, MAX_TEXT_BYTES);
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
  renderArena(renderer, fontId, x, y, PageRenderMode::All);
}

void Page::renderImages(GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  renderArena(renderer, fontId, x, y, PageRenderMode::Images);
}

void Page::renderWithImagePlaceholders(GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  renderArena(renderer, fontId, x, y, PageRenderMode::Placeholders);
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
#ifdef XP_PAGE_CODEC_TEST
  if (!arena) {
    out = footnotes[index];
    return;
  }
#endif
  XpPageSpan number{}, href{};
  if (xp_page_footnote(arena, arenaSize, index, &number, &href)) {
    out = {};
    return;
  }
  memcpy(out.number, number.data, sizeof(out.number));
  memcpy(out.href, href.data, sizeof(out.href));
}

void Page::link(const uint16_t index, PageLink& out) const {
#ifdef XP_PAGE_CODEC_TEST
  if (!arena) {
    out = links[index];
    return;
  }
#endif
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

void PageMetadata::clear() {
  ownedFootnotes.reset();
  ownedLinks.reset();
  footnoteSize = linkSize = 0;
}

bool PageMetadata::assign(const Page& page) {
  clear();
  const auto notes = page.footnoteCount();
  const auto links = page.linkCount();
  auto newNotes = notes ? makeUniqueNoThrow<FootnoteEntry[]>(notes) : nullptr;
  if (notes && !newNotes) {
    LOG_ERR("PGE", "OOM: footnotes %u bytes", static_cast<unsigned>(notes * sizeof(FootnoteEntry)));
    return false;
  }
  auto newLinks = links ? makeUniqueNoThrow<PageLink[]>(links) : nullptr;
  if (links && !newLinks) {
    LOG_ERR("PGE", "OOM: links %u bytes", static_cast<unsigned>(links * sizeof(PageLink)));
    return false;
  }
  for (uint16_t i = 0; i < notes; ++i) page.footnote(i, newNotes[i]);
  for (uint16_t i = 0; i < links; ++i) page.link(i, newLinks[i]);
  ownedFootnotes = std::move(newNotes);
  ownedLinks = std::move(newLinks);
  footnoteSize = notes;
  linkSize = links;
  return true;
}

PageElementView::PageElementView(const Page& owner, const uint16_t index) : page(&owner), index(index) {
#ifdef XP_PAGE_CODEC_TEST
  if (!page->arena) {
    const auto& e = *page->elements[index];
    tag = e.getTag();
    xPos = e.xPos;
    yPos = e.yPos;
    return;
  }
#endif
  const auto& e = page->views[index].position;
  tag = static_cast<PageElementTag>(e.tag);
  xPos = e.x;
  yPos = e.y;
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
#ifdef XP_PAGE_CODEC_TEST
  if (!page->arena)
    return TextBlock(TextBlock::View{}, *static_cast<const PageLine&>(*page->elements[index]).getBlock());
#endif
  return TextBlock(TextBlock::View{}, page->arena, page->views[index].line);
}

ImageBlock PageElementView::imageBlock() const {
#ifdef XP_PAGE_CODEC_TEST
  if (!page->arena)
    return ImageBlock(ImageBlock::View{}, static_cast<const PageImage&>(*page->elements[index]).getImageBlock());
#endif
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
#ifdef XP_PAGE_CODEC_TEST
  if (!page->arena) return static_cast<const PageDropCap&>(*page->elements[index]).getText();
#endif
  const auto& e = fields();
  return {reinterpret_cast<const char*>(e.first.data), e.first.len};
}

LinePageBuilder::LinePageBuilder(const size_t capacity)
    : bytes(makeUniqueNoThrow<uint8_t[]>(capacity)), capacity(capacity) {
  if (!bytes) LOG_ERR("PGE", "OOM: line page buffer %u bytes", static_cast<unsigned>(capacity));
  reset();
}

bool LinePageBuilder::reset() {
  valid = bytes && xp_page_builder_init(&state, bytes.get(), capacity) == 0;
  return valid;
}

bool LinePageBuilder::addLine(const XpParaLine& line, const int16_t x, const int16_t y) {
  if (!valid) return false;
  valid = xp_page_builder_line(&state, x, y, &line) == 0;
  if (!valid) LOG_ERR("PGE", "Line page exceeds %u byte buffer or has invalid data", static_cast<unsigned>(capacity));
  return valid;
}

std::unique_ptr<Page> LinePageBuilder::finish() {
  size_t size = 0;
  if (!valid || xp_page_builder_finish(&state, &size)) return nullptr;
  return Page::fromBytes(bytes.get(), size);
}
