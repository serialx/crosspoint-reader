#pragma once

#include <GfxRenderer.h>
#include <Memory.h>
#include <xp_epub_session.h>

#include "ClipSelectionActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderFootnoteSelectActivity.h"
#include "activities/OwnedLegacyActivity.h"

namespace epubPage {

inline std::unique_ptr<Page> facade(const XpEpubOwnedPage& source) {
  auto page =
      makeUniqueNoThrow<Page>(Page::Borrowed{}, source.view.arena, source.view.size, source.view.visible_offset);
  if (!page) LOG_ERR("PGE", "OOM: Page facade %u bytes", static_cast<unsigned>(sizeof(Page)));
  return page;
}

inline void adopt(Page& page, XpEpubOwnedPage& source) {
  page.adopt(source.owner, source.destroy);
  source.owner = nullptr;
}

inline void* footnote(GfxRenderer& renderer, MappedInputManager& input, XpEpubOwnedPage& source, int32_t left,
                      int32_t top) {
  auto page = facade(source);
  if (!page) return nullptr;
  auto* borrowed = page.get();
  auto child = makeLegacyActivity<EpubReaderFootnoteSelectActivity>(renderer, input, std::move(page), left, top);
  if (!child) return nullptr;
  adopt(*borrowed, source);
  return child.release();
}

inline void* dictionary(GfxRenderer& renderer, MappedInputManager& input, XpEpubOwnedPage& source, int32_t left,
                        int32_t top, XpAppText initial) {
  auto page = facade(source);
  if (!page) return nullptr;
  auto* borrowed = page.get();
  std::string text = initial.len ? std::string(reinterpret_cast<const char*>(initial.data), initial.len) : "";
  auto child =
      makeLegacyActivity<DictionaryWordSelectActivity>(renderer, input, std::move(page), left, top, std::move(text));
  if (!child) return nullptr;
  adopt(*borrowed, source);
  return child.release();
}

inline void* selection(GfxRenderer& renderer, MappedInputManager& input, XpEpubOwnedPage* sources, uint8_t count,
                       const XpEpubSelectionArgs& args) {
  constexpr size_t pageWindow = 3;
  assert(count > 0 && count <= pageWindow);
  std::vector<std::unique_ptr<Page>> pages;
  pages.reserve(pageWindow);
  Page* borrowed[pageWindow]{};
  for (uint8_t i = 0; i < count; ++i) {
    auto page = facade(sources[i]);
    if (!page) return nullptr;
    borrowed[i] = page.get();
    pages.push_back(std::move(page));
  }
  auto child = makeLegacyActivity<ClipSelectionActivity>(renderer, input, std::move(pages), args.left, args.top, args.x,
                                                         args.y, static_cast<ClipSelectionActivity::Mode>(args.mode));
  if (!child) return nullptr;
  // No allocation or callback may interrupt this all-or-nothing commit.
  for (uint8_t i = 0; i < count; ++i) adopt(*borrowed[i], sources[i]);
  return child.release();
}

inline void paint(GfxRenderer& renderer, const XpEpubPageView& view, int32_t font, int32_t left, int32_t top,
                  uint8_t mode) {
  Page page(Page::Borrowed{}, view.arena, view.size, view.visible_offset);
  switch (static_cast<PageRenderMode>(mode)) {
    case PageRenderMode::All:
      page.render(renderer, font, left, top);
      break;
    case PageRenderMode::Images:
      page.renderImages(renderer, font, left, top);
      break;
    case PageRenderMode::Placeholders:
      page.renderWithImagePlaceholders(renderer, font, left, top);
      break;
  }
}

inline uint8_t imageNeedsDecode(XpAppText path, XpAppText source, int16_t width, int16_t height) {
  const std::string_view imagePath{reinterpret_cast<const char*>(path.data), path.len};
  const std::string_view sourcePath{reinterpret_cast<const char*>(source.data), source.len};
  return ImageBlock(ImageBlock::View{}, imagePath, sourcePath, width, height).needsDecode();
}

inline int32_t fontAscent(GfxRenderer& renderer, int32_t font) { return renderer.getFontAscenderSize(font); }

}  // namespace epubPage
