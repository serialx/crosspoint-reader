#include "lifecycle_session_fixture.h"
#include <xp_ui.h>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace lifecycle_session {
struct Bridge;
static Bridge* active;
static unsigned modelDepth;
static HalFile* liveFile;
static unsigned created, destroyed, inputOwners, inputCalls, roomCalls, publications;
struct Room {
  void* context;
  uint32_t (*status)(void*);
  uint32_t (*now)(void*);
  void (*sleep)(void*, uint32_t);
};
static Room* room;
struct Bridge {
  EpubReaderActivity& fixture;
  void* owner = nullptr;
  bool input;
  bool publishing = false;
  bool destroying = false;
  unsigned publicationStep = 0;
  explicit Bridge(EpubReaderActivity&, bool);
  ~Bridge();
  void observe();
  void beginPublication() {
    assert(!publishing && fixture.section);
    publishing = true;
    publicationStep = 0;
  }
};
struct Active {
  Bridge* previous;
  explicit Active(Bridge& bridge) : previous(active) {
    assert(modelDepth == 0 && lockDepth == 0 && !liveFile);
    active = &bridge;
  }
  ~Active() {
    assert(modelDepth == 0 && lockDepth == 0 && !liveFile && !active->publishing);
    active = previous;
  }
};
static Bridge& bridge() { assert(active && modelDepth == 0); return *active; }
static EpubReaderActivity& fixture() { return bridge().fixture; }
static void leaf() { (void)bridge(); }
static void context(void* value) { assert(value == &fixture()); }
static Section& section(void* value) {
  assert(value && value == fixture().section.get());
  return *static_cast<Section*>(value);
}
static std::string text(XpAppText value) {
  assert(value.data || value.len == 0);
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}
static XpAppText wire(const std::string& value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
static void publication(unsigned step) {
  auto& b = bridge();
  assert(b.publishing && b.publicationStep++ == step);
  assert(lockDepth == (b.owner ? 1 : 0));
  if (step == 4) { b.publishing = false; ++publications; }
}
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) {
    leaf();
    assert(false && "unexpected lifecycle leaf");
    std::abort();
  }
};
#define UNUSED_LEAF(field) table.field = &Unexpected<decltype(table.field)>::call

static XpAppResources resourcesDefaults() {
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
  return table;
}

static XpAppRuntime runtimeDefaults() {
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
  return table;
}

static LegacyEpubLeaves leavesDefaults() {
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
    UNUSED_LEAF(physical.book_facade_open);
    UNUSED_LEAF(physical.book_facade_close);
    UNUSED_LEAF(physical.book_set_extractor);
  UNUSED_LEAF(book.book_author);
  UNUSED_LEAF(book.book_cache_path);
  UNUSED_LEAF(book.book_clear_cache);
  UNUSED_LEAF(book.book_clear_extractor);
  UNUSED_LEAF(book.book_close);
  UNUSED_LEAF(book.book_cumulative_size);
  UNUSED_LEAF(book.book_install_extractor);
  UNUSED_LEAF(book.book_language);
  UNUSED_LEAF(book.book_load);
  UNUSED_LEAF(book.book_open);
  UNUSED_LEAF(book.book_path);
  UNUSED_LEAF(book.book_progress);
  UNUSED_LEAF(book.book_protection_error);
  UNUSED_LEAF(book.book_resolve_href);
  UNUSED_LEAF(book.book_setup_cache);
  UNUSED_LEAF(book.book_size);
  UNUSED_LEAF(book.book_spine);
  UNUSED_LEAF(book.book_spine_count);
  UNUSED_LEAF(book.book_spine_for_size);
  UNUSED_LEAF(book.book_text_reference);
  UNUSED_LEAF(book.book_title);
  UNUSED_LEAF(book.book_toc);
  UNUSED_LEAF(book.book_toc_count);
  UNUSED_LEAF(book.book_toc_for_spine);
  return table;
}

#undef UNUSED_LEAF

static const XpAppResources resources = [] {
  auto table = resourcesDefaults();
  table.path_exists = [](XpAppText path) -> uint8_t {
    leaf(); assert(lockDepth == 0);
    return Storage.exists(text(path).c_str());
  };
  return table;
}();
static const XpAppRuntime runtime = [] {
  auto table = runtimeDefaults();
  table.now_ms = []() -> uint32_t {
    leaf();
    return room ? room->now(room->context) : static_cast<uint32_t>(millis());
  };
  table.delay_ms = [](uint32_t ms) {
    leaf(); assert(room && lockDepth == 0 && ms == 50);
    room->sleep(room->context, ms);
  };
  table.render_lock = []() -> void* {
    leaf(); assert(lockDepth == 0);
    return new RenderLock();
  };
  table.render_try_lock = []() -> void* {
    leaf(); assert(lockDepth == 0);
    auto token = std::make_unique<RenderLock>(RenderLock::Mode::Try);
    if (!token->ownsLock()) return nullptr;
    return token.release();
  };
  table.render_unlock = [](void* token) {
    leaf(); assert(token && lockDepth == 1);
    delete static_cast<RenderLock*>(token);
  };
  table.model_lock = [] { assert(active && modelDepth == 0); ++modelDepth; };
  table.model_unlock = [] { assert(active && modelDepth == 1); --modelDepth; };
  table.notify_render = [] { fixture().requestUpdate(); };
  table.touch_feedback = [](uint8_t strong) {
    leaf(); assert(strong <= 1 && lockDepth == 0);
    haptic_feedback::touchAction(strong != 0);
  };
  return table;
}();

static const XpUiReaderInputServices inputServices = {
    nullptr,
    [](void* unused, uint32_t button, uint8_t query, uint32_t threshold) -> uint8_t {
      assert(!unused && query == XP_UI_READER_BUTTON_RELEASE && threshold == 0);
      auto& input = fixture().mappedInput;
      if (button == XP_UI_POWER) return input.wasReleased(MappedInputManager::Button::Power);
      assert(button == XP_UI_DOWN);
      return input.wasReleased(MappedInputManager::Button::Down);
    },
    [](void* unused, uint8_t query) -> uint32_t {
      assert(!unused && query == XP_UI_READER_INPUT_HELD_MS);
      return fixture().mappedInput.getHeldTime();
    },
    &Unexpected<decltype(XpUiReaderInputServices::tap)>::call};

static const LegacyEpubLeaves leaves = [] {
  auto table = leavesDefaults();
  table.physical.render_busy = []() -> uint8_t { leaf(); return RenderLock::peek(); };
  table.physical.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    leaf(); assert(resolve == 0 && out);
    static_assert(CrossPointSettings::OFF == 0 && CrossPointSettings::CHAPTER_SKIP == 1 &&
                  CrossPointSettings::ORIENTATION_CHANGE == 2);
    *out = {};
    out->long_button = SETTINGS.longPressButtonBehavior;
    out->orientation = SETTINGS.orientation;
  };
  table.physical.input_services = []() -> const XpUiReaderInputServices* { leaf(); return &inputServices; };
  table.physical.input_pressed = [](uint32_t button) -> uint8_t {
    assert(button < 7);
    return button == 0 && fixture().anyButtonPressed();
  };
  table.physical.input_raw_contact = []() -> uint8_t { leaf(); return buttonContactClosed(); };
  table.physical.radio_holds_heap = []() -> uint8_t { leaf(); return bleturner::holdsHeap(); };
  table.physical.heap_snapshot = [](XpEpubHeap* out) {
    leaf(); assert(out);
    *out = {static_cast<uint32_t>(ESP.getFreeHeap()), static_cast<uint32_t>(ESP.getMaxAllocHeap())};
  };
  table.physical.strip_release_idle = [](void* native) -> uint8_t { context(native); return 0; };
  table.physical.radio_status = [](XpEpubRadio* out, XpEpubPeer* peer) {
    leaf(); assert(room && out && !peer && lockDepth == 0);
    const auto value = room->status(room->context);
    assert(value <= 7);
    *out = {};
    out->running = (value & 1) != 0;
    out->stopping = (value & 2) != 0;
    out->starting = (value & 4) != 0;
  };
  table.physical.loan_count = []() -> uint32_t { return fixture().renderer.frameBufferLoanCount(); };
  table.book.book_spine_count = [](void* book) -> int32_t {
    assert(book && book == fixture().epub.get());
    return static_cast<Epub*>(book)->getSpineItemsCount();
  };
  table.book.book_cache_path = [](void* book, XpAppTextSink sink, void* target) -> uint8_t {
    assert(book && book == fixture().epub.get() && sink && lockDepth == 0);
    const auto path = static_cast<Epub*>(book)->getCachePath();
    return sink(target, wire(path));
  };
  table.book.section.section_close = [](void* handle) {
    (void)section(handle);
    assert(!bridge().publishing && (lockDepth == 1 || bridge().destroying));
    fixture().section.reset();
  };
  table.book.section.section_page_count = [](void* handle) -> uint16_t {
    const auto value = section(handle).pageCount;
    publication(0);
    return value;
  };
  table.book.section.section_estimated_pages = [](void* handle) -> int32_t {
    const auto value = section(handle).pageCount;
    publication(1);
    return value;
  };
  table.book.section.section_building = [](void* handle) -> uint8_t {
    const auto value = section(handle).isBuilding();
    if (bridge().publishing) publication(2);
    else assert(lockDepth == 1);
    return value;
  };
  table.book.section.section_complete = [](void* handle) -> uint8_t {
    const auto value = section(handle).isBuildComplete();
    if (bridge().publishing) publication(3);
    else assert(lockDepth == 1);
    return value;
  };
  table.book.section.section_partial = [](void* handle) -> uint8_t {
    (void)section(handle); publication(4); return 0;
  };
  table.book.section.section_build_more = [](void* handle, int32_t pages, uint8_t yield) -> uint8_t {
    assert(lockDepth == 1 && pages == 2 && yield == 1);
    const auto result = section(handle).buildSomeMore(pages, &buttonContactClosed);
    bridge().beginPublication();
    return result;
  };
  table.physical.file_open_read = [](XpAppText path, XpAppText origin) -> void* {
    assert(origin.len == 3 && std::memcmp(origin.data, "ERS", 3) == 0);
    leaf(); assert(lockDepth == 0 && !liveFile);
    auto file = std::make_unique<HalFile>();
    if (!Storage.openFileForRead("ERS", text(path), *file)) return nullptr;
    liveFile = file.release();
    return liveFile;
  };
  table.physical.file_open_write = [](XpAppText path, XpAppText origin) -> void* {
    assert(origin.len == 3 && std::memcmp(origin.data, "ERS", 3) == 0);
    leaf(); assert(lockDepth == 0 && !liveFile);
    auto file = std::make_unique<HalFile>();
    if (!Storage.openFileForWrite("ERS", text(path), *file)) return nullptr;
    liveFile = file.release();
    return liveFile;
  };
  table.physical.file_read = [](void* file, uint8_t* bytes, uint32_t length) -> int32_t {
    leaf(); assert(lockDepth == 0 && file && file == liveFile && (length == 10 || length == 13));
    return liveFile->read(bytes, length);
  };
  table.physical.file_write = [](void* file, const uint8_t* bytes, uint32_t length) -> uint32_t {
    leaf(); assert(lockDepth == 0 && file && file == liveFile && length <= 13);
    return liveFile->write(bytes, length);
  };
  table.physical.file_close = [](void* file) -> uint8_t {
    leaf(); assert(lockDepth == 0 && file && file == liveFile);
    delete liveFile;
    liveFile = nullptr;
   return 1; };
  table.physical.path_remove = [](XpAppText path) -> uint8_t {
    leaf(); assert(lockDepth == 0 && !liveFile);
    return Storage.remove(text(path).c_str());
  };
  table.physical.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    leaf(); assert(severity <= 1 && text(origin) == "ERS");
    if (room) {
      // This exact command was a silent historical room-fixture boundary.
      assert(severity == 1 && text(message) ==
             "Bluetooth radio still holds the heap after 5000 ms; chapter build refused");
      return;
    }
    std::printf("%s %.*s %.*s\n", severity ? "ERR" : "DBG", int(origin.len),
                reinterpret_cast<const char*>(origin.data), int(message.len),
                reinterpret_cast<const char*>(message.data));
  };
  table.physical.book_set_extractor = [](void* handle) {
    if (handle) leaves.book.book_install_extractor(handle);
    else leaves.book.book_clear_extractor();
  };
  return table;
}();

Bridge::Bridge(EpubReaderActivity& value, bool composition) : fixture(value), input(composition) {
  Active current(*this);
  XpEpubTestLifecycleSeed seed{};
  seed.native = &fixture;
  seed.book = fixture.epub.get();
  seed.section = fixture.section.get();
  auto& s = seed.state;
  s.spine = fixture.currentSpineIndex;
  s.page = fixture.section ? fixture.section->currentPage : 0;
  s.next_page = fixture.nextPageNumber;
  s.cached_spine = fixture.cachedSpineIndex;
  s.cached_count = fixture.cachedChapterTotalPageCount;
  s.cached_offset = fixture.cachedVisibleTextOffset.value_or(0);
  s.has_cached_offset = fixture.cachedVisibleTextOffset.has_value();
  s.pending_page = fixture.pendingPageJump.value_or(0);
  s.has_pending_page = fixture.pendingPageJump.has_value();
  s.last_turn = fixture.lastPageTurnTime;
  s.failed_spine = fixture.backgroundBuildFailedSpine;
  s.heap_paused = fixture.buildHeapPaused;
  s.overlay = static_cast<uint8_t>(fixture.overlay);
  s.stale = fixture.pageBufferStale.load();
  s.manual_turn = fixture.pendingManualTurn;
  s.manual_touch = fixture.pendingManualTurnTouch;
  s.orientation = SETTINGS.orientation;
  s.has_section = bool(fixture.section);
  s.radio_released = room != nullptr;
  s.history_depth = fixture.footnoteDepth;
  for (unsigned i = 0; i < 3; ++i) {
    s.history[2*i] = fixture.savedPositions[i].spineIndex;
    s.history[2*i+1] = fixture.savedPositions[i].pageNumber;
  }
  seed.context = &fixture;
  seed.clear = [](void* target) {
    context(target); assert(lockDepth == 1 && bridge().owner);
    lifecycle_session::fixture().clearDeferredReposition();
    xp_epub_test_lifecycle_clear_pending_page(bridge().owner);
  };
  seed.acknowledge = [](void* target) { context(target); bleturner::acknowledgeLinkNote(); };
  seed.fallback = &Unexpected<decltype(seed.fallback)>::call;
  seed.estimate = [](void* target, int32_t value) {
    context(target); assert(lifecycle_session::fixture().section && value == lifecycle_session::fixture().section->pageCount);
  };
  seed.building = [](void* target, uint8_t value) {
    context(target); assert(lockDepth == 0 && lifecycle_session::fixture().section);
    assert(value == uint8_t(lifecycle_session::fixture().section->isBuilding()));
  };
  seed.reposition = [](void* target) -> uint8_t {
    context(target); assert(lockDepth == 1);
    return lifecycle_session::fixture().applyDeferredReposition();
  };
  seed.update = [](void* target) { context(target); lifecycle_session::fixture().requestUpdate(); };
  seed.buttons = [](void* target) -> XpUiReaderTurnInput {
    context(target);
    const auto value = ReaderUtils::detectPageTurn(lifecycle_session::fixture().mappedInput);
    return {0, uint8_t(value.prev), uint8_t(value.next), uint8_t(value.fromTilt), 0};
  };
  seed.note = [](void* target, uint8_t forward, uint8_t ok) {
    context(target); lifecycle_session::fixture().notePageTurn(forward != 0, ok != 0);
  };
  seed.end_turn = [](void* target, uint8_t previous, uint8_t next) -> uint8_t {
    context(target); return lifecycle_session::fixture().handleEndOfBookPageTurn(previous != 0, next != 0);
  };
  seed.orientation = [](void* target, uint8_t value) { context(target); lifecycle_session::fixture().applyOrientation(value); };
  seed.input_composition = input;
  if (fixture.section) beginPublication();
  owner = xp_epub_test_lifecycle_create_legacy_book(&seed,&leaves.physical, &runtime, &resources, &leaves.book);
  assert(owner && !publishing);
  ++created;
  inputOwners += input;
}
Bridge::~Bridge() {
  Active current(*this);
  assert(owner);
  destroying = true;
  xp_epub_test_lifecycle_destroy(owner);
  owner = nullptr;
  assert(!fixture.section);
  ++destroyed;
}
void Bridge::observe() {
  assert(active == this && owner && !publishing);
  XpEpubTestLifecycleSnapshot s{};
  xp_epub_test_lifecycle_observe(owner, &s);
  assert(bool(s.has_section) == bool(fixture.section));
  assert(s.overlay <= 1);
  fixture.currentSpineIndex = s.spine;
  if (fixture.section) fixture.section->currentPage = s.page;
  fixture.nextPageNumber = s.next_page;
  fixture.cachedSpineIndex = s.cached_spine;
  fixture.cachedChapterTotalPageCount = s.cached_count;
  fixture.cachedVisibleTextOffset = s.has_cached_offset ? std::optional<uint32_t>(s.cached_offset) : std::nullopt;
  fixture.pendingPageJump = s.has_pending_page ? std::optional<uint16_t>(s.pending_page) : std::nullopt;
  fixture.lastPageTurnTime = s.last_turn;
  fixture.backgroundBuildFailedSpine = s.failed_spine;
  fixture.buildHeapPaused = s.heap_paused;
  fixture.overlay = static_cast<EpubReaderActivity::Overlay>(s.overlay);
  fixture.pageBufferStale.store(s.stale);
  fixture.pendingManualTurn = s.manual_turn;
  fixture.pendingManualTurnTouch = s.manual_touch;
  assert(s.orientation == SETTINGS.orientation);
  fixture.footnoteDepth = s.history_depth;
  for (unsigned i = 0; i < 3; ++i) {
    fixture.savedPositions[i] = {s.history[2*i], s.history[2*i+1]};
  }
  assert(s.radio_released == uint8_t(room != nullptr));
}
template <typename Call> static auto invoke(const EpubReaderActivity& value, bool input, Call call) {
  auto& fixture = const_cast<EpubReaderActivity&>(value);
  if (!fixture.lifecycleSession) fixture.lifecycleSession = new Bridge(fixture, input);
  auto& b = *static_cast<Bridge*>(fixture.lifecycleSession);
  assert(b.input == input);
  Active current(b);
  if constexpr (std::is_void_v<decltype(call(b.owner))>) {
    call(b.owner);
    b.observe();
  } else {
    auto result = call(b.owner);
    b.observe();
    return result;
  }
}
static void assertComplete() {
  assert(!active && !room && !liveFile && !lockDepth && !modelDepth);
  assert(created == 1566 && destroyed == created);
  assert(inputOwners == 12 && inputCalls == 96 && roomCalls == 20);
  if (publications != 1549) {
    std::fflush(stdout);
    std::fprintf(stderr, "EXPECTED_ASSERTION: LIFECYCLE_PUBLICATION_COUNT expected=1549 actual=%u\n", publications);
    std::exit(42);
  }
}
}  // namespace lifecycle_session

EpubReaderActivity::~EpubReaderActivity() {
  delete static_cast<lifecycle_session::Bridge*>(lifecycleSession);
}
bool EpubReaderActivity::pageTurn(bool forward) {
  return lifecycle_session::invoke(*this, false, [=](void* owner) { return xp_epub_test_projection_turn(owner, forward) != 0; });
}
bool EpubReaderActivity::skipPages(int amount) {
  return lifecycle_session::invoke(*this, false, [=](void* owner) { return xp_epub_test_projection_skip(owner, amount) != 0; });
}
bool EpubReaderActivity::buildTickHeapGate() {
  return lifecycle_session::invoke(*this, false, [](void* owner) { return xp_epub_test_build_heap(owner) != 0; });
}
void EpubReaderActivity::backgroundBuildTick() {
  lifecycle_session::invoke(*this, false, [](void* owner) { xp_epub_test_build_background(owner); });
}
bool EpubReaderActivity::backgroundBuildWanted() const {
  return lifecycle_session::invoke(*this, false, [](void* owner) { return xp_epub_test_build_wanted(owner) != 0; });
}
bool EpubReaderActivity::skipLoopDelay() {
  return lifecycle_session::invoke(*this, false, [](void* owner) { return xp_epub_test_build_delay(owner) != 0; });
}
void EpubReaderActivity::saveLinkStack() const {
  lifecycle_session::invoke(*this, false, [](void* owner) { xp_epub_test_lifecycle_save_links(owner); });
}
void EpubReaderActivity::loadLinkStack() {
  lifecycle_session::invoke(*this, false, [](void* owner) { xp_epub_test_lifecycle_load_links(owner); });
}
void EpubReaderActivity::restoreSavedPosition() {
  lifecycle_session::invoke(*this, false, [](void* owner) { xp_epub_test_navigation_restore(owner); });
}
void EpubReaderActivity::loadProgress() {
  lifecycle_session::invoke(*this, false, [](void* owner) { xp_epub_test_lifecycle_progress(owner); });
}
void EpubReaderActivity::turnInput() {
  const auto touch = touchInput;
  lifecycle_session::invoke(*this, true, [=](void* owner) {
    ++lifecycle_session::inputCalls;
    xp_epub_test_lifecycle_input(owner, {static_cast<uint32_t>(touch.heldMs), uint8_t(touch.prev), uint8_t(touch.next), 0, 0});
  });
}

template <typename Status, typename Now, typename Sleep>
bool waitForBuildRoom(Status status, Now now, Sleep sleep) {
  struct Calls { Status& status; Now& now; Sleep& sleep; } calls{status, now, sleep};
  lifecycle_session::Room room{&calls,
      [](void* p) -> uint32_t { return static_cast<Calls*>(p)->status(); },
      [](void* p) -> uint32_t { return static_cast<Calls*>(p)->now(); },
      [](void* p, uint32_t ms) { static_cast<Calls*>(p)->sleep(ms); }};
  assert(!lifecycle_session::room);
  lifecycle_session::room = &room;
  bool result;
  {
    EpubReaderActivity owner;
    owner.section.reset();
    result = lifecycle_session::invoke(owner, false, [](void* value) { return xp_epub_test_build_room(value) != 0; });
  }
  lifecycle_session::room = nullptr;
  ++lifecycle_session::roomCalls;
  return result;
}
inline bool buildRoomForRadio(bool running, bool stopping, bool starting) {
  uint32_t now = 0;
  return waitForBuildRoom([=] { return uint32_t(running) | (uint32_t(stopping) << 1) | (uint32_t(starting) << 2); },
                          [&] { return now; }, [&](uint32_t ms) { now += ms; });
}
