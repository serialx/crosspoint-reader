#include <cstdlib>
#include <cstring>
#include <vector>

namespace recovery_adapter {
static EpubReaderActivity* activeFixture;
static void leaf() { assert(modelDepth == 0 && activeFixture); }
static void unlocked() { leaf(); assert(locks == 0); }
static void locked() { leaf(); assert(locks == 1); }
static void context(void* value) { unlocked(); assert(value == activeFixture); }
static XpAppText wire(const char* value) {
  return {reinterpret_cast<const uint8_t*>(value), std::strlen(value)};
}
static std::string nativeText(XpAppText value) {
  assert(value.data || !value.len);
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}
static constexpr StrId nativeKeys[] = {
  StrId::STR_DRM_PROTECTED_FILE, StrId::STR_LOAN_EXPIRED, StrId::STR_LOAN_TIME_UNVERIFIED,
  StrId::STR_CLOCK_SYNC_NOW, StrId::STR_OK_BUTTON, StrId::STR_SYNCING_TIME, StrId::STR_RETRY,
  StrId::STR_CLOCK_SYNC_FAIL,
};
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) { leaf(); assert(false && "unexpected recovery leaf"); std::abort(); }
};
#define UNUSED_LEAF(field) table.field = &Unexpected<decltype(table.field)>::call

static const XpAppResources resources = [] {
  XpAppResources table{};
  static_assert(sizeof(XpAppResources) == 46 * sizeof(void (*)()));
  UNUSED_LEAF(settings_snapshot);
  UNUSED_LEAF(recents_visit);
  UNUSED_LEAF(recent_missing);
  UNUSED_LEAF(recent_update);
  UNUSED_LEAF(opds_count);
  UNUSED_LEAF(book_progress);
  UNUSED_LEAF(library_open);
  UNUSED_LEAF(library_count);
  UNUSED_LEAF(library_row);
  UNUSED_LEAF(library_close);
  UNUSED_LEAF(library_rebuild);
  UNUSED_LEAF(book_open);
  UNUSED_LEAF(book_thumb_path);
  UNUSED_LEAF(book_load);
  UNUSED_LEAF(book_generate_thumb);
  UNUSED_LEAF(book_generate_thumb_from_source);
  UNUSED_LEAF(book_close);
  UNUSED_LEAF(path_exists);
  UNUSED_LEAF(thumbnail_path);
  UNUSED_LEAF(bitmap_open);
  UNUSED_LEAF(bitmap_info);
  UNUSED_LEAF(bitmap_draw);
  UNUSED_LEAF(bitmap_close);
  UNUSED_LEAF(theme_snapshot);
  UNUSED_LEAF(translate);
  UNUSED_LEAF(header_draw);
  UNUSED_LEAF(footer_draw);
  UNUSED_LEAF(placeholder_draw);
  UNUSED_LEAF(popup_draw);
  UNUSED_LEAF(popup_progress);
  UNUSED_LEAF(icon_draw);
  UNUSED_LEAF(text_draw);
  UNUSED_LEAF(text_width);
  UNUSED_LEAF(line_height);
  UNUSED_LEAF(text_wrap);
  UNUSED_LEAF(text_truncate);
  UNUSED_LEAF(text_box_height);
  UNUSED_LEAF(region_size);
  UNUSED_LEAF(region_read);
  UNUSED_LEAF(region_write);
  UNUSED_LEAF(rounded_mask);
  UNUSED_LEAF(polygon_draw);
  UNUSED_LEAF(paint_scene);
  UNUSED_LEAF(refresh);
  UNUSED_LEAF(buffer_allocate);
  UNUSED_LEAF(buffer_release);
  table.translate = [](uint16_t key) -> XpAppText {
    unlocked();
    for (size_t index = 0; index < std::size(nativeKeys); ++index)
      if (key == static_cast<uint16_t>(nativeKeys[index]))
        return wire(I18N.get(static_cast<FixtureStrId>(index)));
    assert(false && "unexpected recovery translation"); std::abort();
  };
  table.popup_draw = [](XpAppText message) -> XpUiRect {
    unlocked(); GUI.drawPopup(activeFixture->renderer, nativeText(message).c_str());
    return {};
  };
  return table;
}();

static const XpAppRuntime runtime = [] {
  XpAppRuntime table{};
  static_assert(sizeof(XpAppRuntime) == 33 * sizeof(void (*)()));
  UNUSED_LEAF(input_mapped_snapshot);
  UNUSED_LEAF(input_shared_power_mode);
  UNUSED_LEAF(input_power_confirm);
  UNUSED_LEAF(input_remote_click);
  UNUSED_LEAF(loop_settings);
  UNUSED_LEAF(display_geometry);
  UNUSED_LEAF(header_back_clear);
  UNUSED_LEAF(now_ms);
  UNUSED_LEAF(delay_ms);
  UNUSED_LEAF(idle_wait);
  UNUSED_LEAF(yield_task);
  UNUSED_LEAF(power_saving);
  UNUSED_LEAF(tilt_update);
  UNUSED_LEAF(battery_capacity_step);
  UNUSED_LEAF(fading_fix);
  UNUSED_LEAF(memory_diagnostic);
  UNUSED_LEAF(console_service);
  UNUSED_LEAF(screenshot);
  UNUSED_LEAF(touch_feedback);
  UNUSED_LEAF(frontlight_present);
  UNUSED_LEAF(frontlight_state);
  UNUSED_LEAF(frontlight_set);
  UNUSED_LEAF(settings_save);
  UNUSED_LEAF(ble_tick);
  UNUSED_LEAF(ble_before_screen_change);
  UNUSED_LEAF(sleep_enter);
  UNUSED_LEAF(diagnostic);
  UNUSED_LEAF(render_lock);
  UNUSED_LEAF(render_try_lock);
  UNUSED_LEAF(render_unlock);
  UNUSED_LEAF(model_lock);
  UNUSED_LEAF(model_unlock);
  UNUSED_LEAF(notify_render);
  table.model_lock = [] { leaf(); ++modelDepth; };
  table.model_unlock = [] { assert(modelDepth == 1); --modelDepth; };
  table.render_lock = []() -> void* {
    unlocked(); return new RenderLock;
  };
  table.render_unlock = [](void* value) {
    locked(); assert(value); delete static_cast<RenderLock*>(value); unlocked();
  };
  table.delay_ms = [](uint32_t value) { unlocked(); delay(value); };
  return table;
}();

static XpUiReaderRenderServices renderServices{
  nullptr,
  &Unexpected<uint32_t (*)(void*, uint8_t)>::call,
  [](void* context, const XpUiReaderRenderOp* op) -> uint32_t {
    locked(); assert(context == activeFixture && op);
    assert(op->kind == XP_UI_READER_RENDER_CLEAR && op->value == 255);
    assert(!op->buffer && !op->reserved && !op->y && !op->rows && !op->offset && !op->mode);
    activeFixture->renderer.clearScreen();
    return 0;
  }
};

static const XpEpubLeaves leaves = [] {
  XpEpubLeaves table{};
  static_assert(sizeof(XpEpubLeaves) == 180 * sizeof(void (*)()));
  UNUSED_LEAF(resources_open);
  UNUSED_LEAF(resources_close);
  UNUSED_LEAF(render_services);
  UNUSED_LEAF(render_bind_page);
  UNUSED_LEAF(render_finish_page);
  UNUSED_LEAF(render_margins);
  UNUSED_LEAF(render_geometry);
  UNUSED_LEAF(render_restore_bw);
  UNUSED_LEAF(render_discard_bw);
  UNUSED_LEAF(render_orientation);
  UNUSED_LEAF(loan_count);
  UNUSED_LEAF(strip_release_idle);
  UNUSED_LEAF(progress_to_reader);
  UNUSED_LEAF(progress_to_saved);
  UNUSED_LEAF(progress_save);
  UNUSED_LEAF(page_close);
  UNUSED_LEAF(page_info);
  UNUSED_LEAF(page_element);
  UNUSED_LEAF(page_word);
  UNUSED_LEAF(metadata_assign);
  UNUSED_LEAF(metadata_clear);
  UNUSED_LEAF(metadata_footnotes);
  UNUSED_LEAF(metadata_links);
  UNUSED_LEAF(metadata_footnote);
  UNUSED_LEAF(metadata_link);
  UNUSED_LEAF(paint_dither);
  UNUSED_LEAF(paint_pixel);
  UNUSED_LEAF(paint_status);
  UNUSED_LEAF(image_clear_failures);
  UNUSED_LEAF(image_release_cache);
  UNUSED_LEAF(fonts_ensure_loaded);
  UNUSED_LEAF(fonts_release_caches);
  UNUSED_LEAF(fonts_prewarm_begin);
  UNUSED_LEAF(fonts_prewarm_finish_scan);
  UNUSED_LEAF(fonts_prewarm_drop);
  UNUSED_LEAF(fonts_take_corrupt_notice);
  UNUSED_LEAF(font_sizes);
  UNUSED_LEAF(settings_snapshot);
  UNUSED_LEAF(settings_render_spec);
  UNUSED_LEAF(settings_orientation);
  UNUSED_LEAF(settings_font_size);
  UNUSED_LEAF(settings_line_spacing);
  UNUSED_LEAF(settings_alignment);
  UNUSED_LEAF(settings_focus_reading);
  UNUSED_LEAF(settings_inversion);
  UNUSED_LEAF(settings_ble_enabled);
  UNUSED_LEAF(settings_save);
  UNUSED_LEAF(input_services);
  UNUSED_LEAF(input_facts);
  UNUSED_LEAF(input_pressed);
  UNUSED_LEAF(input_screen_long);
  UNUSED_LEAF(input_screen_tap);
  UNUSED_LEAF(input_raw_contact);
  UNUSED_LEAF(render_busy);
  UNUSED_LEAF(file_open_read);
  UNUSED_LEAF(file_open_write);
  UNUSED_LEAF(file_read);
  UNUSED_LEAF(file_write);
  UNUSED_LEAF(file_close);
  UNUSED_LEAF(path_remove);
  UNUSED_LEAF(path_rename);
  UNUSED_LEAF(path_mkdir);
  UNUSED_LEAF(native_path_hash);
  UNUSED_LEAF(heap_snapshot);
  UNUSED_LEAF(radio_status);
  UNUSED_LEAF(radio_before_build);
  UNUSED_LEAF(radio_settle_start);
  UNUSED_LEAF(radio_holds_heap);
  UNUSED_LEAF(radio_after_paint);
  UNUSED_LEAF(radio_switch_off);
  UNUSED_LEAF(radio_request_connect);
  UNUSED_LEAF(trusted_now);
  UNUSED_LEAF(trusted_sync);
  UNUSED_LEAF(wifi_connected);
  UNUSED_LEAF(wifi_disconnect);
  UNUSED_LEAF(restart_reader);
  UNUSED_LEAF(child_menu);
  UNUSED_LEAF(child_chapter);
  UNUSED_LEAF(child_percent);
  UNUSED_LEAF(child_text_settings);
  UNUSED_LEAF(child_footnote);
  UNUSED_LEAF(child_dictionary);
  UNUSED_LEAF(child_selection);
  UNUSED_LEAF(child_bookmarks);
  UNUSED_LEAF(child_clippings);
  UNUSED_LEAF(child_qr);
  UNUSED_LEAF(child_wifi);
  UNUSED_LEAF(child_sync);
  UNUSED_LEAF(toolbar_open);
  UNUSED_LEAF(toolbar_close);
  UNUSED_LEAF(toolbar_begin);
  UNUSED_LEAF(toolbar_paint);
  UNUSED_LEAF(toolbar_route);
  UNUSED_LEAF(toolbar_scroll);
  UNUSED_LEAF(toolbar_visible_rows);
  UNUSED_LEAF(popup_open);
  UNUSED_LEAF(popup_close);
  UNUSED_LEAF(popup_active);
  UNUSED_LEAF(popup_show);
  UNUSED_LEAF(popup_dismiss);
  UNUSED_LEAF(popup_input);
  UNUSED_LEAF(popup_paint);
  UNUSED_LEAF(popup_process_render);
  UNUSED_LEAF(end_open);
  UNUSED_LEAF(end_close);
  UNUSED_LEAF(end_load);
  UNUSED_LEAF(end_menu_active);
  UNUSED_LEAF(end_input);
  UNUSED_LEAF(end_paint);
  UNUSED_LEAF(bookmarks_open);
  UNUSED_LEAF(bookmarks_close);
  UNUSED_LEAF(bookmarks_clear);
  UNUSED_LEAF(bookmarks_load);
  UNUSED_LEAF(bookmarks_save);
  UNUSED_LEAF(bookmarks_count);
  UNUSED_LEAF(bookmarks_row);
  UNUSED_LEAF(bookmarks_retain);
  UNUSED_LEAF(bookmarks_insert_front);
  UNUSED_LEAF(clippings_load);
  UNUSED_LEAF(clippings_unload);
  UNUSED_LEAF(clippings_count);
  UNUSED_LEAF(clippings_row);
  UNUSED_LEAF(clippings_remove);
  UNUSED_LEAF(clippings_insert);
  UNUSED_LEAF(clippings_export);
  UNUSED_LEAF(clippings_move_book);
  UNUSED_LEAF(recents_remove);
  UNUSED_LEAF(recents_add);
  UNUSED_LEAF(recents_update_path);
  UNUSED_LEAF(state_open_path);
  UNUSED_LEAF(state_set_open_path);
  UNUSED_LEAF(state_reset_reader_count);
  UNUSED_LEAF(state_save);
  UNUSED_LEAF(has_sync_credentials);
  UNUSED_LEAF(has_exit_subscriber);
  UNUSED_LEAF(has_session_subscriber);
  UNUSED_LEAF(document_id);
  UNUSED_LEAF(emit_open);
  UNUSED_LEAF(emit_exit);
  UNUSED_LEAF(emit_session);
  UNUSED_LEAF(log);
  UNUSED_LEAF(page_word_font);
  UNUSED_LEAF(page_line_font);
  UNUSED_LEAF(page_word_y_offset);
  UNUSED_LEAF(page_line_ruby_shift);
  UNUSED_LEAF(font_text_advance);
  UNUSED_LEAF(log_enabled);
  UNUSED_LEAF(toolbar_reset_navigation);
  UNUSED_LEAF(input_snapshot);
  UNUSED_LEAF(page_render);
  UNUSED_LEAF(protection_open);
  UNUSED_LEAF(protection_close);
  UNUSED_LEAF(protection_encrypted);
  UNUSED_LEAF(protection_size);
  UNUSED_LEAF(protection_stream);
  UNUSED_LEAF(file_seek);
  UNUSED_LEAF(file_size);
  UNUSED_LEAF(path_remove_tree);
  UNUSED_LEAF(cache_lock);
  UNUSED_LEAF(cache_unlock);
  UNUSED_LEAF(scratch_claim);
  UNUSED_LEAF(scratch_release);
  UNUSED_LEAF(inflate_oom_log);
  UNUSED_LEAF(build_metrics);
  UNUSED_LEAF(build_allocator);
  UNUSED_LEAF(font_variant);
  UNUSED_LEAF(font_line_height);
  UNUSED_LEAF(font_ready);
  UNUSED_LEAF(font_codepoint);
  UNUSED_LEAF(hyphen_language);
  UNUSED_LEAF(file_position);
  UNUSED_LEAF(file_flush);
  UNUSED_LEAF(image_supported);
  UNUSED_LEAF(image_dimensions);
  UNUSED_LEAF(page_load);
  UNUSED_LEAF(framebuffer_lend);
  UNUSED_LEAF(framebuffer_restore);
  table.render_services = [](void* root) -> const XpUiReaderRenderServices* {
    locked(); assert(root == activeFixture); renderServices.context = root; return &renderServices;
  };
  table.popup_active = [](void* popup) -> uint8_t {
    locked(); assert(popup == &activeFixture->loadFailurePopup);
    return activeFixture->loadFailurePopup.isActive();
  };
  table.popup_process_render = [](void* popup) {
    locked(); assert(popup == &activeFixture->loadFailurePopup);
    activeFixture->loadFailurePopup.processRender(activeFixture->renderer, activeFixture->mappedInput);
  };
  table.popup_show = [](void* popup, XpAppText title, XpAppText message, uint32_t count, int32_t selected,
                        uint8_t (*label)(void*, uint32_t, XpAppTextSink, void*), void* labelContext,
                        void (*choose)(void*, int32_t), void* choiceContext) {
    locked(); assert(popup == &activeFixture->loadFailurePopup && !title.len);
    assert(count >= 1 && count <= 2 && selected == 0 && label && choose);
    assert(choiceContext == activeFixture->owner);
    std::vector<std::string> labels;
    labels.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
      struct Label { std::string value; unsigned calls = 0; } result;
      const auto accepted = label(labelContext, index, [](void* opaque, XpAppText value) -> uint8_t {
        locked(); auto& result = *static_cast<Label*>(opaque);
        assert(result.calls++ == 0); result.value = nativeText(value); return 1;
      }, &result);
      locked(); assert(accepted == 1 && result.calls == 1);
      labels.push_back(std::move(result.value));
    }
    const char* options[2]{};
    for (uint32_t index = 0; index < count; ++index) options[index] = labels[index].c_str();
    // The physical popup copies labels now and retains only the production continuation.
    activeFixture->loadFailurePopup.showMessage(nativeText(title).c_str(), nativeText(message).c_str(),
      options, count, selected, [choose, choiceContext](int index) {
        unlocked(); assert(choiceContext == activeFixture->owner);
        choose(choiceContext, index); unlocked();
      });
  };
  table.child_wifi = []() -> void* {
    unlocked(); assert(!activeFixture->child);
    return makeUniqueNoThrow<WifiSelectionActivity>(activeFixture->renderer, activeFixture->mappedInput).release();
  };
  table.wifi_connected = []() -> uint8_t { unlocked(); return WiFi.status() == WL_CONNECTED; };
  table.trusted_sync = [](uint32_t timeout) -> uint8_t { unlocked(); return trustedtime::syncNow(timeout); };
  table.wifi_disconnect = [](uint8_t off) { unlocked(); WiFi.disconnect(off != 0); };
  table.restart_reader = [] { unlocked(); silentRestartToReader(); };
  table.state_set_open_path = [](XpAppText path) { unlocked(); APP_STATE.openEpubPath = nativeText(path); };
  table.state_save = [] { unlocked(); APP_STATE.saveToFile(); };
  table.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    unlocked(); assert(severity == 1 && nativeText(origin) == "ERS");
    LOG_ERR(nativeText(origin).c_str(), nativeText(message).c_str());
  };
    UNUSED_LEAF(book_facade_open);
    UNUSED_LEAF(book_facade_close);
    UNUSED_LEAF(book_set_extractor);
  return table;
}();
#undef UNUSED_LEAF
}  // namespace recovery_adapter

void EpubReaderActivity::initialize(uint8_t failure) {
  using namespace recovery_adapter;
  assert(!owner && !activeFixture && modelDepth == 0);
  activeFixture = this;
  XpEpubTestRecoverySeed seed{};
  seed.native = this;
  seed.popup = &loadFailurePopup;
  seed.path = wire(bookPath.c_str());
  seed.failure = failure;
  seed.context = this;
  seed.finish = [](void* value) { context(value); activeFixture->finish(); };
  seed.update = [](void* value) { context(value); activeFixture->requestUpdate(); };
  seed.reopen = [](void* value, XpAppText path) {
    context(value); assert(nativeText(path) == activeFixture->bookPath);
    activityManager.goToReader(nativeText(path));
  };
  seed.push = [](void* value, void* child) {
    context(value); assert(child && !activeFixture->child);
    auto* fixture = activeFixture;
    fixture->startActivityForResult(std::unique_ptr<WifiSelectionActivity>(static_cast<WifiSelectionActivity*>(child)),
      [fixture](const ActivityResult& result) {
        context(fixture); assert(!fixture->child);
        xp_epub_test_recovery_wifi_result(fixture->owner, result.isCancelled);
        unlocked();
      });
  };
  owner = xp_epub_test_recovery_create(&seed, &leaves, &runtime, &resources);
  assert(owner && modelDepth == 0);
}
void EpubReaderActivity::seedFailure(uint8_t failure) {
  recovery_adapter::unlocked(); xp_epub_test_recovery_seed_failure(owner, failure);
}
void EpubReaderActivity::seedPath() {
  recovery_adapter::unlocked();
  assert(xp_epub_test_recovery_seed_path(owner, recovery_adapter::wire(bookPath.c_str())) == 1);
}
EpubReaderActivity::~EpubReaderActivity() {
  using namespace recovery_adapter;
  if (!owner) return;
  context(this);
  // Dismiss retained continuations before releasing their owner. A retry child is
  // still destroyed here, after the historical final state line.
  loadFailurePopup.callback = {};
  childResult = {};
  child.reset();
  xp_epub_test_recovery_destroy(owner);
  owner = nullptr;
  assert(modelDepth == 0 && locks == 0);
  activeFixture = nullptr;
}
bool EpubReaderActivity::handleLoadFailure() {
  recovery_adapter::unlocked(); return xp_epub_test_recovery_handle(owner) != 0;
}
void EpubReaderActivity::beginLoanTimeSync() {
  recovery_adapter::unlocked(); xp_epub_test_recovery_begin(owner);
}
bool EpubReaderActivity::renderLoadFailure() {
  if (!owner) initialize();
  recovery_adapter::locked(); return xp_epub_test_recovery_render(owner) != 0;
}
