#include <xp_epub_session.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <new>
#include <string>
#include <vector>

static bool countAllocations = false;
static size_t allocationAttempts = 0;
void* operator new(std::size_t size) {
  if (countAllocations) ++allocationAttempts;
  if (void* result = std::malloc(size ? size : 1)) return result;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

enum Event { Choice = 1, Haptic, Update };
static std::array<Event, 64> events{};
static size_t eventCount = 0;
static void event(Event value) {
  assert(eventCount < events.size());
  events[eventCount++] = value;
}
static void expectEvents(std::initializer_list<Event> expected) {
  assert(eventCount == expected.size());
  assert(std::equal(expected.begin(), expected.end(), events.begin()));
  eventCount = 0;
}
namespace haptic_feedback {
static void touchAction() { event(Haptic); }
}  // namespace haptic_feedback

// Physical SDK fakes supply a routed event and record the production render output.
namespace freeink::ui {
using ActionId = uint16_t;
struct InputSnapshot {
  bool touchPressed = false, touchReleased = false, touchHeld = false;
  int16_t touchX = -1, touchY = -1;
};
struct ActionEvent {
  ActionId action = 0;
  int16_t value = 0;
  explicit operator bool() const { return action != 0; }
};
struct Interaction {
  ActionId action = 0;
  int16_t value = 0;
};
template <size_t N>
struct InteractionBuffer {
  ActionEvent routed{};
  std::array<Interaction, N> hits{};
  int16_t active = -1;
  unsigned routes = 0, begins = 0, publishes = 0;
  ActionEvent routePublished(const InputSnapshot&) {
    ++routes;
    return routed;
  }
  int16_t activeIndex() const { return active; }
  const Interaction* publishedData() const { return hits.data(); }
  void beginPublishCycle() { ++begins; }
  void publish() { ++publishes; }
};
enum class TextAlign { Center };
enum class SelectionStyle { Default, InvertFill };
enum class Color { Black };
struct Paint {
  static Paint solid(Color) { return {}; }
};
struct Style {
  Paint border{};
  uint8_t borderWidth = 0, radius = 0;
};
struct Styles {
  Style normal, selected, focused, active, disabled;
};
static Styles defaultButtonStyles() { return {}; }
static Styles defaultPopupStyles() { return {}; }
static void setStyleRadius(Styles&, uint8_t) {}
struct ThemeTokens {
  SelectionStyle listSelectionStyle = SelectionStyle::Default;
  uint8_t listRowRadius = 0;
};
struct Insets {
  int16_t left, top, right, bottom;
};
struct Rect {
  int16_t x = 0, y = 0, width = 600, height = 800;
};
struct Size {
  int16_t width, height;
};
struct DeviceContext {
  Rect screen() const { return {}; }
};
struct GfxRendererTarget {
  static constexpr int FONT_BODY = 1;
  DeviceContext deviceContext() const { return {}; }
  int lineHeight(int) const { return 16; }
};
enum { StateNormal = 0, StateFocused = 1, InputTouch = 1 };
struct DialogOption {
  const char* label = nullptr;
  ActionId action = 0;
  int16_t value = 0;
  int state = 0;
};
struct TextProps {
  int font = 0;
  bool bold = false;
  TextAlign align = TextAlign::Center;
  int maxLines = 0;
};
struct OptionDialogProps {
  const char* title = nullptr;
  const char* headline = nullptr;
  const char* message = nullptr;
  TextProps messageText, titleText, headlineText, buttonText;
  DialogOption* options = nullptr;
  uint8_t optionCount = 0;
  bool verticalOptions = false;
  int inputMask = 0;
  Insets padding{};
  int16_t gap = 0, buttonHeight = 0;
  Styles buttonStyles, styles;
};
template <size_t N>
struct Frame {
  Frame(GfxRendererTarget&, const DeviceContext&, const InputSnapshot&, InteractionBuffer<N>&) {}
  void hit(Rect, ActionId, int, int) {}
};
static uint8_t renderedCount = 0;
static std::array<std::string, 16> renderedLabels;
static std::array<int16_t, 16> renderedValues{};
static int renderedFocus = -1;
template <size_t N>
static void optionDialog(Frame<N>&, Rect, const OptionDialogProps& props) {
  renderedCount = props.optionCount;
  assert(renderedCount <= renderedLabels.size());
  renderedFocus = -1;
  for (uint8_t i = 0; i < renderedCount; ++i) {
    renderedLabels[i] = props.options[i].label;
    renderedValues[i] = props.options[i].value;
    if (props.options[i].state == StateFocused) renderedFocus = i;
  }
}
static int16_t clampI16(int value, int low = INT16_MIN, int high = INT16_MAX) {
  return static_cast<int16_t>(std::clamp(value, low, high));
}
static int optionDialogHeight(GfxRendererTarget&, const OptionDialogProps&, int) { return 200; }
static Rect centeredRect(Rect screen, Size size) { return {screen.x, screen.y, size.width, size.height}; }
}  // namespace freeink::ui

struct GfxRenderer {
  unsigned displays = 0;
  void displayBuffer() { ++displays; }
};
struct MappedInputManager {
  enum class Button { NavPrevious, NavNext, Confirm, Back };
  freeink::ui::InputSnapshot snap{};
  std::array<bool, 4> pressed{}, released{};
  bool wasPressed(Button b) const { return pressed[static_cast<size_t>(b)]; }
  bool wasReleased(Button b) const { return released[static_cast<size_t>(b)]; }
  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) const { return {a, b, c, d}; }
};
static freeink::ui::InputSnapshot touchSnapshotFrom(MappedInputManager& input) { return input.snap; }
static freeink::ui::GfxRendererTarget makeUiTarget(const GfxRenderer&) { return {}; }
static const freeink::ui::ThemeTokens& refreshSharedUiThemeTokens(freeink::ui::GfxRendererTarget&) {
  static const freeink::ui::ThemeTokens tokens;
  return tokens;
}
struct UITheme {
  struct Metrics {
    int optionPopupInnerPadding = 4, optionPopupItemSpacing = 2, popupFrameThickness = 1, popupCornerRadius = 2,
        optionPopupSelectionVPadding = 2, optionPopupDialogSideMargin = 8;
  } metrics;
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  const Metrics& getMetrics() const { return metrics; }
};
struct Gui {
  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) {}
} GUI;
enum class StrId { Title, First, Second };
struct I18n {
  const char* get(StrId id) const { return id == StrId::Title ? "title" : id == StrId::First ? "first" : "second"; }
} I18N;
constexpr int STR_BACK = 1, STR_SELECT = 2, STR_DIR_UP = 3, STR_DIR_DOWN = 4;
static const char* tr(int) { return "label"; }

// ACTUAL_OPTION_POPUP
// ACTUAL_PATH_TEXT
static MappedInputManager physicalInput;
static MappedInputManager* const mapped = &physicalInput;
static decltype(XpEpubLeaves::popup_show) showLeaf = /* ACTUAL_SHOW_LEAF */;
static decltype(XpEpubLeaves::popup_input) inputLeaf = /* ACTUAL_INPUT_LEAF */;
static decltype(XpEpubLeaves::popup_active) activeLeaf = /* ACTUAL_ACTIVE_LEAF */;
static decltype(XpEpubLeaves::popup_dismiss) dismissLeaf = /* ACTUAL_DISMISS_LEAF */;

static XpAppText wire(const std::string& text) {
  return {reinterpret_cast<const uint8_t*>(text.data()), static_cast<uint32_t>(text.size())};
}
struct Source {
  std::vector<std::string> labels;
  std::array<unsigned, 40> calls{};
  bool showing = false;
  int fail = -1;
};
static uint8_t source(void* raw, uint32_t index, XpAppTextSink sink, void* context) {
  auto& labels = *static_cast<Source*>(raw);
  assert(labels.showing && index < labels.labels.size() && index < labels.calls.size());
  ++labels.calls[index];
  if (static_cast<int>(index) == labels.fail) return 0;
  return sink(context, wire(labels.labels[index]));
}
struct Selection {
  OptionPopup* popup = nullptr;
  XpEpubPopupInput* output = nullptr;
  unsigned calls = 0;
  int index = -1;
};
static void chosen(void* raw, int32_t index) {
  auto& selection = *static_cast<Selection*>(raw);
  assert(selection.popup && !selection.popup->isActive());
  assert(selection.popup->selectedIndex == index);
  if (selection.output) assert(selection.output->changed == 0 && "choice must precede changed update");
  ++selection.calls;
  selection.index = index;
  event(Choice);
}
static void show(OptionPopup& popup, Source& labels, Selection& selection, int current = 0) {
  static const std::string title = "title", message = "message";
  labels.showing = true;
  showLeaf(&popup, wire(title), wire(message), static_cast<uint32_t>(labels.labels.size()), current, source, &labels,
           chosen, &selection);
  labels.showing = false;
}
static Source labels(unsigned count) {
  Source result;
  for (unsigned i = 0; i < count; ++i) result.labels.push_back("label-" + std::to_string(i));
  return result;
}
static void copiedLabelsAndCap() {
  OptionPopup popup;
  Selection selection{&popup};
  auto input = std::make_unique<Source>(labels(33));
  input->labels[0] = "";
  input->labels[1] = "short";
  input->labels[2] = std::string(255, 'x');
  input->labels[17] = std::string(255, 'y');
  input->labels[31] = "255 pt";
  input->labels[32] = "last label beyond widget cap";
  const auto expected = input->labels;
  show(popup, *input, selection, 32);
  assert(popup.ownedStrings == expected && popup.ownedStrings.size() == 33);
  assert(popup.selectedIndex == 0 && activeLeaf(&popup));
  for (unsigned i = 0; i < 33; ++i) assert(input->calls[i] == 1);
  for (auto& text : input->labels) text.assign(255, '!');
  input.reset();
  assert(popup.ownedStrings == expected);
  GfxRenderer renderer;
  popup.render(renderer);
  assert(freeink::ui::renderedCount == 16 && freeink::ui::renderedFocus == 0);
  for (int i = 0; i < 16; ++i) {
    assert(freeink::ui::renderedLabels[i] == expected[i]);
    assert(freeink::ui::renderedValues[i] == i);
  }
  auto update = [] { event(Update); };
  assert(popup.handleButtons(true, false, false, false, update));
  assert(popup.selectedIndex == 15);
  expectEvents({Update});
  assert(popup.handleButtons(false, true, false, false, update));
  assert(popup.selectedIndex == 0);
  expectEvents({Update});
  assert(popup.handleButtons(false, false, true, false, [&] {
    assert(selection.calls == 1 && "choice must precede requestUpdate");
    event(Update);
  }));
  assert(selection.index == 0 && !activeLeaf(&popup));
  expectEvents({Choice, Update});
  assert(!popup.handleButtons(false, false, true, false, update));
  assert(selection.calls == 1);
}
static void sourceFunctorLifetime() {
  OptionPopup popup;
  std::weak_ptr<std::vector<std::string>> borrowed;
  Selection selection{&popup};
  {
    auto values = std::make_shared<std::vector<std::string>>(std::initializer_list<std::string>{"first", "second"});
    borrowed = values;
    popup.show(
        "title", "body", 2, 1, [values](int index, std::string& out) { out = (*values)[index]; },
        [&selection](int index) { chosen(&selection, index); });
  }
  assert(borrowed.expired() && "label source must not be retained");
  assert(popup.ownedStrings[1] == "second");
  popup.handleButtons(false, false, true, false, [] { event(Update); });
  expectEvents({Choice, Update});
}
static void noCallbackAllocationAfterPreallocation() {
  OptionPopup popup;
  Selection selection{&popup};
  auto input = labels(33);
  for (auto& text : input.labels) text.assign(255, 'p');
  show(popup, input, selection);
  allocationAttempts = 0;
  countAllocations = true;
  show(popup, input, selection, 15);
  countAllocations = false;
  assert(allocationAttempts == 0 && "two-pointer choice assignment must stay inline");
  for (unsigned i = 0; i < 33; ++i) assert(input.calls[i] == 2);
  popup.handleButtons(false, false, true, false, [] { event(Update); });
  assert(selection.calls == 1 && selection.index == 15);
  expectEvents({Choice, Update});
}
static void emptyFailedAndCancel() {
  OptionPopup popup;
  Selection selection{&popup};
  auto input = labels(0);
  show(popup, input, selection, -1);
  assert(!popup.isActive() && popup.ownedStrings.empty() && popup.selectedIndex == 0);
  assert(!popup.handleButtons(true, true, true, true, [] { assert(false); }));
  input = labels(2);
  show(popup, input, selection, 1);
  input.fail = 1;
  show(popup, input, selection, -3);
  assert(popup.ownedStrings[1].empty() && popup.selectedIndex == 0);
  popup.handleButtons(false, false, false, true, [] { event(Update); });
  expectEvents({Update});
  assert(selection.calls == 0 && !popup.isActive());
  show(popup, input, selection);
  dismissLeaf(&popup);
  assert(!popup.isActive() && !popup.onSelectCallback);
  assert(!popup.handleButtons(false, false, true, false, [] { assert(false); }));
  assert(selection.calls == 0);
}
static void nativeInputOrderAndTouch() {
  OptionPopup popup;
  XpEpubPopupInput output{};
  Selection selection{&popup, &output};
  auto input = labels(3);
  physicalInput = {};
  show(popup, input, selection, 2);
  physicalInput.released[static_cast<size_t>(MappedInputManager::Button::Confirm)] = true;
  inputLeaf(&popup, &output);
  assert(output.consumed && output.changed && !output.active && selection.index == 2);
  expectEvents({Choice});
  inputLeaf(&popup, &output);
  assert(!output.active && !output.changed && !output.consumed && selection.calls == 1);

  physicalInput = {};
  show(popup, input, selection);
  physicalInput.snap = {false, true, false, 10, 10};
  popup.interactions.routed = {OptionPopup::ACTION_OPTION, 1};
  inputLeaf(&popup, &output);
  assert(output.active && output.consumed && !output.changed && popup.interactions.routes == 0);
  GfxRenderer renderer;
  popup.render(renderer);
  inputLeaf(&popup, &output);
  assert(selection.calls == 2 && selection.index == 1 && output.changed && !output.active);
  expectEvents({Choice, Haptic});

  show(popup, input, selection);
  popup.render(renderer);
  popup.interactions.routed = {OptionPopup::ACTION_CHROME, 0};
  inputLeaf(&popup, &output);
  assert(output.active && output.consumed && !output.changed && selection.calls == 2);
  popup.interactions.routed = {};
  physicalInput.snap.touchX = -1;
  inputLeaf(&popup, &output);
  assert(output.active && !output.changed);
  physicalInput.snap.touchX = 10;
  inputLeaf(&popup, &output);
  assert(!output.active && output.changed && selection.calls == 2);
  expectEvents({Haptic});

  show(popup, input, selection);
  popup.render(renderer);
  popup.interactions.active = 0;
  popup.interactions.hits[0] = {OptionPopup::ACTION_OPTION, 2};
  physicalInput.snap = {true, false, false, 10, 10};
  inputLeaf(&popup, &output);
  assert(popup.selectedIndex == 2 && output.active && output.changed && selection.calls == 2);
  physicalInput = {};
  physicalInput.released[static_cast<size_t>(MappedInputManager::Button::Back)] = true;
  inputLeaf(&popup, &output);
  assert(!output.active && output.changed && output.consumed && selection.calls == 2);
  expectEvents({});
}
static void touchSelectionOrderDirectly() {
  OptionPopup popup;
  auto input = labels(2);
  Selection selection{&popup};
  show(popup, input, selection);
  GfxRenderer renderer;
  popup.render(renderer);
  MappedInputManager touch;
  touch.snap = {false, true, false, 10, 10};
  popup.interactions.routed = {OptionPopup::ACTION_OPTION, 1};
  assert(popup.handleInput(touch, [&] {
    assert(selection.calls == 1 && "touch choice must precede requestUpdate");
    event(Update);
  }));
  expectEvents({Choice, Haptic, Update});
}
static void legacyOverloads() {
  OptionPopup popup;
  auto callback = [](int) {};
  const char* options[] = {"one", "two"};
  popup.show("title", options, 2, 1, callback);
  assert(popup.ownedStrings[1] == "two" && popup.selectedIndex == 1);
  popup.show("title", "subject", options, 2, 0, callback);
  assert(popup.headline == "subject");
  popup.showMessage("title", "body", options, 2, 1, callback);
  assert(popup.message == "body");
  const StrId ids[] = {StrId::First, StrId::Second};
  popup.show(StrId::Title, ids, 2, 0, callback);
  assert(popup.ownedStrings[0] == "first" && popup.message.empty());
  popup.show(StrId::Title, std::vector<std::string>{"vector"}, 0, callback);
  assert(popup.ownedStrings.size() == 1 && popup.ownedStrings[0] == "vector");
  GfxRenderer renderer;
  MappedInputManager input;
  assert(popup.processRender(renderer, input) && renderer.displays == 1);
  popup.dismiss();
  assert(!popup.processRender(renderer, input) && renderer.displays == 1);
}
int main() {
  copiedLabelsAndCap();
  sourceFunctorLifetime();
  noCallbackAllocationAfterPreallocation();
  emptyFailedAndCancel();
  nativeInputOrderAndTouch();
  touchSelectionOrderDirectly();
  legacyOverloads();
  assert(!countAllocations && eventCount == 0);
}
