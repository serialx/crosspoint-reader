// Appended to the existing physical popup SDK fixture by the driver.
using namespace std::chrono_literals;
extern "C" {
void* xp_epub_test_popup_concurrency_create(void*, void*, const XpEpubLeaves*, const XpAppRuntime*,
                                            const XpAppResources*);
uint8_t xp_epub_test_popup_concurrency_failure(void*);
void xp_epub_test_popup_concurrency_text(void*, int32_t);
void xp_epub_test_popup_concurrency_destroy(void*);
void xp_epub_test_overlay_reject(void*);
}
static std::mutex renderMutex, modelMutex;
static thread_local unsigned renderDepth, modelDepth;
static std::promise<void> paused, resume, attempting;
static std::shared_future<void> resumeFuture;
static std::atomic<bool> pauseLabels, pausePaint, observeAttempt;
static std::atomic<unsigned> updates, generation, paints;
static unsigned spacingChanges, fontChanges, booksClosed;
static GfxRenderer renderer;
static OptionPopup popup;
static int book;

static void locked() { assert(renderDepth == 1 && modelDepth == 0); }
static void* lockRender() {
  assert(!renderDepth && !modelDepth && "non-recursive popup render lock");
  if (observeAttempt.exchange(false)) attempting.set_value();
  renderMutex.lock();
  ++renderDepth;
  return &renderMutex;
}
static void unlockRender(void* token) {
  locked();
  assert(token == &renderMutex);
  --renderDepth;
  renderMutex.unlock();
}
static void paintPause() {
  locked();
  ++paints;
  if (pausePaint.exchange(false)) {
    paused.set_value();
    resumeFuture.wait();
  }
}
static XpAppText constant(const char* value) { return {reinterpret_cast<const uint8_t*>(value), std::strlen(value)}; }
static const XpAppRuntime runtime = [] {
  XpAppRuntime value{};
  value.render_lock = lockRender;
  value.render_unlock = unlockRender;
  value.model_lock = [] {
    assert(!modelDepth);
    modelMutex.lock();
    ++modelDepth;
  };
  value.model_unlock = [] {
    assert(modelDepth == 1);
    --modelDepth;
    modelMutex.unlock();
  };
  value.notify_render = [] {
    assert(!modelDepth && !renderDepth);
    ++updates;
  };
  return value;
}();
static const XpAppResources resources = [] {
  XpAppResources value{};
  value.path_exists = [](XpAppText) -> uint8_t { return 1; };
  value.translate = [](uint16_t) -> XpAppText { return constant(generation == 0 ? "first content" : "later content"); };
  return value;
}();
static const XpUiReaderRenderServices rendering{
    nullptr, nullptr, [](void*, const XpUiReaderRenderOp* op) -> uint32_t {
      locked();
      assert(op->kind == XP_UI_READER_RENDER_CLEAR || op->kind == XP_UI_READER_RENDER_DISPLAY);
      return 0;
    }};
static const XpEpubLeaves leaves = [] {
  XpEpubLeaves value{};
  value.book_open = [](XpAppText) -> void* {
    assert(!renderDepth);
    return &book;
  };
  value.book_close = [](void* p) {
    assert(p == &book && !renderDepth);
    ++booksClosed;
  };
  value.book_cache_path = [](void* p, XpAppTextSink sink, void* context) -> uint8_t {
    assert(p == &book);
    return sink(context, constant("/cached"));
  };
  value.book_load = [](void* p, uint8_t, uint8_t) -> uint8_t {
    assert(p == &book);
    return 0;
  };
  value.book_protection_error = [](void* p, XpAppTextSink sink, void* context) -> uint8_t {
    assert(p == &book);
    return sink(context, constant("loan date unverified"));
  };
  value.settings_snapshot = [](uint8_t, XpEpubSettings* out) {
    *out = {};
    out->embedded_style = 1;
    out->font_size = 16;
  };
  value.font_sizes = [](XpAppText, XpAppTextSink sink, void* context) -> uint8_t {
    assert(!renderDepth && !modelDepth);
    const uint8_t sizes[] = {12, 16, 20};
    const uint8_t changed[] = {24, 28, 32};
    return sink(context, {generation == 0 ? sizes : changed, 3});
  };
  value.log = [](uint8_t, XpAppText, XpAppText) { assert(!modelDepth); };
  value.popup_show = [](void* p, XpAppText title, XpAppText message, uint32_t count, int32_t selected,
                        uint8_t (*label)(void*, uint32_t, XpAppTextSink, void*), void* labelContext,
                        void (*choose)(void*, int32_t), void* choiceContext) {
    locked();
    struct Labels {
      decltype(label) callback;
      void* context;
    } labels{label, labelContext};
    showLeaf(
        p, title, message, count, selected,
        [](void* context, uint32_t index, XpAppTextSink sink, void* sinkContext) -> uint8_t {
          locked();
          auto& source = *static_cast<Labels*>(context);
          if (pauseLabels.exchange(false)) {
            paused.set_value();
            resumeFuture.wait();
          }
          return source.callback(source.context, index, sink, sinkContext);
        },
        &labels, choose, choiceContext);
  };
  value.popup_active = [](void* p) -> uint8_t {
    locked();
    return activeLeaf(p);
  };
  value.popup_process_render = [](void* p) {
    locked();
    static_cast<OptionPopup*>(p)->processRender(renderer, physicalInput);
  };
  value.popup_paint = [](void* p) {
    locked();
    static_cast<OptionPopup*>(p)->render(renderer);
  };
  value.render_services = [](void*) -> const XpUiReaderRenderServices* {
    locked();
    return &rendering;
  };
  value.render_geometry = [](XpEpubGeometry* out) {
    locked();
    *out = {};
  };
  value.settings_line_spacing = [](uint8_t index) {
    assert(!renderDepth && index == 0);
    ++spacingChanges;
  };
  value.settings_font_size = [](uint8_t size) {
    assert(!renderDepth && size == 16 && "retained font-size payload changed");
    ++fontChanges;
  };
  value.settings_save = [] { assert(!renderDepth); };
  value.fonts_ensure_loaded = [] { locked(); };
  return value;
}();
static void resetPause() {
  paused = std::promise<void>();
  resume = std::promise<void>();
  attempting = std::promise<void>();
  resumeFuture = resume.get_future().share();
}
static void render(void* owner) {
  auto* token = lockRender();
  assert(xp_epub_test_entry_gate_render(owner));
  unlockRender(token);
}
int main() {
  void* owner = xp_epub_test_popup_concurrency_create(&renderer, &popup, &leaves, &runtime, &resources);
  assert(owner && !xp_epub_test_entry_gate_load(owner));
  XpEpubEntryGateObservation observation{};
  xp_epub_test_entry_gate_observe(owner, &observation);
  assert(observation.ready && booksClosed == 1 && !popup.isActive());

  resetPause();
  pauseLabels = true;
  auto show = std::async(std::launch::async, [=] { assert(xp_epub_test_popup_concurrency_failure(owner)); });
  assert(paused.get_future().wait_for(5s) == std::future_status::ready);
  observeAttempt = true;
  auto firstPaint = std::async(std::launch::async, [=] { render(owner); });
  assert(attempting.get_future().wait_for(5s) == std::future_status::ready);
  assert(firstPaint.wait_for(50ms) == std::future_status::timeout);
  resume.set_value();
  show.get();
  firstPaint.get();
  assert(popup.message == "first content" && popup.ownedStrings.size() == 2);

  resetPause();
  pausePaint = true;
  auto paint = std::async(std::launch::async, [=] { render(owner); });
  assert(paused.get_future().wait_for(5s) == std::future_status::ready);
  generation = 1;
  observeAttempt = true;
  auto replace = std::async(std::launch::async, [=] { assert(xp_epub_test_popup_concurrency_failure(owner)); });
  assert(attempting.get_future().wait_for(5s) == std::future_status::ready);
  assert(replace.wait_for(50ms) == std::future_status::timeout);
  resume.set_value();
  paint.get();
  replace.get();
  assert(popup.message == "later content" && popup.ownedStrings.size() == 2);

  xp_epub_test_popup_concurrency_text(owner, 2);
  assert(popup.isActive());
  physicalInput.released[static_cast<size_t>(MappedInputManager::Button::Confirm)] = true;
  XpEpubPopupInput result{};
  inputLeaf(&popup, &result);
  assert(result.consumed && result.changed && !result.active && spacingChanges == 1 && updates == 1);
  assert(!renderDepth && !modelDepth);
  dismissLeaf(&popup);

  for (int row = 1; row <= 3; ++row) {
    physicalInput.released[static_cast<size_t>(MappedInputManager::Button::Confirm)] = false;
    generation = 0;
    assert(xp_epub_test_popup_concurrency_failure(owner));
    render(owner);
    xp_epub_test_popup_concurrency_text(owner, 1);
    assert(popup.isActive() && popup.selectedIndex == 1);
    const auto title = popup.title;
    const auto labels = popup.ownedStrings;
    const auto beforePaints = paints.load();
    resetPause();
    auto* token = lockRender();
    generation = 1;
    observeAttempt = true;
    auto text = std::async(std::launch::async, [=] { xp_epub_test_popup_concurrency_text(owner, row); });
    assert(attempting.get_future().wait_for(5s) == std::future_status::ready);
    assert(text.wait_for(50ms) == std::future_status::timeout);
    xp_epub_test_overlay_reject(owner);
    unlockRender(token);
    text.get();
    assert(popup.title == title && popup.ownedStrings == labels && popup.selectedIndex == 1 &&
           "late rejection must preserve popup content");
    assert(paints == beforePaints && "rejected popup action must not paint");
    xp_epub_test_entry_gate_observe(owner, &observation);
    assert(observation.scene_failed);

    // Recover, then exercise the real retained callback to check both intent and payload.
    render(owner);
    physicalInput.released[static_cast<size_t>(MappedInputManager::Button::Confirm)] = true;
    inputLeaf(&popup, &result);
    assert(result.consumed && result.changed && !result.active && fontChanges == static_cast<unsigned>(row));
    assert(spacingChanges == 1 && updates == static_cast<unsigned>(row + 1));
    dismissLeaf(&popup);
  }
  xp_epub_test_popup_concurrency_destroy(owner);
  std::puts("popup: real content exclusion, unlocked choices, and three late-rejection rows preserve callback data");
}
