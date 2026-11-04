#include <cstring>

namespace base_owner_adapter {
static ReaderActivity* active;
static EndOfBookOptions* liveEnd;
static bool teardown;
static const std::string* borrowedPath;
static const std::string* borrowedDocument;
static ReaderSession sessionSnapshot;
static unsigned documentResets;
static unsigned pathCallbacks, homeCalls, navigateCalls, updateCalls, returnCalls;
static int lockToken;
static void leaf() { assert(active && modelDepth == 0); }
static void unlocked() { leaf(); assert(lockDepth == 0); }
static void locked() { leaf(); assert(lockDepth == 1); }
static void context(void* value) { leaf(); assert(value == active); }
static XpAppText wire(const std::string& value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
static std::string_view view(XpAppText value) {
  assert(value.data || !value.len);
  return {reinterpret_cast<const char*>(value.data), value.len};
}
static void path(XpAppText value) { assert(view(value) == active->bookPath); }
static bool tracked(const std::string& value) {
  for (const auto& slot : allocations) if (slot.pointer == value.data()) return true;
  return false;
}
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) { leaf(); assert(false && "unexpected base-owner leaf"); std::abort(); }
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
  table.path_exists = [](XpAppText value) -> uint8_t {
    unlocked(); path(value); assert(value.data[value.len] == 0);
    return Storage.exists(reinterpret_cast<const char*>(value.data));
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
  table.render_lock = []() -> void* { unlocked(); ++lockDepth; event("lock"); return &lockToken; };
  table.render_unlock = [](void* token) { locked(); assert(token == &lockToken); --lockDepth; event("unlock"); };
  table.touch_feedback = [](uint8_t skip) { unlocked(); haptic_feedback::touchAction(skip != 0); };
  return table;
}();

static XpUiReaderInputServices inputServices{
  nullptr,
  &Unexpected<uint8_t (*)(void*, uint32_t, uint8_t, uint32_t)>::call,
  [](void* value, uint8_t query) -> uint32_t {
    context(value); unlocked(); assert(query == XP_UI_READER_INPUT_HELD_MS);
    return active->mappedInput.getHeldTime();
  },
  &Unexpected<uint8_t (*)(void*, int32_t*, int32_t*)>::call,
};
static XpUiReaderRenderServices renderServices{
  nullptr,
  &Unexpected<uint32_t (*)(void*, uint8_t)>::call,
  [](void* value, const XpUiReaderRenderOp* op) -> uint32_t {
    context(value); locked(); assert(op && !op->buffer && !op->reserved && !op->y && !op->rows && !op->offset);
    if (op->kind == XP_UI_READER_RENDER_CLEAR) {
      assert(op->value == 255 && op->mode == 0); active->renderer.clearScreen();
    } else {
      assert(op->kind == XP_UI_READER_RENDER_DISPLAY && !op->value && op->mode == XP_UI_FAST);
      active->renderer.displayBuffer();
    }
    return 0;
  }
};

static const LegacyEpubLeaves leaves = [] {
  LegacyEpubLeaves table{};
  static_assert(sizeof(XpEpubLeaves) == 180 * sizeof(void (*)()));
  UNUSED_LEAF(physical.resources_open);
  UNUSED_LEAF(physical.resources_close);
  UNUSED_LEAF(physical.render_services);
  UNUSED_LEAF(physical.render_bind_page);
  UNUSED_LEAF(physical.render_finish_page);
  UNUSED_LEAF(physical.render_margins);
  UNUSED_LEAF(physical.render_geometry);
  UNUSED_LEAF(physical.render_restore_bw);
  UNUSED_LEAF(physical.render_discard_bw);
  UNUSED_LEAF(physical.render_orientation);
  UNUSED_LEAF(physical.loan_count);
  UNUSED_LEAF(physical.strip_release_idle);
  UNUSED_LEAF(book.book_open);
  UNUSED_LEAF(book.book_load);
  UNUSED_LEAF(book.book_close);
  UNUSED_LEAF(book.book_size);
  UNUSED_LEAF(book.book_spine_count);
  UNUSED_LEAF(book.book_toc_count);
  UNUSED_LEAF(book.book_path);
  UNUSED_LEAF(book.book_cache_path);
  UNUSED_LEAF(book.book_title);
  UNUSED_LEAF(book.book_author);
  UNUSED_LEAF(book.book_language);
  UNUSED_LEAF(book.book_spine);
  UNUSED_LEAF(book.book_toc);
  UNUSED_LEAF(book.book_toc_for_spine);
  UNUSED_LEAF(book.book_cumulative_size);
  UNUSED_LEAF(book.book_spine_for_size);
  UNUSED_LEAF(book.book_text_reference);
  UNUSED_LEAF(book.book_progress);
  UNUSED_LEAF(book.book_resolve_href);
  UNUSED_LEAF(book.book_setup_cache);
  UNUSED_LEAF(book.book_clear_cache);
  UNUSED_LEAF(book.book_install_extractor);
  UNUSED_LEAF(book.book_clear_extractor);
  UNUSED_LEAF(book.book_protection_error);
  UNUSED_LEAF(physical.progress_to_reader);
  UNUSED_LEAF(physical.progress_to_saved);
  UNUSED_LEAF(physical.progress_save);
  UNUSED_LEAF(book.section.section_open);
  UNUSED_LEAF(book.section.section_close);
  UNUSED_LEAF(book.section.section_page_count);
  UNUSED_LEAF(book.section.section_estimated_pages);
  UNUSED_LEAF(book.section.section_building);
  UNUSED_LEAF(book.section.section_complete);
  UNUSED_LEAF(book.section.section_partial);
  UNUSED_LEAF(book.section.section_load_cache);
  UNUSED_LEAF(book.section.section_create_cache);
  UNUSED_LEAF(book.section.section_start_build);
  UNUSED_LEAF(book.section.section_build_more);
  UNUSED_LEAF(book.section.section_abandon_build);
  UNUSED_LEAF(book.section.section_clear_cache);
  UNUSED_LEAF(book.section.section_html_cached);
  UNUSED_LEAF(book.section.section_find_anchor);
  UNUSED_LEAF(book.section.section_cached_anchor);
  UNUSED_LEAF(book.section.section_toc_for_page);
  UNUSED_LEAF(book.section.section_page_for_offset);
  UNUSED_LEAF(book.section.section_offset_for_page);
  UNUSED_LEAF(book.section.section_paragraph_for_page);
  UNUSED_LEAF(book.section.section_reached_offset);
  UNUSED_LEAF(book.section.section_load_page);
  UNUSED_LEAF(book.section.section_page_text);
  UNUSED_LEAF(physical.page_close);
  UNUSED_LEAF(physical.page_info);
  UNUSED_LEAF(physical.page_element);
  UNUSED_LEAF(physical.page_word);
  UNUSED_LEAF(physical.metadata_assign);
  UNUSED_LEAF(physical.metadata_clear);
  UNUSED_LEAF(physical.metadata_footnotes);
  UNUSED_LEAF(physical.metadata_links);
  UNUSED_LEAF(physical.metadata_footnote);
  UNUSED_LEAF(physical.metadata_link);
  UNUSED_LEAF(physical.paint_dither);
  UNUSED_LEAF(physical.paint_pixel);
  UNUSED_LEAF(physical.paint_status);
  UNUSED_LEAF(physical.image_clear_failures);
  UNUSED_LEAF(physical.image_release_cache);
  UNUSED_LEAF(physical.fonts_ensure_loaded);
  UNUSED_LEAF(physical.fonts_release_caches);
  UNUSED_LEAF(physical.fonts_prewarm_begin);
  UNUSED_LEAF(physical.fonts_prewarm_finish_scan);
  UNUSED_LEAF(physical.fonts_prewarm_drop);
  UNUSED_LEAF(physical.fonts_take_corrupt_notice);
  UNUSED_LEAF(physical.font_sizes);
  UNUSED_LEAF(physical.settings_snapshot);
  UNUSED_LEAF(physical.settings_render_spec);
  UNUSED_LEAF(physical.settings_orientation);
  UNUSED_LEAF(physical.settings_font_size);
  UNUSED_LEAF(physical.settings_line_spacing);
  UNUSED_LEAF(physical.settings_alignment);
  UNUSED_LEAF(physical.settings_focus_reading);
  UNUSED_LEAF(physical.settings_inversion);
  UNUSED_LEAF(physical.settings_ble_enabled);
  UNUSED_LEAF(physical.settings_save);
  UNUSED_LEAF(physical.input_services);
  UNUSED_LEAF(physical.input_facts);
  UNUSED_LEAF(physical.input_pressed);
  UNUSED_LEAF(physical.input_screen_long);
  UNUSED_LEAF(physical.input_screen_tap);
  UNUSED_LEAF(physical.input_raw_contact);
  UNUSED_LEAF(physical.render_busy);
  UNUSED_LEAF(physical.file_open_read);
  UNUSED_LEAF(physical.file_open_write);
  UNUSED_LEAF(physical.file_read);
  UNUSED_LEAF(physical.file_write);
  UNUSED_LEAF(physical.file_close);
  UNUSED_LEAF(physical.path_remove);
  UNUSED_LEAF(physical.path_rename);
  UNUSED_LEAF(physical.path_mkdir);
  UNUSED_LEAF(physical.native_path_hash);
  UNUSED_LEAF(physical.heap_snapshot);
  UNUSED_LEAF(physical.radio_status);
  UNUSED_LEAF(physical.radio_before_build);
  UNUSED_LEAF(physical.radio_settle_start);
  UNUSED_LEAF(physical.radio_holds_heap);
  UNUSED_LEAF(physical.radio_after_paint);
  UNUSED_LEAF(physical.radio_switch_off);
  UNUSED_LEAF(physical.radio_request_connect);
  UNUSED_LEAF(physical.trusted_now);
  UNUSED_LEAF(physical.trusted_sync);
  UNUSED_LEAF(physical.wifi_connected);
  UNUSED_LEAF(physical.wifi_disconnect);
  UNUSED_LEAF(physical.restart_reader);
  UNUSED_LEAF(physical.child_menu);
  UNUSED_LEAF(physical.child_chapter);
  UNUSED_LEAF(physical.child_percent);
  UNUSED_LEAF(physical.child_text_settings);
  UNUSED_LEAF(physical.child_footnote);
  UNUSED_LEAF(physical.child_dictionary);
  UNUSED_LEAF(physical.child_selection);
  UNUSED_LEAF(physical.child_bookmarks);
  UNUSED_LEAF(physical.child_clippings);
  UNUSED_LEAF(physical.child_qr);
  UNUSED_LEAF(physical.child_wifi);
  UNUSED_LEAF(physical.child_sync);
  UNUSED_LEAF(physical.toolbar_open);
  UNUSED_LEAF(physical.toolbar_close);
  UNUSED_LEAF(physical.toolbar_begin);
  UNUSED_LEAF(physical.toolbar_paint);
  UNUSED_LEAF(physical.toolbar_route);
  UNUSED_LEAF(physical.toolbar_scroll);
  UNUSED_LEAF(physical.toolbar_visible_rows);
  UNUSED_LEAF(physical.popup_open);
  UNUSED_LEAF(physical.popup_close);
  UNUSED_LEAF(physical.popup_active);
  UNUSED_LEAF(physical.popup_show);
  UNUSED_LEAF(physical.popup_dismiss);
  UNUSED_LEAF(physical.popup_input);
  UNUSED_LEAF(physical.popup_paint);
  UNUSED_LEAF(physical.popup_process_render);
  UNUSED_LEAF(physical.end_open);
  UNUSED_LEAF(physical.end_close);
  UNUSED_LEAF(physical.end_load);
  UNUSED_LEAF(physical.end_menu_active);
  UNUSED_LEAF(physical.end_input);
  UNUSED_LEAF(physical.end_paint);
  UNUSED_LEAF(physical.bookmarks_open);
  UNUSED_LEAF(physical.bookmarks_close);
  UNUSED_LEAF(physical.bookmarks_clear);
  UNUSED_LEAF(book.bookmarks_load);
  UNUSED_LEAF(book.bookmarks_save);
  UNUSED_LEAF(physical.bookmarks_count);
  UNUSED_LEAF(physical.bookmarks_row);
  UNUSED_LEAF(physical.bookmarks_retain);
  UNUSED_LEAF(physical.bookmarks_insert_front);
  UNUSED_LEAF(book.clippings_load);
  UNUSED_LEAF(physical.clippings_unload);
  UNUSED_LEAF(physical.clippings_count);
  UNUSED_LEAF(physical.clippings_row);
  UNUSED_LEAF(physical.clippings_remove);
  UNUSED_LEAF(physical.clippings_insert);
  UNUSED_LEAF(physical.clippings_export);
  UNUSED_LEAF(physical.clippings_move_book);
  UNUSED_LEAF(physical.recents_remove);
  UNUSED_LEAF(book.recents_add);
  UNUSED_LEAF(physical.recents_update_path);
  UNUSED_LEAF(physical.state_open_path);
  UNUSED_LEAF(physical.state_set_open_path);
  UNUSED_LEAF(physical.state_reset_reader_count);
  UNUSED_LEAF(physical.state_save);
  UNUSED_LEAF(physical.has_sync_credentials);
  UNUSED_LEAF(physical.has_exit_subscriber);
  UNUSED_LEAF(physical.has_session_subscriber);
  UNUSED_LEAF(physical.document_id);
  UNUSED_LEAF(physical.emit_open);
  UNUSED_LEAF(physical.emit_exit);
  UNUSED_LEAF(physical.emit_session);
  UNUSED_LEAF(physical.log);
  UNUSED_LEAF(physical.page_word_font);
  UNUSED_LEAF(physical.page_line_font);
  UNUSED_LEAF(physical.page_word_y_offset);
  UNUSED_LEAF(physical.page_line_ruby_shift);
  UNUSED_LEAF(physical.font_text_advance);
  UNUSED_LEAF(physical.log_enabled);
  UNUSED_LEAF(physical.toolbar_reset_navigation);
  UNUSED_LEAF(physical.input_snapshot);
  UNUSED_LEAF(physical.page_render);
  UNUSED_LEAF(physical.protection_open);
  UNUSED_LEAF(physical.protection_close);
  UNUSED_LEAF(physical.protection_encrypted);
  UNUSED_LEAF(physical.protection_size);
  UNUSED_LEAF(physical.protection_stream);
  UNUSED_LEAF(physical.file_seek);
  UNUSED_LEAF(physical.file_size);
  UNUSED_LEAF(physical.path_remove_tree);
  UNUSED_LEAF(physical.cache_lock);
  UNUSED_LEAF(physical.cache_unlock);
  UNUSED_LEAF(physical.scratch_claim);
  UNUSED_LEAF(physical.scratch_release);
  UNUSED_LEAF(physical.inflate_oom_log);
  UNUSED_LEAF(physical.build_metrics);
  UNUSED_LEAF(physical.build_allocator);
  UNUSED_LEAF(physical.font_variant);
  UNUSED_LEAF(physical.font_line_height);
  UNUSED_LEAF(physical.font_ready);
  UNUSED_LEAF(physical.font_codepoint);
  UNUSED_LEAF(physical.hyphen_language);
  UNUSED_LEAF(physical.file_position);
  UNUSED_LEAF(physical.file_flush);
  UNUSED_LEAF(physical.image_supported);
  UNUSED_LEAF(physical.image_dimensions);
  UNUSED_LEAF(physical.page_load);
  UNUSED_LEAF(physical.framebuffer_lend);
  UNUSED_LEAF(physical.framebuffer_restore);
  table.physical.heap_snapshot = [](XpEpubHeap* out) {
    unlocked(); assert(out);
    // Preserve the original fixture's compiler evaluation of the two arguments.
    const auto capture = [out](unsigned free, unsigned largest) { *out = {free, largest}; };
    capture(ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  };
  table.physical.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    leaf(); assert(severity == 1 || severity == 2);
    std::printf("%u %.*s %.*s\n", eventId++, int(origin.len), reinterpret_cast<const char*>(origin.data),
                int(message.len), reinterpret_cast<const char*>(message.data));
  };
  table.physical.log_enabled = [](uint8_t severity) -> uint8_t { leaf(); assert(severity <= 2); return 1; };
  table.physical.state_open_path = [](XpAppTextSink sink, void* value) -> uint8_t {
    unlocked(); assert(sink); return sink(value, wire(APP_STATE.openEpubPath));
  };
  table.physical.state_set_open_path = [](XpAppText value) {
    unlocked(); APP_STATE.openEpubPath.assign(reinterpret_cast<const char*>(value.data), value.len);
  };
  table.physical.state_save = [] { unlocked(); APP_STATE.saveToFile(); };
  table.physical.state_reset_reader_count = [] { unlocked(); APP_STATE.readerActivityLoadCount = 0; };
  table.physical.fonts_ensure_loaded = [] { unlocked(); sdFontSystem.ensureLoaded(active->renderer); };
  table.physical.fonts_release_caches = [] {
    unlocked(); if (auto* cache = active->renderer.getFontCacheManager()) cache->releaseSdFontCaches();
  };
  table.physical.render_orientation = [](uint8_t value) {
    unlocked(); assert(value == 0); active->renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  };
  table.book.recents_add = [](void* book) {
    unlocked(); assert(book == active);
    RECENT_BOOKS.addBook(active->bookPath, active->getBookTitle(), active->getBookAuthor(), active->getBookThumbBmpPath());
  };
  table.physical.emit_open = [](XpAppText value) {
    unlocked(); path(value);
    const pluginevents::Var vars[] = {{"book", active->bookPath.c_str()}};
    pluginevents::emit(pluginevents::Event::ReaderOpen, vars, 1);
  };
  table.physical.has_exit_subscriber = []() -> uint8_t { unlocked(); return pluginevents::anySubscriber(pluginevents::Event::ReaderExit); };
  table.physical.has_session_subscriber = []() -> uint8_t { unlocked(); return pluginevents::anySubscriber(pluginevents::Event::ReaderSession); };
  table.physical.emit_exit = [](XpAppText value, int32_t percent) {
    unlocked(); path(value);
    char number[8]; std::snprintf(number, sizeof(number), "%d", percent);
    const pluginevents::Var vars[] = {{"book", active->bookPath.c_str()}, {"percent", number}};
    pluginevents::emit(pluginevents::Event::ReaderExit, vars, 2);
  };
  table.physical.document_id = [](XpAppText value, XpAppTextSink sink, void* context) -> uint8_t {
    unlocked(); path(value); assert(!borrowedDocument && sink);
    const std::string document = KOReaderDocumentId::calculate(active->bookPath);
    assert(tracked(document)); borrowedDocument = &document; documentResets = 0;
    const uint8_t result = sink(context, wire(document));
    unlocked(); assert(result == 1 && documentResets == 1 && tracked(document));
    borrowedDocument = nullptr;
    return result;
  };
  table.physical.emit_session = [](XpAppText value, XpAppText document, const XpEpubSessionEvent* row) {
    unlocked(); path(value); assert(borrowedDocument && row && tracked(*borrowedDocument));
    assert(document.data == reinterpret_cast<const uint8_t*>(borrowedDocument->data()));
    assert(document.len == borrowedDocument->size());
    // The historical compositional snapshot has scalar getters. Observe them at
    // their original emission point, checking every real event field first.
    assert(row->start_time == 1234 && row->end_time == 1256 && row->duration_seconds == 22);
    assert(row->start_progress == 111 && row->end_progress == 222);
    char start[24], end[24], duration[16], startProgress[8], endProgress[8];
    std::snprintf(start, sizeof(start), "%lld", static_cast<long long>(sessionSnapshot.startTime()));
    std::snprintf(end, sizeof(end), "%lld", static_cast<long long>(sessionSnapshot.endTime()));
    std::snprintf(duration, sizeof(duration), "%lu", static_cast<unsigned long>(sessionSnapshot.durationSeconds()));
    std::snprintf(startProgress, sizeof(startProgress), "%u", sessionSnapshot.startProgressBp());
    std::snprintf(endProgress, sizeof(endProgress), "%u", sessionSnapshot.endProgressBp());
    const pluginevents::Var vars[] = {{"book", active->bookPath.c_str()}, {"document", borrowedDocument->c_str()},
      {"start_time", start}, {"end_time", end}, {"duration_seconds", duration},
      {"start_progress_bp", startProgress}, {"end_progress_bp", endProgress}, {"progress_scale", "10000"}};
    pluginevents::emit(pluginevents::Event::ReaderSession, vars, 8);
  };
  table.physical.settings_snapshot = [](uint8_t resolveFont, XpEpubSettings* out) {
    unlocked(); assert(!resolveFont && out); *out = {}; out->long_button = SETTINGS.longPressButtonBehavior;
  };
  table.physical.input_services = []() -> const XpUiReaderInputServices* {
    unlocked(); inputServices.context = active; return &inputServices;
  };
  table.physical.render_services = [](void* root) -> const XpUiReaderRenderServices* {
    locked(); assert(root == active); renderServices.context = root; return &renderServices;
  };
  table.physical.end_open = []() -> void* {
    locked(); assert(!liveEnd); liveEnd = makeUniqueNoThrow<EndOfBookOptions>(active->renderer).release();
    return liveEnd;
  };
  table.physical.end_close = [](void* end) {
    leaf(); assert(end && end == liveEnd && (lockDepth == 1 || (teardown && lockDepth == 0)));
    delete liveEnd; liveEnd = nullptr;
  };
  table.physical.end_load = [](void* end, XpAppText value) {
    locked(); assert(end == liveEnd && end); path(value); liveEnd->loadOnce(active->bookPath);
  };
  table.physical.end_paint = [](void* end) {
    locked(); assert(end == liveEnd && end); liveEnd->render(active->renderer, active->mappedInput);
  };
  table.physical.end_menu_active = [](void* end) -> uint8_t {
    unlocked(); assert(end == liveEnd && end); return liveEnd->menuActive();
  };
  table.physical.end_input = [](void* end, void (*result)(void*, uint8_t, XpAppText), void* context) {
    unlocked(); assert(end == liveEnd && end && result && !borrowedPath);
    std::string selected;
    const auto action = liveEnd->handleMenuInput(active->mappedInput, &selected);
    assert(tracked(selected)); borrowedPath = &selected;
    result(context, static_cast<uint8_t>(action), wire(selected));
    unlocked(); assert(tracked(selected)); ++pathCallbacks;
    borrowedPath = nullptr;
  };
  UNUSED_LEAF(physical.book_facade_open);
  UNUSED_LEAF(physical.book_facade_close);
  table.physical.book_set_extractor = [](void* handle) {
    if (handle) leaves.book.book_install_extractor(handle);
    else leaves.book.book_clear_extractor();
  };
  return table;
}();
#undef UNUSED_LEAF
}  // namespace base_owner_adapter

void ReaderActivity::initializeOwner() {
  using namespace base_owner_adapter;
  assert(!owner && !active && !liveEnd && !modelDepth && !lockDepth);
  active = this;
  liveEnd = endOfBookOptions.release();
  XpEpubTestBaseSeed seed{};
  seed.native = seed.book = this;
  seed.end = liveEnd;
  seed.path = wire(bookPath);
  seed.counter = pagesUntilFullRefresh;
  seed.forced = forcedRefreshPending;
  seed.rendered = pageRendered.load();
  seed.remembered = bookRemembered;
  seed.end_ready = endOfBookOptionsReady.load();
  auto& hooks = seed.hooks;
  hooks.context = this;
  hooks.enter = [](void* value) { context(value); unlocked(); active->Activity::onEnter(); };
  hooks.exit = [](void* value) { context(value); unlocked(); active->Activity::onExit(); };
  hooks.orientation = [](void* value) { context(value); unlocked(); active->applyInitialOrientation(); };
  hooks.load = [](void* value) -> uint8_t { context(value); unlocked(); return active->loadBook(); };
  hooks.failure = [](void* value) -> uint8_t { context(value); unlocked(); return active->handleLoadFailure(); };
  hooks.at_end = [](void* value) -> uint8_t { context(value); assert(lockDepth <= 1); return active->isAtEndOfBook(); };
  hooks.return_end = [](void* value) { context(value); unlocked(); ++returnCalls; active->onReturnFromEndOfBook(); };
  hooks.end_rendered = [](void* value) { context(value); locked(); active->onEndOfBookRendered(); };
  hooks.back = [](void* value) -> uint8_t { context(value); unlocked(); return active->handleBackNavigation(); };
  hooks.format = [](void* value) -> uint8_t { context(value); unlocked(); return active->handleFormatInput(); };
  hooks.touch = [](void* value) -> XpUiReaderTurnInput {
    context(value); unlocked(); auto touch = ReaderUtils::detectTouchPageTurn(active->renderer, active->mappedInput);
    return {static_cast<uint32_t>(touch.heldMs), touch.prev, touch.next, 0, 0};
  };
  hooks.buttons = [](void* value) -> XpUiReaderTurnInput {
    context(value); unlocked(); auto [previous, next, tilt] = ReaderUtils::detectPageTurn(active->mappedInput);
    return {0, previous, next, tilt, 0};
  };
  hooks.turn = [](void* value, uint8_t forward) -> uint8_t { context(value); unlocked(); return active->pageTurn(forward != 0); };
  hooks.skip = [](void* value, int32_t amount) -> uint8_t { context(value); unlocked(); return active->skipPages(amount); };
  hooks.snapshot = [](void* value) -> XpUiReaderSession {
    context(value); unlocked(); sessionSnapshot = active->snapshotReaderSession();
    XpUiReaderSession session{};
    session.start_epoch = 1234; session.duration = 22; session.start_progress = 111; session.end_progress = 222;
    return session;
  };
  hooks.worthy = [](void* value) -> uint8_t { context(value); unlocked(); return sessionSnapshot.isEmitWorthy(); };
  hooks.reset = [](void* value) {
    context(value); unlocked();
    if (borrowedDocument) { assert(tracked(*borrowedDocument)); ++documentResets; }
    active->resetReaderSession();
  };
  hooks.complete = [](void* value) { context(value); locked(); active->completeReaderSession(); };
  hooks.mark = [](void* value) { context(value); locked(); active->markPageRendered(); };
  hooks.note = [](void* value, uint8_t forward, uint8_t changed) { context(value); unlocked(); active->notePageTurn(forward != 0, changed != 0); };
  hooks.force = [](void* value) { context(value); unlocked(); active->queueForcedRefresh(); };
  hooks.percent = [](void* value) -> int32_t { context(value); unlocked(); return active->getScreenshotInfo().progressPercent; };
  hooks.home = [](void* value) {
    context(value); unlocked(); if (borrowedPath) assert(tracked(*borrowedPath));
    ++homeCalls; active->onGoHome();
  };
  hooks.navigate = [](void* value, XpAppText selected) {
    context(value); unlocked(); assert(borrowedPath && tracked(*borrowedPath));
    assert(selected.data == reinterpret_cast<const uint8_t*>(borrowedPath->data()) && selected.len == borrowedPath->size());
    ++navigateCalls; activityManager.goToReader(*borrowedPath);
  };
  seed.finish = [](void* value) { context(value); unlocked(); active->finish(); };
  seed.update = [](void* value) {
    context(value); unlocked(); if (borrowedPath) assert(tracked(*borrowedPath));
    ++updateCalls; active->requestUpdate();
  };
  owner = xp_epub_test_base_create_legacy_book(&seed,&leaves.physical, &runtime, &resources, &leaves.book);
  assert(owner && !modelDepth);
}
void ReaderActivity::observeOwner() {
  using namespace base_owner_adapter;
  context(this); unlocked(); XpEpubTestBaseObservation result{};
  xp_epub_test_base_observe(owner, &result);
  assert(result.end == liveEnd);
  pagesUntilFullRefresh = result.counter; forcedRefreshPending = result.forced;
  pageRendered = result.rendered; bookRemembered = result.remembered; endOfBookOptionsReady = result.end_ready;
}
ReaderActivity::~ReaderActivity() {
  using namespace base_owner_adapter;
  if (!owner) return;
  context(this); unlocked(); assert(!borrowedDocument && !borrowedPath);
  teardown = true; xp_epub_test_base_destroy(owner); teardown = false;
  owner = nullptr; assert(!liveEnd && !modelDepth); active = nullptr;
}
void ReaderActivity::onEnter() { base_owner_adapter::unlocked(); xp_epub_test_base_enter(owner); }
void ReaderActivity::onExit() {
  base_owner_adapter::unlocked(); base_owner_adapter::teardown = true;
  xp_epub_test_base_exit(owner); base_owner_adapter::teardown = false;
}
void ReaderActivity::loop() { base_owner_adapter::unlocked(); xp_epub_test_base_loop(owner); }
void ReaderActivity::render(RenderLock&&) {
  base_owner_adapter::locked();
  assert(xp_epub_test_base_render(owner, [](void* value) {
    base_owner_adapter::context(value); base_owner_adapter::locked();
    static_cast<ReaderActivity*>(value)->renderBook();
  }, this) == 1);
}
void ReaderActivity::rememberBookOnceRendered() { base_owner_adapter::unlocked(); xp_epub_test_base_remember(owner); }
void ReaderActivity::clearEndOfBookOptionsIfNeeded() { base_owner_adapter::unlocked(); xp_epub_test_base_clear_end(owner); }
bool ReaderActivity::endOfBookMenuActive() const { base_owner_adapter::unlocked(); return xp_epub_test_base_end_active(owner); }
bool ReaderActivity::handleEndOfBookMenu(bool suppressed) { base_owner_adapter::unlocked(); return xp_epub_test_base_end_menu(owner, suppressed); }
bool ReaderActivity::handleEndOfBookPageTurn(bool previous, bool next) { base_owner_adapter::unlocked(); return xp_epub_test_base_end_turn(owner, previous, next); }
bool ReaderActivity::handleForcedRefresh() { base_owner_adapter::unlocked(); return xp_epub_test_base_force(owner); }
void ReaderActivity::flushReaderSession() { base_owner_adapter::unlocked(); xp_epub_test_base_flush(owner); }

namespace base_owner_adapter {
static int callbackCases() {
  recordAllocations = true; longCallbackPath = true;
  for (unsigned action = 0; action < 5; ++action) {
    ReaderActivity fixture;
    fixture.atEnd = true; fixture.endOfBookOptionsReady = true;
    fixture.endOfBookOptions = std::make_unique<EndOfBookOptions>(fixture.renderer);
    fixture.endOfBookOptions->action = static_cast<EndOfBookOptions::Action>(action);
    fixture.initializeOwner();
    pathCallbacks = homeCalls = navigateCalls = updateCalls = returnCalls = 0;
    const bool handled = fixture.handleEndOfBookMenu();
    assert(handled == (action != 0) && pathCallbacks == 1 && !borrowedPath);
    assert(homeCalls == unsigned(action == 3) && navigateCalls == unsigned(action == 2));
    assert(updateCalls == unsigned(action == 1 || action == 4) && returnCalls == unsigned(action == 4));
    fixture.observeOwner();
    assert(fixture.endOfBookOptionsReady && !modelDepth && !lockDepth);
  }
  longCallbackPath = false;
  std::puts("base owner callbacks: five 4096-byte paths stayed live through synchronous results");
  return 0;
}
}  // namespace base_owner_adapter
