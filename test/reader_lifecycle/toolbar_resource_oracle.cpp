#include <Memory.h>
#include <xp_epub_session.h>
#include <xp_ui_reader.h>
#include <xp_ui_reader_chrome.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <string>

static bool countAllocations = false;
static size_t allocationAttempts = 0;
void* operator new(size_t size) {
  if (countAllocations) ++allocationAttempts;
  if (void* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, size_t) noexcept { std::free(value); }
void operator delete[](void* value, size_t) noexcept { std::free(value); }
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  try {
    return ::operator new[](size);
  } catch (...) {
    return nullptr;
  }
}
void operator delete(void* value, const std::nothrow_t&) noexcept { std::free(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { std::free(value); }

static int criticalDepth = 0;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p)     \
  do {                            \
    (void)(p);                    \
    assert(criticalDepth++ == 0); \
  } while (0)
#define taskEXIT_CRITICAL(p)      \
  do {                            \
    (void)(p);                    \
    assert(--criticalDepth == 0); \
  } while (0)
#define LOG_ERR(...) ((void)0)

struct EpdFontFamily {
  enum Style { Regular, Bold };
};
struct TouchSnapshot {
  int16_t touchX = -1, touchY = -1;
  bool touchPressed = false, touchReleased = false, touchHeld = false;
};
class MappedInputManager {
 public:
  TouchSnapshot touch;
};
static TouchSnapshot touchSnapshotFrom(const MappedInputManager& input) {
  assert(criticalDepth == 0);
  return input.touch;
}
static int haptics = 0;
namespace haptic_feedback {
static void touchAction(bool strong) {
  assert(!strong && criticalDepth == 0);
  ++haptics;
}
}  // namespace haptic_feedback

// These fakes supply physical metrics and consume the production scene synchronously.
namespace freeink::ui {
struct Rect {
  int16_t x, y, width, height;
};
struct TextStyle {
  int font = 0;
  bool bold = false;
  uint8_t maxLines = 1;
};
struct BitmapRef {};
enum class BitmapMode { Center };
static BitmapRef bitmapFromIcon(int) { return {}; }
struct Device {
  Rect safeRect() const { return {0, 0, 528, 792}; }
};
struct GfxRendererTarget {
  void setFont(int, int) { assert(criticalDepth == 0); }
  Device deviceContext() const { return {}; }
  void bitmap(Rect, BitmapRef, BitmapMode) { assert(criticalDepth == 0); }
};
struct Measured {
  int height;
};
static Measured measureWrappedText(GfxRendererTarget&, const char* value, TextStyle, int16_t width) {
  assert(criticalDepth == 0 && value && width > 0);
  return {24};
}
}  // namespace freeink::ui
namespace fui = freeink::ui;
static constexpr int icon_reader_contents_24 = 0, icon_reader_text_24 = 1, icon_reader_more_24 = 2,
                     icon_reader_back_24 = 3, icon_reader_next_24 = 4;
class GfxRenderer {
 public:
  bool paintSucceeds = true;
  bool longRows = false;
  int longTitles = 0;
  int scenes = 0, texts = 0;
  std::array<int, 4> clip{0, 0, 528, 792};
  int getTextWidth(int, const char* text, EpdFontFamily::Style) const {
    assert(criticalDepth == 0 && text);
    return static_cast<int>(std::strlen(text)) * 8;
  }
  int getLineHeight(int) const {
    assert(criticalDepth == 0);
    return 24;
  }
  void drawUiText(const XpUiCommand& command, XpUiRect, const char* text) {
    assert(criticalDepth == 0 && text);
    ++texts;
    if (command.resource > 0 && command.resource < XP_UI_READER_HEADER) {
      const int index = (command.resource - 1) / 2;
      const bool label = (command.resource - 1) % 2 == 0;
      const size_t prefix = label ? (longRows ? 6 : index % 5) : 0;
      const size_t length = longRows ? (label ? 4096 + prefix : 0) : (label ? 64 + prefix : 32);
      const char expected = longRows ? 'x' : static_cast<char>((label ? 'A' : 'a') + index % 26);
      assert(std::strlen(text) == length && "temporary source bytes must be copied before painting");
      for (size_t i = 0; i < length; ++i) assert(text[i] == (i < prefix ? ' ' : expected));
    } else if (command.resource == XP_UI_READER_TITLE && std::strlen(text) == 4096) {
      ++longTitles;
      for (size_t i = 0; i < 4096; ++i) assert(text[i] == 'x');
    }
  }
  std::array<int, 4> getClipRect() const { return clip; }
  void setClipRect(int x, int y, int w, int h) { clip = {x, y, w, h}; }
  bool drawUiScene(const XpUiScene& scene, const XpUiPaint* paint) {
    assert(criticalDepth == 0 && paint && scene.command_count > 0);
    ++scenes;
    for (uint16_t i = 0; i < scene.command_count; ++i) {
      const auto& command = scene.commands[i];
      if (command.kind == XP_UI_TEXT_HANDLE || command.kind == XP_UI_BITMAP) {
        assert(paint->resource_valid(paint->context, command.resource, command.generation));
        paint->resource(paint->context, nullptr, &command, {0, 0, 528, 792});
      }
    }
    return paintSucceeds;
  }
};
static fui::GfxRendererTarget makeUiTarget(GfxRenderer&) { return {}; }
struct UITheme {
  struct Metrics {
    int16_t listRowHeight = 48;
  };
  static UITheme& getInstance() {
    static UITheme instance;
    return instance;
  }
  Metrics getMetrics() const { return {}; }
};
static void sampleReaderChromeTheme(const GfxRenderer&, XpUiReaderTheme& theme) {
  assert(criticalDepth == 0);
  theme = {};
  theme.screen = theme.safe = {0, 0, 528, 792};
  theme.body_font = 1;
  theme.title_font = 2;
  theme.row_min = 48;
  theme.row_padding = 8;
  theme.scroll_width = 3;
  theme.space_sm = 4;
  theme.space_md = 8;
  theme.space_lg = 16;
  theme.control_radius = theme.sheet_radius = 12;
}

// ACTUAL_COMMAND_CLASS
// ACTUAL_COMMAND_METHODS
// ACTUAL_TOOLBAR_CLASS
// ACTUAL_TOOLBAR_METHODS
// ACTUAL_PATH_TEXT

static GfxRenderer renderer;
static MappedInputManager input;
static GfxRenderer* target = &renderer;
static MappedInputManager* mapped = &input;
static const auto openToolbar = /* ACTUAL_OPEN_LEAF */;
static const auto closeToolbar = /* ACTUAL_CLOSE_LEAF */;
static const auto beginToolbar = /* ACTUAL_BEGIN_LEAF */;
static const auto paintToolbar = /* ACTUAL_PAINT_LEAF */;
static const auto routeToolbar = /* ACTUAL_ROUTE_LEAF */;
static const auto scrollToolbar = /* ACTUAL_SCROLL_LEAF */;
static const auto visibleRows = /* ACTUAL_VISIBLE_LEAF */;
static const auto resetToolbar = /* ACTUAL_RESET_LEAF */;

static void cleared(const ReaderToolbarUi& toolbar) {
  const auto& model = toolbar.model_;
  assert(!model.rowFill && !model.rowFillContext && "borrowed Model must be cleared after paint");
  assert(!model.chapterTitle && !model.pageInfo && !model.panelTitle);
}
struct Source {
  std::array<int, 64> indices{};
  int count = 0;
  int mode = 0;  // 1: failure after copy, 2: no copy, 3: failure before copy.
};
static uint8_t supply(void* raw, int32_t index, XpEpubToolbarRowSink sink, void* context) {
  assert(criticalDepth == 0);
  auto& source = *static_cast<Source*>(raw);
  assert(source.count < static_cast<int>(source.indices.size()));
  if (source.count) assert(index == source.indices[source.count - 1] + 1 && "rowFill at most once in visible order");
  source.indices[source.count++] = index;
  if (source.mode == 2) return true;
  if (source.mode == 3) return false;
  char label[64], value[32];
  std::memset(label, 'A' + index % 26, sizeof(label));
  std::memset(value, 'a' + index % 26, sizeof(value));
  const XpEpubToolbarRow row{{reinterpret_cast<const uint8_t*>(label), sizeof(label)},
                             {reinterpret_cast<const uint8_t*>(value), sizeof(value)},
                             static_cast<uint32_t>(index % 5),
                             static_cast<uint8_t>(index % 2),
                             static_cast<uint8_t>(index % 3 == 0)};
  assert(sink(context, &row));
  std::memset(label, '!', sizeof(label));
  std::memset(value, '?', sizeof(value));
  return source.mode != 1;
}
static XpAppText text(const char* value) {
  return {reinterpret_cast<const uint8_t*>(value), static_cast<uint32_t>(std::strlen(value))};
}
static void sameNav(const XpUiListState& a, const XpUiListState& b) {
  assert(a.selected == b.selected && a.top == b.top && a.visible == b.visible && a.drawn == b.drawn);
  assert(a.drawn_count == b.drawn_count && a.pending_scroll == b.pending_scroll && a.active == b.active);
  assert(a.last_repeat == b.last_repeat && a.follow == b.follow && a.navigation_revision == b.navigation_revision);
}
static void routeLikeRust(ReaderToolbarUi& toolbar, TouchSnapshot touch) {
  input.touch = touch;
  auto contact = toolbar.contact;
  const XpUiInput frame{
      0,
      0,
      0,
      0,
      0,
      touch.touchX,
      touch.touchY,
      static_cast<uint8_t>((touch.touchPressed ? XP_UI_TOUCH_PRESSED : 0) |
                           (touch.touchReleased ? XP_UI_TOUCH_RELEASED : 0) | (touch.touchHeld ? XP_UI_TOUCH_HELD : 0)),
      0,
      0,
      0,
      0};
  int32_t permille = -1;
  const auto expected = xp_ui_reader_toolbar_route(&contact, &toolbar.published, &frame, &permille);
  const int oldHaptics = haptics;
  XpEpubRouted actual{};
  routeToolbar(&toolbar, &actual);
  assert(actual.event == expected.kind && actual.value == expected.value && actual.routed == expected.invalidate);
  assert(actual.permille == permille && actual.x == touch.touchX && actual.y == touch.touchY);
  assert(haptics == oldHaptics + (expected.kind != 0 && touch.touchReleased));
  assert(std::memcmp(&contact, &toolbar.contact, sizeof(contact)) == 0);
}

static void directWindowContract(ReaderToolbarUi& toolbar) {
  int labels = 0, values = 0, flags = 0;
  ReaderToolbarUi::Model model;
  model.itemCount = 30;
  struct Rows {
    int* labels;
    int* values;
    int* flags;
  } rows{&labels, &values, &flags};
  model.rowFillContext = &rows;
  model.rowFill = [](void* raw, int index, std::string& label, std::string& value, ReaderToolbarUi::RowFlags& flags) {
    auto& rows = *static_cast<Rows*>(raw);
    assert(index == 7 + *rows.labels && "rowFill at most once in visible order");
    ++*rows.labels;
    ++*rows.values;
    ++*rows.flags;
    label = "label" + std::to_string(index);
    value = "value" + std::to_string(index);
    flags = {true, index % 2 != 0};
    return true;
  };
  toolbar.setModel(model);
  auto resources = toolbar.resources();
  assert(resources.window(resources.context, 7, 4));
  assert(labels == 4 && values == 4 && flags == 4);
  for (int i = 0; i < 4; ++i) {
    assert(toolbar.windowLabels_[i] == "label" + std::to_string(i + 7));
    assert(toolbar.windowValues_[i] == "value" + std::to_string(i + 7));
    XpUiReaderRow row{};
    assert(resources.row(resources.context, i + 7, &row));
    assert(row.label == static_cast<uint32_t>(2 * (i + 7) + 1) && row.value == row.label + 1);
    assert(row.toggle && row.checked == ((i + 7) % 2));
  }
  assert(!resources.window(resources.context, 0, 17));
  assert(!resources.window(resources.context, 29, 2));
  assert(labels == 4 && values == 4 && flags == 4);
  struct Direct {
    ReaderToolbarUi* toolbar;
    int next = 4, calls = 0;
  } direct{&toolbar};
  model.rowFillContext = &direct;
  model.rowFill = [](void* raw, int index, std::string& label, std::string& value, ReaderToolbarUi::RowFlags& flags) {
    auto& state = *static_cast<Direct*>(raw);
    assert(index == state.next++ && "rowFill at most once in visible order");
    const int slot = state.calls++;
    assert(&label == &state.toolbar->windowLabels_[slot] && &value == &state.toolbar->windowValues_[slot]);
    assert(&flags == &state.toolbar->rowFlags[slot]);
    label.assign("direct");
    value.clear();
    flags = {};
    return index != 6;
  };
  toolbar.setModel(model);
  resources = toolbar.resources();
  assert(!resources.window(resources.context, 4, 5));
  assert(direct.calls == 3 && labels == 4 && values == 4 && flags == 4);
  model = {};
  model.itemCount = 1;
  toolbar.setModel(model);
  resources = toolbar.resources();
  assert(!resources.window(resources.context, 0, 1) && "missing row source must reject the window");
  toolbar.setModel({});
}

int main() {
  auto* toolbar = static_cast<ReaderToolbarUi*>(openToolbar());
  assert(toolbar);
  directWindowContract(*toolbar);
  beginToolbar(toolbar);
  assert(!toolbar->ready);
  XpEpubRouted inactive{};
  routeToolbar(toolbar, &inactive);
  assert(inactive.event == 0 && !inactive.routed && inactive.permille == -1);
  assert(toolbar->commands.reserve(128));
  for (int i = 0; i < 16; ++i) {
    toolbar->windowLabels_[i].reserve(256);
    toolbar->windowValues_[i].reserve(256);
  }
  XpEpubToolbarModel model{text("Chapter"), text("12/40 30%"), text("Contents"), 300, 30, 9, 1, 1, 1};
  Source source;
  resetToolbar(toolbar, 9, 7);
  assert(toolbar->nav_.selected == 9 && toolbar->nav_.top == 7);
  allocationAttempts = 0;
  countAllocations = true;
  const bool painted = paintToolbar(toolbar, &model, supply, &source);
  countAllocations = false;
  assert(painted && toolbar->ready && renderer.scenes > 0 && renderer.texts > 0);
  assert(allocationAttempts == 0 && "preallocated native paint must not allocate on this host");
  cleared(*toolbar);
  assert(source.count > 0 && source.count <= 16 && source.indices[0] == toolbar->windowFirst);
  assert(source.count == toolbar->windowCount);
  for (int i = 0; i < source.count; ++i) {
    const int index = source.indices[i];
    assert(toolbar->windowLabels_[i] == std::string(index % 5, ' ') + std::string(64, 'A' + index % 26) &&
           "row label must preserve Rust indentation");
    assert(toolbar->windowValues_[i] == std::string(32, 'a' + index % 26));
    assert(toolbar->rowFlags[i].toggle == (index % 2 != 0));
    assert(toolbar->rowFlags[i].checked == (index % 3 == 0));
  }
  assert(toolbar->published.row_count > 0);
  const auto row = toolbar->published.first_row;
  routeLikeRust(*toolbar, {static_cast<int16_t>(row.x + 3), static_cast<int16_t>(row.y + 3), true, false, false});
  routeLikeRust(*toolbar, {static_cast<int16_t>(row.x + 3), static_cast<int16_t>(row.y + 3), false, true, false});
  assert(haptics > 0);
  auto expectedNav = toolbar->nav_;
  assert(visibleRows(toolbar) == xp_ui_reader_toolbar_page_rows(&expectedNav));
  for (int delta : {1, -1, 3, -8, 0}) {
    const bool expected = xp_ui_reader_toolbar_scroll(&expectedNav, delta, 30);
    assert(static_cast<bool>(scrollToolbar(toolbar, delta, 30)) == expected);
    sameNav(toolbar->nav_, expectedNav);
  }
  xp_ui_reader_toolbar_nav_reset(&expectedNav);
  expectedNav.selected = -1;
  expectedNav.top = 11;
  resetToolbar(toolbar, -1, 11);
  sameNav(toolbar->nav_, expectedNav);
  for (int mode : {1, 2, 3}) {
    source = {};
    source.mode = mode;
    assert(!paintToolbar(toolbar, &model, supply, &source) && "row-source failure must reject paint");
    assert(!toolbar->ready && source.count == 1);
    cleared(*toolbar);
  }
  source = {};
  assert(!paintToolbar(toolbar, &model, nullptr, &source));
  cleared(*toolbar);
  renderer.paintSucceeds = false;
  source = {};
  assert(!paintToolbar(toolbar, &model, supply, &source));
  assert(!toolbar->ready);
  cleared(*toolbar);
  renderer.paintSucceeds = true;
  std::string longTitle(4096, 'x');
  model.chapter = text(longTitle.c_str());
  model.item_count = 1;
  renderer.longRows = true;
  unsigned longRowCalls = 0;
  struct LongRow {
    const std::string& title;
    unsigned& calls;
  } longRow{longTitle, longRowCalls};
  assert(paintToolbar(
      toolbar, &model,
      [](void* raw, int32_t index, XpEpubToolbarRowSink sink, void* context) -> uint8_t {
        auto& source = *static_cast<LongRow*>(raw);
        assert(index == 0);
        ++source.calls;
        const XpEpubToolbarRow row{text(source.title.c_str()), text(""), 6, 0, 0};
        return sink(context, &row);
      },
      &longRow));
  assert(longRowCalls == 1 && toolbar->windowLabels_[0] == std::string(6, ' ') + longTitle);
  cleared(*toolbar);
  model.item_count = 65536;
  source = {};
  assert(!paintToolbar(toolbar, &model, supply, &source) && source.count == 0);
  cleared(*toolbar);
  model.item_count = 30;
  model.panel = 0;
  source = {};
  assert(paintToolbar(toolbar, &model, supply, &source));
  assert(renderer.longTitles > 0);
  cleared(*toolbar);
  beginToolbar(toolbar);
  assert(!toolbar->ready && toolbar->contact.contact_held == 0);
  closeToolbar(toolbar);
  assert(criticalDepth == 0);
  std::puts(
      "toolbar resource: actual class/render/native leaves; real Rust view/nav/route; temporary rows copied; "
      "direct window; cleared Model on success/failure; preallocated host paint allocation count zero "
      "(not C3 SBO proof); 4096-byte title and indented row preserved; four actual-source mutants required");
}
