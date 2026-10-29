// Included after the extracted original and current keyboard implementations.
class HitTarget final : public fui::DrawTarget {
 public:
  fui::Rect clipRect() const override { return {0, 0, 528, 792}; }
  bool setClipRect(fui::Rect) override { return true; }
  fui::Size measureText(fui::FontId, const char*, fui::TextStyle) const override { return {8, 20}; }
  int16_t lineHeight(fui::FontId) const override { return 20; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char*, fui::TextStyle) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};
void OriginalKeyboard::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto kb = keyboardRect();
  const int height = keyboardKeysHeight(metrics, currentLayout().rowCount, true);
  fui::Rect keys{kb.x, int16_t(kb.y + (kb.height - height) / 2), kb.width, int16_t(height)};
  interactions.beginPublishCycle();
  HitTarget target;
  fui::DeviceContext device;
  device.width = 528;
  device.height = 792;
  fui::Frame<56> frame(target, device, {}, interactions);
  fui::KeyboardProps props;
  props.layout = &currentLayout();
  props.keyAction = 1;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.selectedIndex = -1;
  props.gap = props.rowGap = keyboardGap(metrics);
  props.uniformKeyWidth = !urlPanel;
  props.bottomHitOverflow = std::max(0, 792 - keys.bottom());
  fui::keyboard(frame, keys, props);
  interactions.publish();
  interactionsReady = true;
}
struct Pair {
  GfxRenderer renderer;
  MappedInputManager input;
  OriginalKeyboard old;
  candidate::KeyboardEntryActivity current;
  unsigned frames = 0;
  explicit Pair(std::string text = "", InputType type = InputType::Text) : old(renderer, input, "Test", text, 0, type) {
    SETTINGS.keyboardLayouts = 1023;
    I18N.language = Language::EN;
    ButtonNavigator::setMappedInputManager(input);
    old.onEnter();
    assert(current.text.grow(4096, 0));
    memcpy(current.text.data.get(), text.c_str(), text.size() + 1);
    xp_ui_keyboard_init(&current.state, text.size(), 0, uint8_t(type), 1023, uint8_t(Language::EN));
    current.arena = std::make_unique<candidate::KeyboardEntryActivity::Arena>();
    auto& l = current.layout;
    l.screen = {0, 0, 528, 792};
    l.content = l.screen;
    l.font = 1;
    l.small_font = 0;
    l.spacing = 8;
    l.header_height = 40;
    l.key_height = 56;
    l.hints_height = 40;
    l.key_spacing = 6;
    l.text_percent = 80;
    l.touch = 1;
    l.field_normal = 1;
    l.field_cursor = 2;
    l.language = uint8_t(Language::EN);
    publish();
  }
  std::string bytes() const { return {(const char*)current.text.data.get(), current.state.length}; }
  void equal() const {
    if (bytes() != old.text || current.state.cursor != old.cursorPos) {
      fprintf(stderr, "frame %u: text sizes old=%zu new=%u cursor old=%zu new=%u\n", frames, old.text.size(),
              current.state.length, old.cursorPos, current.state.cursor);
      assert(false);
    }
    assert(current.state.layout == uint8_t(old.layoutId));
    assert(bool(current.state.shifted) == old.shifted && bool(current.state.symbols) == old.symbols);
    assert(bool(current.state.url_panel) == old.urlPanel && bool(current.state.cursor_mode) == old.cursorMode);
    assert(bool(current.state.password_visible) == old.passwordVisible);
    if (old.result == 1) assert(current.finished && current.result == old.returned && current.result == old.text);
    if (old.result == 2) assert(current.finished && current.result == "cancel");
  }
  void publish() {
    old.render(RenderLock{});
    XpUiScene scene{};
    assert(current.buildRustScene(scene));
    const int active = old.interactions.activeIndex();
    const int16_t paintedActive = active >= 0 && size_t(active) < old.interactions.publishedCount()
                                      ? old.interactions.publishedData()[active].value
                                      : INT16_MIN;
    assert(current.arena->snapshot.active_key == paintedActive);
    current.rustPublished();
    current.publication.ready = true;
  }
  fui::Point point(int16_t value) const {
    for (size_t i = 0; i < old.interactions.publishedCount(); ++i) {
      const auto& h = old.interactions.publishedData()[i];
      if (h.value == value) return {int16_t(h.rect.x + h.rect.width / 2), int16_t(h.rect.y + h.rect.height / 2)};
    }
    assert(false);
    return {};
  }
  void frame(MappedInputManager value) {
    ++frames;
    now += 100;
    input = value;
    current.mappedInput = value;
    old.loop();
    current.loop();
    equal();
  }
  void touch(fui::Point p, bool down, bool tap, bool held) {
    MappedInputManager in;
    in.x = p.x;
    in.y = p.y;
    in.down = down;
    in.tapped = tap;
    in.contact = held;
    frame(in);
  }
  void tap(int16_t value) { touch(point(value), false, true, false); }
  void accept() {
    publish();
    tap(fui::QWERTY_KEY_ENTER);
    assert(old.result == 1 && current.finished && old.returned == bytes());
  }
};
int main() {
  unsigned cases = 0;
  {
    Pair p("é");
    const auto lower = p.point('q');
    p.tap(fui::QWERTY_KEY_SHIFT);
    p.touch(lower, false, true, false);
    // Baseline resolves the published value against the LIVE layer: 'q' is
    // absent after Shift, so it does not insert the still-painted lower case.
    assert(p.bytes() == "é");
    p.publish();
    p.tap('Q');
    assert(p.bytes() == "éQ" && p.current.state.cursor == 3);
    const auto upper = p.point('Q');
    p.tap(fui::QWERTY_KEY_LANG);
    assert(p.current.state.layout == 1);
    p.touch(upper, false, true, false);
    assert(p.bytes() == "éQ");
    p.publish();
    p.tap('a');
    assert(p.bytes() == "éQa");
    const auto french = p.point('a');
    for (unsigned i = 0; i < 3; ++i) p.tap(fui::QWERTY_KEY_LANG);
    assert(p.current.state.layout == 4);
    p.touch(french, false, true, false);
    assert(p.bytes() == "éQa");
    p.publish();
    const auto& russian = p.old.currentLayout().rows[0].keys[0];
    p.tap(russian.value);
    assert(p.bytes() == std::string("éQa") + russian.output);
    p.accept();
    ++cases;
  }
  {
    Pair p("", InputType::Url);
    const auto letter = p.point('q');
    p.tap(-3);
    p.touch(letter, false, true, false);
    assert(p.bytes().empty());
    p.publish();
    p.tap(2001);
    const auto snippet = p.point(2001);
    const auto before = p.bytes();
    assert(!before.empty());
    p.tap(-3);
    p.touch(snippet, false, true, false);
    // A snippet absent from the live letter page is dropped, as in C++.
    assert(p.bytes() == before);
    p.publish();
    p.tap('q');
    p.accept();
    ++cases;
  }
  // Real bidi, original cursor-point routine and current facade field routing.
  // Fixed glyph advances isolate input geometry; native-font pixels have a
  // separate original-versus-current renderer oracle.
  for (const auto& text : {std::string("aéЖאع"), std::string("אבגדה"), std::string("مرحبا")})
    for (auto type : {InputType::Text, InputType::Password})
      for (int x : {0, 52, 56, 60, 64, 68, 72, 80, 96, 120, 500}) {
        Pair p(text, type);
        p.touch({int16_t(x), 80}, false, true, false);
        const auto cursor = p.current.state.cursor;
        assert(cursor == 0 || cursor == text.size() || (uint8_t(text[cursor]) & 0xc0) != 0x80);
        p.tap('q');
        auto expected = text;
        expected.insert(cursor, "q");
        assert(p.bytes() == expected && p.current.state.cursor == cursor + 1);
        p.accept();
        ++cases;
      }
  {
    Pair p("éאב", InputType::Password);
    const auto key = p.point('q');
    p.touch(key, true, false, true);
    p.publish();
    p.touch({64, 80}, false, true, false);
    const auto afterField = p.bytes();
    p.publish();
    p.touch({527, 300}, false, true, false);
    // A second release without a new down preserves the C++ router's stale
    // fallback after a field tap. A fresh off-target contact clears it.
    auto stale = afterField;
    stale.insert(p.current.state.cursor - 1, "q");
    assert(p.bytes() == stale);
    p.touch({527, 300}, true, false, true);
    p.touch({527, 300}, false, true, false);
    assert(p.bytes() == stale);
    p.frame({});
    assert(p.current.state.active_key == INT16_MIN && p.old.interactions.activeIndex() == -1);
    p.accept();
    ++cases;
  }
  {
    Pair p("pending");
    p.old.syncSelectionToValue(fui::QWERTY_KEY_SHIFT);
    p.current.state.row = p.old.selRow;
    p.current.state.col = p.old.selCol;
    p.touch(p.point('q'), true, false, true);
    MappedInputManager in;
    in.contact = true;
    in.pressed = in.held = 1u << unsigned(MappedInputManager::Button::Confirm);
    p.frame(in);
    in.pressed = in.held = 0;
    in.released = 1u << unsigned(MappedInputManager::Button::Confirm);
    p.frame(in);
    assert(p.current.state.shifted);
    p.publish();
    p.touch({527, 300}, false, true, false);
    assert(p.bytes() == "pendingQ");
    assert(p.current.state.active_key == INT16_MIN && p.old.interactions.activeIndex() == -1);
    p.publish();
    p.tap('q');
    assert(p.bytes() == "pendingQq");
    p.accept();
    ++cases;
  }
  {
    Pair p("https://example.test/é", InputType::Url);
    // Populate both SDK generations before the shorter page reuses one.
    p.publish();
    assert(p.old.interactions.publishedCount() == 44);
    p.old.syncSelectionToValue(-3);
    p.current.state.row = p.old.selRow;
    p.current.state.col = p.old.selCol;
    p.touch(p.point(fui::QWERTY_KEY_ENTER), true, false, true);
    assert(p.old.interactions.activeIndex() == 43);
    MappedInputManager in;
    in.contact = true;
    in.pressed = in.held = 1u << unsigned(MappedInputManager::Button::Confirm);
    p.frame(in);
    in.pressed = in.held = 0;
    in.released = 1u << unsigned(MappedInputManager::Button::Confirm);
    p.frame(in);
    assert(p.current.state.url_panel);
    p.publish();
    const int active = p.old.interactions.activeIndex();
    assert(active == 43 && p.old.interactions.publishedCount() == 13);
    // Inside the allocated 56 slots, but outside this publication's 13 keys.
    const auto& stale = p.old.interactions.publishedData()[active];
    assert(stale.action == 1 && stale.value == fui::QWERTY_KEY_ENTER);
    assert(p.current.state.active_index == active && p.current.state.active_key == INT16_MIN);
    assert(p.old.result == 0 && !p.current.finished);
    const auto text = p.bytes();
    const auto cursor = p.current.state.cursor;
    in = {};
    in.x = 527;
    in.y = 300;
    in.tapped = true;
    now += 100;
    p.input = p.current.mappedInput = in;
    // Deliberate deviation: do not reuse equal(), which requires equal results.
    p.old.loop();
    p.current.loop();
    assert(p.old.result == 1 && p.old.returned == text && p.old.text == text);
    assert(!p.current.finished && p.current.result.empty());
    assert(p.bytes() == text && p.current.state.cursor == cursor && p.old.cursorPos == cursor);
    assert(p.current.state.active_key == INT16_MIN && p.current.state.active_index == -1);
    assert(p.old.interactions.activeIndex() == -1);
    printf(
        "stale-slot deviation: held Enter index=43, published count=13; original accepts %zu bytes, "
        "Rust stays open with unchanged bytes/cursor and clears contact\n",
        text.size());
  }
  {
    Pair p("pending URL", InputType::Url);
    p.publish();
    assert(p.old.interactions.publishedCount() == 44);
    p.old.syncSelectionToValue(-3);
    p.current.state.row = p.old.selRow;
    p.current.state.col = p.old.selCol;
    MappedInputManager in;
    in.pressed = in.held = 1u << unsigned(MappedInputManager::Button::Confirm);
    p.frame(in);
    in.pressed = in.held = 0;
    in.released = 1u << unsigned(MappedInputManager::Button::Confirm);
    p.frame(in);
    assert(p.current.state.url_panel && p.old.interactions.publishedCount() == 44);
    // Press the old published URL key while the snippet render is pending.
    const fui::Point point{303, 736};
    p.touch(point, true, false, true);
    assert(p.old.interactions.activeIndex() == 42);
    p.publish();
    assert(p.old.interactions.publishedCount() == 13 && p.old.interactions.activeIndex() == 42);
    assert(p.old.interactions.publishedData()[42].value == -3);
    fui::Interaction hit;
    assert(!p.old.interactions.hitPublished(point.x, point.y, 1, hit));
    assert(p.current.state.active_index == 42 && p.current.state.active_key == INT16_MIN);
    const auto text = p.bytes();
    const auto cursor = p.current.state.cursor;
    in = {};
    in.x = point.x;
    in.y = point.y;
    in.tapped = true;
    now += 100;
    p.input = p.current.mappedInput = in;
    p.old.loop();
    p.current.loop();
    // The SDK's stale URL slot toggles back; Rust ignores the out-of-count key.
    assert(!p.old.urlPanel && p.current.state.url_panel);
    assert(p.old.result == 0 && !p.current.finished && p.current.result.empty());
    assert(p.old.text == text && p.bytes() == text);
    assert(p.old.cursorPos == cursor && p.current.state.cursor == cursor);
    assert(p.current.state.active_key == INT16_MIN && p.current.state.active_index == -1);
    assert(p.old.interactions.activeIndex() == -1);
    printf(
        "stale-slot deviation: pending-render URL press/release=(303,736), index=42, count=13; "
        "original toggles to letters, Rust stays on snippets with unchanged bytes/cursor and clears contact\n");
  }
  {
    Pair p("cancel");
    MappedInputManager in;
    in.released = 1u << unsigned(MappedInputManager::Button::Back);
    p.frame(in);
    assert(p.old.result == 2 && p.current.result == "cancel");
    ++cases;
  }
  printf("%u real-layout routed cases match; 2 traces pin the stale-slot safety deviation\n", cases);
}
