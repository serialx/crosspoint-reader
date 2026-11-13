#pragma once

#include <GfxRenderer.h>
#include <Memory.h>
#include <xp_epub_session.h>

#include "ClipSelectionActivity.h"
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

inline int32_t fontAscent(GfxRenderer& renderer, int32_t font) { return renderer.getFontAscenderSize(font); }

}  // namespace epubPage
