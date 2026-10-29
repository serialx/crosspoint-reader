#include <xp_ui_reader.h>

#include <cassert>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>

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

int main() {
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
  XpUiReaderBuildWait services{};
  assert(!xp_ui_reader_wait_build(nullptr, 5000, 50));
  assert(!xp_ui_reader_wait_build(&services, 5000, 50));
  assert(!xp_ui_reader_refresh(nullptr, 3));
  assert(!xp_ui_reader_link_draw(nullptr, 1, 1));
  assert(!xp_ui_reader_link_repaint(nullptr, 1));
  xp_ui_reader_session_render(&session, 0, 12345, 50);
  xp_ui_reader_session_note(&session, 1, 1);
  xp_ui_reader_session_render(&session, 10000, 12355, 100);
  assert(xp_ui_reader_session_worthy(&session));
}
