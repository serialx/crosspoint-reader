#pragma once
#include <HalStorage.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "FootnoteEntry.h"
#include "PageLink.h"
#include "blocks/ImageBlock.h"
#include "blocks/TextBlock.h"

enum PageElementTag : uint8_t {
  TAG_PageLine = 1,
  TAG_PageImage = 2,
  TAG_PageHorizontalRule = 3,
  TAG_PageDropCap = 4,
  TAG_PageBorderBox = 5,
};

// represents something that has been added to a page
class PageElement {
 public:
  int16_t xPos;
  int16_t yPos;
  explicit PageElement(const int16_t xPos, const int16_t yPos) : xPos(xPos), yPos(yPos) {}
  virtual ~PageElement() = default;
  virtual void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) = 0;
#ifdef XP_PAGE_CODEC_TEST
  virtual bool serialize(HalFile& file) = 0;
#endif
  virtual PageElementTag getTag() const = 0;  // Add type identification
};

// a line from a block element
class PageLine final : public PageElement {
  std::unique_ptr<TextBlock> block;

 public:
  PageLine(std::unique_ptr<TextBlock> block, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), block(std::move(block)) {}
  const TextBlock* getBlock() const { return block.get(); }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
#ifdef XP_PAGE_CODEC_TEST
  bool serialize(HalFile& file) override;
#endif
  PageElementTag getTag() const override { return TAG_PageLine; }
};

// New PageImage class
class PageImage final : public PageElement {
  std::unique_ptr<ImageBlock> imageBlock;

 public:
  PageImage(std::unique_ptr<ImageBlock> block, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), imageBlock(std::move(block)) {}
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
  void renderPlaceholder(GfxRenderer& renderer, int xOffset, int yOffset) const;
#ifdef XP_PAGE_CODEC_TEST
  bool serialize(HalFile& file) override;
#endif
  PageElementTag getTag() const override { return TAG_PageImage; }
  const ImageBlock& getImageBlock() const { return *imageBlock; }
};

class PageHorizontalRule final : public PageElement {
  uint16_t width;
  uint8_t thickness;

 public:
  PageHorizontalRule(uint16_t width, uint8_t thickness, const int16_t xPos, const int16_t yPos)
      : PageElement(xPos, yPos), width(width), thickness(thickness) {}

  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
#ifdef XP_PAGE_CODEC_TEST
  bool serialize(HalFile& file) override;
#endif
  PageElementTag getTag() const override { return TAG_PageHorizontalRule; }
};

// An initial letter spanning several lines, drawn scaled up from a font's glyph.
// yPos is the baseline of the last spanned line.
class PageDropCap final : public PageElement {
 public:
  static constexpr size_t MAX_TEXT_BYTES = 12;  // leading punctuation plus the letter

 private:
  int32_t fontId;
  uint16_t scale256;
  EpdFontFamily::Style style;
  char text[MAX_TEXT_BYTES + 1] = {};

 public:
  PageDropCap(int32_t fontId, uint16_t scale256, EpdFontFamily::Style style, const char* utf8, int16_t xPos,
              int16_t yPos);
  const char* getText() const { return text; }
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
#ifdef XP_PAGE_CODEC_TEST
  bool serialize(HalFile& file) override;
#endif
  PageElementTag getTag() const override { return TAG_PageDropCap; }
};

// A CSS border and/or background shade around a block element's slice on this page.
// Sides split off by a page break have zero width.
class PageBorderBox final : public PageElement {
  uint16_t width;
  uint16_t height;
  CssBorderSide sides[4];  // top, right, bottom, left
  bool shaded;

 public:
  PageBorderBox(uint16_t width, uint16_t height, const CssBorderSide (&sides)[4], bool shaded, int16_t xPos,
                int16_t yPos)
      : PageElement(xPos, yPos),
        width(width),
        height(height),
        sides{sides[0], sides[1], sides[2], sides[3]},
        shaded(shaded) {}
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) override;
#ifdef XP_PAGE_CODEC_TEST
  bool serialize(HalFile& file) override;
#endif
  PageElementTag getTag() const override { return TAG_PageBorderBox; }
  void renderBackground(GfxRenderer& renderer, int xOffset, int yOffset) const;
};

class Page;

// Value views borrow the Page. Word/path spans remain valid until it is destroyed.
class PageElementView {
  const Page* page;
  uint16_t index;
  PageElementTag tag;
  XpPageElement fields() const;

 public:
  int16_t xPos;
  int16_t yPos;
  PageElementView(const Page& page, uint16_t index);
  PageElementTag getTag() const { return tag; }
  TextBlock textBlock() const;
  ImageBlock imageBlock() const;
  std::string_view dropCapText() const;
  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) const;
  void renderBackground(GfxRenderer& renderer, int xOffset, int yOffset) const;
  void renderPlaceholder(GfxRenderer& renderer, int xOffset, int yOffset) const;
#ifdef XP_PAGE_CODEC_TEST
  bool serialize(HalFile& file) const;
#endif
};

class Page {
  friend class PageElementView;
  uint8_t* arena = nullptr;
  size_t arenaSize = 0;
  XpPageInfo info{};
  struct ElementData {
    XpPagePosition position;
    union {
      TextBlock::ViewData line;
      struct {
        XpPageSpan path, source;
        int16_t width, height;
      } image;
      struct {
        uint16_t width;
        uint8_t thickness;
      } rule;
      struct {
        XpPageSpan text;
        int32_t font;
        uint16_t scale;
        uint8_t style;
      } cap;
      struct {
        uint16_t width, height;
        uint8_t sides[8], shaded;
      } border;
    };
    ElementData() : position{}, line{} {}
  };
  ElementData* views = nullptr;
  static std::unique_ptr<Page> allocate(const XpPageInfo& info);
  bool resolveViews();
  std::vector<FootnoteEntry> footnotes;
  std::vector<PageLink> links;

 public:
  Page() = default;
  Page(const Page&) = delete;
  Page& operator=(const Page&) = delete;
  Page(Page&&) = delete;
  Page& operator=(Page&&) = delete;
  // The header and trailing arena share the allocation owned by unique_ptr<Page>.
  static void operator delete(void* p) noexcept { ::operator delete(p); }
  size_t elementCount() const { return arena ? info.elements : elements.size(); }
  size_t footnoteCount() const { return arena ? info.footnotes : footnotes.size(); }
  size_t linkCount() const { return arena ? info.links : links.size(); }
  PageElementView element(uint16_t index) const { return PageElementView(*this, index); }
  void footnote(uint16_t index, FootnoteEntry& out) const;
  void link(uint16_t index, PageLink& out) const;
  static std::unique_ptr<Page> load(HalFile& file, size_t bytes, uint32_t visibleOffset = 0);
  // Copies validated bytes into the owned arena; does not retain the input span.
  static std::unique_ptr<Page> fromBytes(const uint8_t* bytes, size_t size, uint32_t visibleOffset = 0);

  // the list of block index and line numbers on this page
  std::vector<std::unique_ptr<PageElement>> elements;
  static constexpr uint16_t MAX_FOOTNOTES_PER_PAGE = 16;
  static constexpr uint16_t MAX_LINKS_PER_PAGE = 32;

  // Zero-based visible-codepoint offset where this page starts. Not part of the serialized page
  // body (it lives in the section's visible-offset LUT); Section::loadPage* fills it in from the
  // build LUT or the on-disk LUT while the page file is already open, so the reader can persist
  // progress without a second section-file open per page turn.
  uint32_t visibleTextOffset = 0;

  void addFootnote(const char* number, const char* href) {
    if (footnotes.size() >= MAX_FOOTNOTES_PER_PAGE) return;  // Cap per-page footnotes
    FootnoteEntry entry;
    strncpy(entry.number, number, sizeof(entry.number) - 1);
    entry.number[sizeof(entry.number) - 1] = '\0';
    strncpy(entry.href, href, sizeof(entry.href) - 1);
    entry.href[sizeof(entry.href) - 1] = '\0';
    footnotes.push_back(entry);
  }

  bool addLink(const char* href, int16_t x, int16_t y, int16_t width, int16_t height) {
    if (!href || width <= 0 || height <= 0 || links.size() >= MAX_LINKS_PER_PAGE) {
      return false;
    }
    const size_t hrefLen = strnlen(href, sizeof(PageLink::href));
    if (hrefLen == 0 || hrefLen == sizeof(PageLink::href)) {
      return false;
    }
    links.emplace_back();
    auto& link = links.back();
    memcpy(link.href, href, hrefLen + 1);
    link.x = x;
    link.y = y;
    link.width = width;
    link.height = height;
    return true;
  }

  void render(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) const;
  void renderImages(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) const;
  void renderWithImagePlaceholders(GfxRenderer& renderer, int fontId, int xOffset, int yOffset) const;
#ifdef XP_PAGE_CODEC_TEST
  bool serialize(HalFile& file) const;
#endif

  bool hasImages() const;
  bool hasImagesNeedingDecode() const;
  bool getImageBoundingBox(int16_t& outX, int16_t& outY, int16_t& outW, int16_t& outH) const;
};

// Reader-owned copies outlive the rendered Page; failed assignment leaves no stale targets.
class PageMetadata {
  std::unique_ptr<FootnoteEntry[]> ownedFootnotes;
  std::unique_ptr<PageLink[]> ownedLinks;
  uint16_t footnoteSize = 0;
  uint16_t linkSize = 0;

 public:
  std::span<const FootnoteEntry> footnotes() const { return {ownedFootnotes.get(), footnoteSize}; }
  std::span<const PageLink> links() const { return {ownedLinks.get(), linkSize}; }
  void clear();
  bool assign(const Page& page);
};
