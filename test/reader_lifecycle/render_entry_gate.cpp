#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

#include "render_entry_gate_fixture.h"

using namespace std::chrono_literals;
struct Fixture;
static Fixture* active;
static thread_local unsigned modelDepth, renderDepth;
static const XpEpubLeaves& physicalLeaves();
static const XpAppRuntime& physicalRuntime();
static const XpAppResources& physicalResources();

struct Fixture {
  std::mutex model, render, eventsMutex;
  std::promise<void> opened, resume;
  std::shared_future<void> resumeFuture = resume.get_future().share();
  std::atomic<unsigned> physicalCalls{0}, openCalls{0};
  std::atomic<bool> popupActive{false};
  std::vector<std::string> events;
  bool blockOpen;
  int popup = 0;
  void* owner = nullptr;

  explicit Fixture(bool block) : blockOpen(block) {
    assert(!active && !modelDepth && !renderDepth);
    active = this;
    owner = xp_epub_test_entry_gate_create(this, &popup, &physicalLeaves(), &physicalRuntime(), &physicalResources());
    assert(owner && !physicalCalls && !modelDepth && !renderDepth);
  }
  ~Fixture() {
    assert(!modelDepth && !renderDepth);
    const auto calls = physicalCalls.load();
    xp_epub_test_entry_gate_destroy(owner);
    assert(physicalCalls == calls);
    active = nullptr;
  }
  XpEpubEntryGateObservation observe() {
    assert(!modelDepth);
    XpEpubEntryGateObservation out{};
    xp_epub_test_entry_gate_observe(owner, &out);
    return out;
  }
  void record(const char* event) {
    assert(!modelDepth);
    ++physicalCalls;
    std::lock_guard<std::mutex> lock(eventsMutex);
    events.emplace_back(event);
  }
  void expect(std::initializer_list<const char*> expected) {
    std::lock_guard<std::mutex> lock(eventsMutex);
    assert(events.size() == expected.size());
    size_t index = 0;
    for (const char* event : expected) assert(events[index++] == event);
    assert(physicalCalls == events.size());
  }
};

struct RenderGuard {
  Fixture& fixture;
  explicit RenderGuard(Fixture& f) : fixture(f) {
    assert(!modelDepth && !renderDepth);
    fixture.render.lock();
    ++renderDepth;
  }
  ~RenderGuard() {
    assert(!modelDepth && renderDepth == 1);
    --renderDepth;
    fixture.render.unlock();
  }
};
static void physical(unsigned expectedRenderDepth) {
  assert(active && !modelDepth && renderDepth == expectedRenderDepth);
}
static std::string text(XpAppText value) {
  assert(value.data || !value.len);
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}
static void accountingUnchanged(const XpEpubEntryGateObservation& value) {
  assert(value.anchor_ms == 123 && value.duration == 456 && value.refresh_counter == 7);
  assert(!value.frame_present && !value.rendered && value.refresh_requests == 1 && value.pending_forward == 1);
}
static void equal(const XpEpubEntryGateObservation& a, const XpEpubEntryGateObservation& b) {
  assert(a.anchor_ms == b.anchor_ms && a.duration == b.duration && a.refresh_counter == b.refresh_counter);
  assert(a.ready == b.ready && a.frame_present == b.frame_present && a.rendered == b.rendered);
  assert(a.scene_failed == b.scene_failed && a.refresh_requests == b.refresh_requests &&
         a.pending_forward == b.pending_forward);
}
static void recoveryCallback() {
  physical(1);
  const auto value = active->observe();
  accountingUnchanged(value);
  assert(value.ready);
}
template <typename Signature>
struct Unexpected;
template <typename Result, typename... Args>
struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) {
    assert(active && !modelDepth);
    assert(false && "unexpected entry-gate physical callback");
    std::abort();
  }
};
#define UNUSED_LEAF(field) table.field = &Unexpected<decltype(table.field)>::call
static const XpAppResources& physicalResources() {
  static const XpAppResources value = [] {
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
  }();
  return value;
}
static const XpAppRuntime& physicalRuntime() {
  static const XpAppRuntime value = [] {
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
    table.model_lock = [] {
      assert(active && !modelDepth);
      active->model.lock();
      ++modelDepth;
    };
    table.model_unlock = [] {
      assert(active && modelDepth == 1);
      --modelDepth;
      active->model.unlock();
    };
    return table;
  }();
  return value;
}
static const XpUiReaderRenderServices renderServices = {
    nullptr,
    [](void*, uint8_t) -> uint32_t {
      physical(1);
      assert(false && "recovery must not query render facts");
      std::abort();
    },
    [](void* context, const XpUiReaderRenderOp* op) -> uint32_t {
      recoveryCallback();
      assert(!context && op);
      assert(op->kind == XP_UI_READER_RENDER_CLEAR && op->value == 255);
      assert(op->buffer == 0 && op->mode == 0 && op->reserved == 0);
      assert(op->y == 0 && op->rows == 0 && op->offset == 0);
      active->record("clear-255");
      return 0;
    },
};
static const XpEpubLeaves& physicalLeaves() {
  static const XpEpubLeaves value = [] {
    XpEpubLeaves table{};
    static_assert(sizeof(XpEpubLeaves) == 205 * sizeof(void (*)()));
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
    UNUSED_LEAF(loan_begin);
    UNUSED_LEAF(loan_end);
    UNUSED_LEAF(loan_drop);
    UNUSED_LEAF(loan_count);
    UNUSED_LEAF(strip_release_idle);
    UNUSED_LEAF(book_open);
    UNUSED_LEAF(book_load);
    UNUSED_LEAF(book_close);
    UNUSED_LEAF(book_size);
    UNUSED_LEAF(book_spine_count);
    UNUSED_LEAF(book_toc_count);
    UNUSED_LEAF(book_path);
    UNUSED_LEAF(book_cache_path);
    UNUSED_LEAF(book_title);
    UNUSED_LEAF(book_author);
    UNUSED_LEAF(book_language);
    UNUSED_LEAF(book_thumbnail);
    UNUSED_LEAF(book_spine);
    UNUSED_LEAF(book_toc);
    UNUSED_LEAF(book_spine_for_toc);
    UNUSED_LEAF(book_toc_for_spine);
    UNUSED_LEAF(book_cumulative_size);
    UNUSED_LEAF(book_spine_for_size);
    UNUSED_LEAF(book_text_reference);
    UNUSED_LEAF(book_progress);
    UNUSED_LEAF(book_resolve_href);
    UNUSED_LEAF(book_setup_cache);
    UNUSED_LEAF(book_clear_cache);
    UNUSED_LEAF(book_install_extractor);
    UNUSED_LEAF(book_clear_extractor);
    UNUSED_LEAF(book_protection_error);
    UNUSED_LEAF(progress_to_reader);
    UNUSED_LEAF(progress_to_saved);
    UNUSED_LEAF(progress_save);
    UNUSED_LEAF(section_open);
    UNUSED_LEAF(section_close);
    UNUSED_LEAF(section_page_count);
    UNUSED_LEAF(section_estimated_pages);
    UNUSED_LEAF(section_building);
    UNUSED_LEAF(section_complete);
    UNUSED_LEAF(section_partial);
    UNUSED_LEAF(section_load_cache);
    UNUSED_LEAF(section_create_cache);
    UNUSED_LEAF(section_start_build);
    UNUSED_LEAF(section_build_more);
    UNUSED_LEAF(section_abandon_build);
    UNUSED_LEAF(section_clear_cache);
    UNUSED_LEAF(section_html_cached);
    UNUSED_LEAF(section_find_anchor);
    UNUSED_LEAF(section_cached_anchor);
    UNUSED_LEAF(section_toc_for_page);
    UNUSED_LEAF(section_page_for_offset);
    UNUSED_LEAF(section_offset_for_page);
    UNUSED_LEAF(section_paragraph_for_page);
    UNUSED_LEAF(section_reached_offset);
    UNUSED_LEAF(section_load_page);
    UNUSED_LEAF(section_page_text);
    UNUSED_LEAF(page_close);
    UNUSED_LEAF(page_info);
    UNUSED_LEAF(page_element);
    UNUSED_LEAF(page_word);
    UNUSED_LEAF(page_footnote);
    UNUSED_LEAF(page_link);
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
    UNUSED_LEAF(input_long_press);
    UNUSED_LEAF(input_released);
    UNUSED_LEAF(input_screen_long);
    UNUSED_LEAF(input_screen_tap);
    UNUSED_LEAF(input_raw_contact);
    UNUSED_LEAF(render_busy);
    UNUSED_LEAF(file_open_read);
    UNUSED_LEAF(file_open_write);
    UNUSED_LEAF(file_read);
    UNUSED_LEAF(file_write);
    UNUSED_LEAF(file_flush);
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
    table.book_open = [](XpAppText path) -> void* {
      physical(0);
      assert(path.data || !path.len);
      assert(active->openCalls.fetch_add(1) == 0);
      active->record("book-open");
      active->opened.set_value();
      if (active->blockOpen) {
        assert(active->resumeFuture.wait_for(10s) == std::future_status::ready && "entry unblock timeout");
        active->resumeFuture.get();
      }
      physical(0);
      return nullptr;
    };
    table.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
      physical(0);
      assert(severity == 1 && text(origin) == "ERS" && text(message) == "Failed to allocate EPUB object");
      active->record("allocation-log");
    };
    table.popup_active = [](void* popup) -> uint8_t {
      recoveryCallback();
      assert(popup == &active->popup);
      active->record("popup-active");
      return active->popupActive.load();
    };
    table.render_services = [](void* native) -> const XpUiReaderRenderServices* {
      recoveryCallback();
      assert(native == active);
      active->record("render-services");
      return &renderServices;
    };
    table.popup_process_render = [](void* popup) {
      recoveryCallback();
      assert(popup == &active->popup && active->popupActive);
      active->record("popup-process-render");
    };
    return table;
  }();
  return value;
}
#undef UNUSED_LEAF

static void blocked() {
  Fixture fixture(true);
  const auto before = fixture.observe();
  accountingUnchanged(before);
  assert(!before.ready && before.scene_failed == 1);
  auto entered = fixture.opened.get_future();
  auto load = std::async(std::launch::async, [&fixture] { return xp_epub_test_entry_gate_load(fixture.owner); });
  assert(entered.wait_for(5s) == std::future_status::ready && "book_open did not block");
  entered.get();
  fixture.expect({"book-open"});
  auto paint = std::async(std::launch::async, [&fixture, before] {
    RenderGuard lock(fixture);
    assert(xp_epub_test_entry_gate_render(fixture.owner) == 0);
    equal(before, fixture.observe());
  });
  assert(paint.wait_for(5s) == std::future_status::ready && "blocked render timeout");
  paint.get();
  fixture.expect({"book-open"});
  assert(load.wait_for(0s) == std::future_status::timeout && "entry unexpectedly completed before unblock");
  fixture.resume.set_value();
  assert(load.wait_for(5s) == std::future_status::ready && "entry completion timeout");
  assert(load.get() == 0);
  const auto after = fixture.observe();
  auto expected = before;
  expected.ready = 1;
  equal(expected, after);
  fixture.expect({"book-open", "allocation-log"});
  std::puts(
      "blocked: render rejected during actual entry load; no physical render, frame, accounting or scene mutation");
}
static void failed() {
  Fixture fixture(false);
  const auto before = fixture.observe();
  accountingUnchanged(before);
  assert(!before.ready && before.scene_failed == 1);
  assert(xp_epub_test_entry_gate_load(fixture.owner) == 0);
  auto expected = before;
  expected.ready = 1;
  equal(expected, fixture.observe());
  fixture.expect({"book-open", "allocation-log"});
  fixture.popupActive = true;
  expected.scene_failed = 0;
  auto paint = std::async(std::launch::async, [&fixture, expected] {
    RenderGuard lock(fixture);
    assert(xp_epub_test_entry_gate_render(fixture.owner) == 1);
    equal(expected, fixture.observe());
  });
  assert(paint.wait_for(5s) == std::future_status::ready && "recovery render timeout");
  paint.get();
  fixture.expect(
      {"book-open", "allocation-log", "popup-active", "render-services", "clear-255", "popup-process-render"});
  std::puts("failed: recovery popup renders before base frame; accounting and refresh request preserved");
}
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string mode = argv[1];
  if (mode == "blocked")
    blocked();
  else if (mode == "failed")
    failed();
  else {
    assert(false && "expected blocked or failed");
  }
}
