#pragma once
#include <Epub/Page.h>
#include <xp_epub_session.h>

extern "C" uint8_t xp_epub_test_section_fixture_page(void*, int32_t, XpEpubOwnedPage*);

// A fixture-only facade lets existing native observations hold a real Rust Page.
inline std::unique_ptr<Page> fixtureSectionPage(void* section, int32_t index) {
  XpEpubOwnedPage source{};
  if (!xp_epub_test_section_fixture_page(section, index, &source)) return {};
  auto page = std::make_unique<Page>(Page::Borrowed{}, source.view.arena, source.view.size, source.view.visible_offset);
  page->adopt(source.owner, source.destroy);
  return page;
}
