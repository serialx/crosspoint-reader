#include <xp_epub_session.h>
#include <xp_ui_reader.h>

#include <cassert>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <type_traits>

#include "epub_boundary_assertions.h"

static_assert(std::is_same_v<XpAppResultSink, void (*)(void*, const XpAppResultView*)>);
static_assert(std::is_same_v<decltype(XpAppPlatform::epub), const XpEpubLeaves*>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::strip_release_idle), uint8_t (*)(void*)>);
static_assert(
    std::is_same_v<decltype(XpEpubLeaves::popup_show), void (*)(void*, XpAppText, XpAppText, uint32_t, int32_t,
                                                                uint8_t (*)(void*, uint32_t, XpAppTextSink, void*),
                                                                void*, void (*)(void*, int32_t), void*)>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::settings_snapshot), void (*)(uint8_t, XpEpubSettings*)>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::section_estimated_pages), int32_t (*)(void*)>);
static_assert(
    std::is_same_v<decltype(XpEpubLeaves::section_toc_for_page), int32_t (*)(void*, void*, int32_t, int32_t)>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::bookmarks_load), uint8_t (*)(void*, void*)>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::bookmarks_save), uint8_t (*)(void*, void*)>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::clippings_load), uint8_t (*)(void*)>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::bookmarks_retain),
                             uint32_t (*)(void*, uint8_t (*)(void*, const XpEpubBookmark*), void*)>);
static_assert(
    std::is_same_v<decltype(XpEpubLeaves::child_selection), void* (*)(void**, uint8_t, const XpEpubSelectionArgs*)>);
static_assert(std::is_standard_layout_v<XpAppResultView>);
static_assert(std::is_standard_layout_v<XpEpubLeaves>);
static_assert(std::is_standard_layout_v<XpEpubGeometry>);
static_assert(sizeof(XpEpubGeometry) == 32 && alignof(XpEpubGeometry) == 4);
static_assert(std::is_same_v<decltype(XpEpubGeometry::ui_font_12), int32_t>);
static_assert(offsetof(XpEpubGeometry, status_height) == 8);
static_assert(offsetof(XpEpubGeometry, status_margin) == 16);
static_assert(offsetof(XpEpubGeometry, ui_font_12) == 20);
static_assert(offsetof(XpEpubGeometry, title_mode) == 24);
static_assert(offsetof(XpEpubGeometry, strip_enabled) == 30);
static_assert(offsetof(XpEpubGeometry, has_font_cache) == 31);
static_assert(std::is_same_v<decltype(XpEpubGeometry::has_font_cache), uint8_t>);
static_assert(std::is_standard_layout_v<XpEpubToolbarRow>);
static_assert(std::is_same_v<decltype(XpEpubToolbarRow::label_spaces), uint32_t>);
static_assert(sizeof(XpEpubToolbarRow) == 2 * sizeof(XpAppText) + 8);
static_assert(offsetof(XpEpubToolbarRow, label_spaces) == 2 * sizeof(XpAppText));
static_assert(offsetof(XpEpubToolbarRow, toggle) == 2 * sizeof(XpAppText) + 4);
static_assert(offsetof(XpEpubToolbarRow, checked) == 2 * sizeof(XpAppText) + 5);

static_assert(std::is_same_v<decltype(XpEpubLeaves::radio_status), void (*)(XpEpubRadio*, XpEpubPeer*)>);
static_assert(sizeof(XpEpubRadio) == 5 && alignof(XpEpubRadio) == 1);
static_assert(offsetof(XpEpubRadio, note) == 4);
static_assert(sizeof(XpEpubPeer) == 82 && alignof(XpEpubPeer) == 1);
static_assert(offsetof(XpEpubPeer, name) == 0);
static_assert(offsetof(XpEpubPeer, addr) == 64);
static_assert(offsetof(XpEpubLeaves, radio_status) == 114 * sizeof(void (*)(void)));

static_assert(std::is_standard_layout_v<XpEpubSettings>);
static_assert(offsetof(XpEpubSettings, orientation) == 4);
static_assert(offsetof(XpEpubSettings, dictionary_available) == 26);
static_assert(offsetof(XpEpubSettings, font_family) == (sizeof(void*) == 8 ? 32 : 28));
static_assert(sizeof(XpEpubSettings) == (sizeof(void*) == 8 ? 48 : 36));

static_assert(std::is_same_v<decltype(XpEpubLeaves::toolbar_reset_navigation), void (*)(void*, int32_t, int32_t)>);
static_assert(offsetof(XpEpubLeaves, toolbar_reset_navigation) == 197 * sizeof(void (*)(void)));
static_assert(std::is_same_v<decltype(XpEpubLeaves::input_snapshot), void (*)(XpUiInput*)>);
static_assert(offsetof(XpEpubLeaves, input_snapshot) == 198 * sizeof(void (*)(void)));
static_assert(std::is_same_v<decltype(XpEpubLeaves::page_render), void (*)(void*, int32_t, int32_t, int32_t)>);
static_assert(offsetof(XpEpubLeaves, page_render) == 199 * sizeof(void (*)(void)));
static_assert(sizeof(XpEpubLeaves) == 210 * sizeof(void (*)(void)));
static_assert(std::is_same_v<decltype(XpEpubLeaves::log_enabled), uint8_t (*)(uint8_t)>);
static_assert(offsetof(XpEpubLeaves, log_enabled) == 196 * sizeof(void (*)(void)));
static_assert(XP_UI_READER_RENDER_NOW == 0);
static_assert(XP_UI_READER_RENDER_HAS_IMAGES == 1);
static_assert(XP_UI_READER_RENDER_IMAGES_NEED_DECODE == 2);
static_assert(XP_UI_READER_RENDER_TAKE_FORCED == 3);
static_assert(XP_UI_READER_RENDER_COUNTER == 4);
static_assert(XP_UI_READER_RENDER_FREQUENCY == 5);
static_assert(XP_UI_READER_RENDER_TEXT_AA == 6);
static_assert(XP_UI_READER_RENDER_IS_A4 == 7);
static_assert(XP_UI_READER_RENDER_IS_X3 == 8);
static_assert(XP_UI_READER_RENDER_IS_UC8279 == 9);
static_assert(XP_UI_READER_RENDER_ABSOLUTE_SUPPORTED == 10);
static_assert(XP_UI_READER_RENDER_OVERLAY_CAPS == 11);
static_assert(XP_UI_READER_RENDER_ABSOLUTE_CAPS == 12);
static_assert(XP_UI_READER_RENDER_STRIP_BOARD == 13);
static_assert(XP_UI_READER_RENDER_ELEMENTS == 14);
static_assert(XP_UI_READER_RENDER_WIDTH_BYTES == 15);
static_assert(XP_UI_READER_RENDER_HEIGHT == 16);
static_assert(XP_UI_READER_RENDER_FREE_HEAP == 17);
static_assert(XP_UI_READER_RENDER_MAX_BLOCK == 18);
static_assert(XP_UI_READER_RENDER_IS_BW == 19);

static_assert(std::is_same_v<decltype(XpEpubLeaves::page_element), void (*)(void*, uint16_t, XpEpubElement*)>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::page_word), void (*)(void*, uint16_t, uint16_t, XpEpubWord*)>);
static_assert(
    std::is_same_v<decltype(XpEpubLeaves::end_input), void (*)(void*, void (*)(void*, uint8_t, XpAppText), void*)>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::recents_add), void (*)(void*)>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::page_word_font), int32_t (*)(void*, uint16_t, uint16_t, int32_t)>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::page_line_font), int32_t (*)(void*, uint16_t, int32_t)>);
static_assert(
    std::is_same_v<decltype(XpEpubLeaves::page_word_y_offset), int32_t (*)(void*, uint16_t, uint16_t, int32_t)>);
static_assert(std::is_same_v<decltype(XpEpubLeaves::page_line_ruby_shift), int32_t (*)(void*, uint16_t, int32_t)>);
static_assert(
    std::is_same_v<decltype(XpEpubLeaves::font_text_advance), int32_t (*)(XpAppText, int32_t, uint8_t, int8_t)>);
static_assert(sizeof(XpEpubElement) == 12 && alignof(XpEpubElement) == 4);
static_assert(sizeof(XpEpubWord) == sizeof(XpAppText) + 16);
static_assert(std::is_standard_layout_v<XpEpubInputFacts>);
static_assert(sizeof(XpEpubInputFacts) == 16 && alignof(XpEpubInputFacts) == 4);
static_assert(offsetof(XpEpubInputFacts, touch) == 10);
static_assert(offsetof(XpEpubInputFacts, board_touch) == 11);
static_assert(offsetof(XpEpubInputFacts, home_action) == 12);
static_assert(offsetof(XpEpubInputFacts, slide_owns_light) == 13);
static_assert(std::is_standard_layout_v<XpEpubPopupInput>);
static_assert(sizeof(XpEpubPopupInput) == 3 && alignof(XpEpubPopupInput) == 1);
static_assert(offsetof(XpEpubPopupInput, active) == 0);
static_assert(offsetof(XpEpubPopupInput, changed) == 1);
static_assert(offsetof(XpEpubPopupInput, consumed) == 2);

struct ProgressIoFixture {
  std::string trace;
  bool open = true;
  bool rename = true;
  uint32_t limit = UINT32_MAX;
};

static void progressStatuses() {
  const uint8_t bytes[] = {1, 2, 3, 4};
  for (int scenario = 0; scenario < 5; ++scenario) {
    ProgressIoFixture f;
    f.open = scenario != 1;
    f.limit = scenario == 2 ? 3 : UINT32_MAX;
    f.rename = scenario != 3;
    XpUiReaderProgressIo io{&f,
                            [](void* p) -> uint8_t {
                              auto& f = *static_cast<ProgressIoFixture*>(p);
                              f.trace += "open;";
                              return f.open;
                            },
                            [](void* p, const uint8_t*, uint32_t n) -> uint32_t {
                              auto& f = *static_cast<ProgressIoFixture*>(p);
                              f.trace += "write;";
                              return n < f.limit ? n : f.limit;
                            },
                            [](void* p) { static_cast<ProgressIoFixture*>(p)->trace += "flush;"; },
                            [](void* p) { static_cast<ProgressIoFixture*>(p)->trace += "close;"; },
                            [](void* p) { static_cast<ProgressIoFixture*>(p)->trace += "remove;"; },
                            [](void* p) -> uint8_t {
                              auto& f = *static_cast<ProgressIoFixture*>(p);
                              f.trace += "rename;";
                              return f.rename;
                            }};
    if (scenario == 4) io.close_temp = nullptr;
    uint32_t written = 99;
    const uint8_t result = xp_ui_reader_write_progress(&io, bytes, 4, &written);
    assert(result == scenario);
    assert(written == (scenario == 1 || scenario == 4 ? 0u : scenario == 2 ? 3u : 4u));
    assert(f.trace == (scenario == 1   ? "open;"
                       : scenario == 2 ? "open;write;"
                       : scenario == 4 ? ""
                                       : "open;write;flush;close;remove;rename;"));
  }
  uint32_t written = 99;
  assert(xp_ui_reader_write_progress(nullptr, bytes, 4, &written) == XP_UI_READER_PROGRESS_INVALID);
  assert(written == 0);
}

static void links() {
  XpUiReaderLinkPosition positions[3] = {{1, 0x1234}, {2, 0xabcd}, {0, 65535}};
  uint8_t bytes[13]{};
  const uint8_t expected[13] = {3, 1, 0, 0x34, 0x12, 2, 0, 0xcd, 0xab, 0, 0, 0xff, 0xff};
  assert(xp_ui_reader_links_encode(positions, 3, bytes) == 13);
  assert(std::memcmp(bytes, expected, 13) == 0);
  XpUiReaderLinkPosition decoded[3]{};
  assert(xp_ui_reader_links_decode(bytes, 13, 3, decoded) == 3);
  assert(std::memcmp(decoded, positions, sizeof(positions)) == 0);
  bytes[9] = 3;
  assert(xp_ui_reader_links_decode(bytes, 13, 3, decoded) == 0);
  assert(decoded[2].spine == 3);
  assert(decoded[1].page == 0xabcd);
  const auto last = decoded[2];
  assert(xp_ui_reader_links_decode(bytes, 12, 3, decoded) == 0);
  assert(decoded[2].spine == last.spine && decoded[2].page == last.page);
}

static void inputPointers() {
  int calls = 0;
  const XpUiReaderInputConfig config{};
  const XpUiReaderInputServices complete{&calls,
                                         [](void* p, uint32_t, uint8_t, uint32_t) -> uint8_t {
                                           ++*static_cast<int*>(p);
                                           return 0;
                                         },
                                         [](void* p, uint8_t) -> uint32_t {
                                           ++*static_cast<int*>(p);
                                           return 0;
                                         },
                                         [](void* p, int32_t*, int32_t*) -> uint8_t {
                                           ++*static_cast<int*>(p);
                                           return 0;
                                         }};
  const XpUiReaderTurnInput empty{};
  const auto noServices = xp_ui_reader_buttons(nullptr, &config);
  const auto noConfig = xp_ui_reader_buttons(&complete, nullptr);
  assert(std::memcmp(&noServices, &empty, sizeof(empty)) == 0);
  assert(std::memcmp(&noConfig, &empty, sizeof(empty)) == 0);
  assert(!xp_ui_reader_menu_gesture(nullptr, 1, 90, 120));
  assert(!xp_ui_reader_back(nullptr, 0));
  for (int missing = 0; missing < 3; ++missing) {
    auto services = complete;
    if (missing == 0) services.button = nullptr;
    if (missing == 1) services.read = nullptr;
    if (missing == 2) services.tap = nullptr;
    const auto buttons = xp_ui_reader_buttons(&services, &config);
    const auto touch = xp_ui_reader_touch(&services, &config, 90, 120, 0);
    assert(std::memcmp(&buttons, &empty, sizeof(empty)) == 0);
    assert(std::memcmp(&touch, &empty, sizeof(empty)) == 0);
    assert(!xp_ui_reader_menu_gesture(&services, 1, 90, 120));
    assert(!xp_ui_reader_back(&services, 0));
  }
  assert(calls == 0);
  xp_ui_reader_toolbar_nav_reset(nullptr);
  assert(!xp_ui_reader_toolbar_page_rows(nullptr));
  assert(!xp_ui_reader_toolbar_scroll(nullptr, 1, 10));
}

static void basePointers() {
  uint32_t calls = 0;
  const XpUiReaderBaseServices complete{&calls,
                                        [](void* p, uint8_t) -> uint32_t {
                                          ++*static_cast<uint32_t*>(p);
                                          return 0;
                                        },
                                        [](void* p, uint8_t, uint32_t) { ++*static_cast<uint32_t*>(p); },
                                        [](void* p, uint8_t) -> uint32_t {
                                          ++*static_cast<uint32_t*>(p);
                                          return 0;
                                        },
                                        [](void* p, uint8_t, int32_t, int32_t) -> uint32_t {
                                          ++*static_cast<uint32_t*>(p);
                                          return 0;
                                        },
                                        [](void* p) -> XpUiReaderTurnInput {
                                          ++*static_cast<uint32_t*>(p);
                                          return {};
                                        },
                                        [](void* p) -> XpUiReaderTurnInput {
                                          ++*static_cast<uint32_t*>(p);
                                          return {};
                                        }};
  assert(!xp_ui_reader_base_run(nullptr, XP_UI_READER_BASE_OPERATION_ENTER, 0, 0));
  assert(!xp_ui_reader_base_run(&complete, 255, 0, 0));
  for (int missing = 0; missing < 6; ++missing) {
    auto services = complete;
    if (missing == 0) services.read = nullptr;
    if (missing == 1) services.write = nullptr;
    if (missing == 2) services.query = nullptr;
    if (missing == 3) services.command = nullptr;
    if (missing == 4) services.touch = nullptr;
    if (missing == 5) services.buttons = nullptr;
    for (uint8_t operation = 0; operation <= XP_UI_READER_BASE_OPERATION_MENU_ACTIVE; ++operation)
      assert(!xp_ui_reader_base_run(&services, operation, 0, 0));
  }
  assert(calls == 0);
}

int main() {
  basePointers();
  inputPointers();
  progressStatuses();
  links();
  XpUiReaderSession session{};
  xp_ui_reader_session_note(nullptr, 1, 1);
  xp_ui_reader_session_render(nullptr, 0, 0, 0);
  assert(!xp_ui_reader_session_worthy(nullptr));
  assert(!xp_ui_reader_progress_encode(0, 0, 0, 0, 0, nullptr));
  XpUiReaderProgress out{1, 2, 3, 1, 1, 4};
  const XpUiReaderProgress before = out;
  const uint8_t bytes[10] = {0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0xef, 0xcd, 0xab, 0x89};
  for (uint32_t size : {0u, 1u, 2u, 3u, 5u, 7u, 8u, 9u, 11u, UINT32_MAX}) {
    assert(!xp_ui_reader_progress_decode(bytes, size, &out));
    assert(std::memcmp(&out, &before, sizeof(out)) == 0);
  }
  assert(!xp_ui_reader_progress_decode(nullptr, 10, &out));
  assert(!xp_ui_reader_progress_decode(bytes, 10, nullptr));
  assert(xp_ui_reader_progress_decode(bytes, 10, &out) == 1);
  uint8_t encoded[10]{};
  assert(xp_ui_reader_progress_encode(out.spine, out.page, out.count, out.has_offset, out.offset, encoded) == 10);
  assert(std::memcmp(bytes, encoded, 10) == 0);
  const auto saved = out;
  assert(xp_ui_reader_progress_decode(bytes, 4, &out) == 1);
  assert(out.spine == saved.spine && out.page == saved.page && !out.has_count && !out.has_offset);
  assert(!xp_ui_reader_refresh(nullptr, 3));
  xp_ui_reader_session_render(&session, 0, 12345, 50);
  xp_ui_reader_session_note(&session, 1, 1);
  xp_ui_reader_session_render(&session, 10000, 12355, 100);
  assert(xp_ui_reader_session_worthy(&session));
}
