#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <string>

#include "base_session_fixture.h"

struct Fixture;
static Fixture* active = nullptr;
static thread_local unsigned modelDepth = 0, resourceDepth = 0;
static const LegacyEpubLeaves& physicalLeaves();
static const XpAppRuntime& physicalRuntime();
static const XpAppResources& physicalResources();

struct Fixture {
  std::mutex model, render;
  std::promise<void> painting, resume;
  std::atomic<unsigned> ownerResourceLocks{0}, nativeCalls{0}, progressCalls{0};
  std::atomic<uint32_t> now{10000};
  std::atomic<int64_t> epoch{110};
  std::atomic<float> progress{0.002f};
  void* owner = nullptr;
  unsigned paints = 0;
  explicit Fixture(bool ready = true) {
    assert(!active);
    active = this;
    XpEpubPublicationSeed seed{};
    seed.book = this;
    seed.session.anchor_epoch = 100;
    seed.session.anchor_progress = 10;
    seed.session.has_anchor = 1;
    seed.refresh_counter = 15;
    seed.ready = ready;
    owner = xp_epub_test_publication_create_legacy_book(&seed, &physicalLeaves().physical, &physicalRuntime(),
                                                        &physicalResources(), &physicalLeaves().book);
    assert(owner && !modelDepth && !resourceDepth && !nativeCalls);
  }
  ~Fixture() {
    assert(!modelDepth && !resourceDepth);
    xp_epub_test_publication_destroy(owner);
    active = nullptr;
  }
  XpEpubPublicationObservation observe() {
    XpEpubPublicationObservation value{};
    xp_epub_test_publication_observe(owner, &value);
    return value;
  }
};
struct RenderGuard {
  Fixture& fixture;
  explicit RenderGuard(Fixture& f) : fixture(f) {
    assert(!modelDepth && !resourceDepth);
    fixture.render.lock();
    ++resourceDepth;
  }
  ~RenderGuard() {
    assert(!modelDepth && resourceDepth == 1);
    --resourceDepth;
    fixture.render.unlock();
  }
};
static void physical() {
  assert(active && !modelDepth && resourceDepth == 1);
  ++active->nativeCalls;
}
template <typename Signature>
struct Unexpected;
template <typename Result, typename... Args>
struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) {
    assert(false && "unexpected publication resource");
    std::abort();
  }
};
#define UNUSED_LEAF(field) table.field = &Unexpected<decltype(table.field)>::call

static const XpAppResources& physicalResources() {
  static const XpAppResources value = [] {
    XpAppResources table{};
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
    table.render_lock = []() -> void* {
      assert(active && !modelDepth && !resourceDepth);
      ++active->ownerResourceLocks;
      active->render.lock();
      ++resourceDepth;
      return active;
    };
    table.render_unlock = [](void* token) {
      assert(token == active && !modelDepth && resourceDepth == 1);
      --resourceDepth;
      active->render.unlock();
    };
    table.now_ms = []() -> uint32_t {
      physical();
      return active->now.load();
    };
    return table;
  }();
  return value;
}

static const LegacyEpubLeaves& physicalLeaves() {
  static const LegacyEpubLeaves value = [] {
    LegacyEpubLeaves table{};
    static_assert(sizeof(XpEpubLeaves) == 167 * sizeof(void (*)()));
    UNUSED_LEAF(physical.render_margins);
    UNUSED_LEAF(physical.render_geometry);
    UNUSED_LEAF(physical.render_restore_bw);
    UNUSED_LEAF(physical.render_discard_bw);
    UNUSED_LEAF(physical.render_orientation);
    UNUSED_LEAF(physical.loan_count);
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
    UNUSED_LEAF(book.section.page.page_close);
    UNUSED_LEAF(book.section.page.page_info);
    UNUSED_LEAF(book.section.page.page_element);
    UNUSED_LEAF(book.section.page.page_word);
    UNUSED_LEAF(book.section.page.metadata_assign);
    UNUSED_LEAF(book.section.page.metadata_clear);
    UNUSED_LEAF(book.section.page.metadata_footnotes);
    UNUSED_LEAF(book.section.page.metadata_links);
    UNUSED_LEAF(book.section.page.metadata_footnote);
    UNUSED_LEAF(book.section.page.metadata_link);
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
    UNUSED_LEAF(book.section.page.page_word_font);
    UNUSED_LEAF(book.section.page.page_line_font);
    UNUSED_LEAF(book.section.page.page_word_y_offset);
    UNUSED_LEAF(book.section.page.page_line_ruby_shift);
    UNUSED_LEAF(physical.font_text_advance);
    UNUSED_LEAF(physical.log_enabled);
    UNUSED_LEAF(physical.toolbar_reset_navigation);
    UNUSED_LEAF(physical.input_snapshot);
    UNUSED_LEAF(book.section.page.page_render);
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
    UNUSED_LEAF(book.section.page.page_load);
    UNUSED_LEAF(physical.framebuffer_lend);
    UNUSED_LEAF(physical.framebuffer_restore);
    UNUSED_LEAF(physical.paint_arena);
    UNUSED_LEAF(physical.image_needs_decode);
    UNUSED_LEAF(physical.font_ascent);
    UNUSED_LEAF(physical.render_physical_query);
    UNUSED_LEAF(physical.render_physical_operation);
    UNUSED_LEAF(physical.render_plan);
    UNUSED_LEAF(physical.image_read_window);
    UNUSED_LEAF(physical.display_source);
    UNUSED_LEAF(physical.framebuffer_view);
    table.physical.trusted_now = []() -> int64_t {
      physical();
      return active->epoch.load();
    };
    table.book.book_spine_count = [](void* book) -> int32_t {
      physical();
      assert(book == active);
      return 2;
    };
    table.book.book_size = [](void* book) -> uint32_t {
      physical();
      assert(book == active);
      return 1000;
    };
    table.book.book_progress = [](void* book, int32_t spine, float fraction) -> float {
      physical();
      assert(book == active && spine == 0 && std::fabs(fraction - 0.01f) < 0.000001f);
      ++active->progressCalls;
      return active->progress.load();
    };
    UNUSED_LEAF(physical.book_facade_open);
    UNUSED_LEAF(physical.book_facade_close);
    table.physical.book_set_extractor = [](void* handle) {
      if (handle)
        value.book.book_install_extractor(handle);
      else
        value.book.book_clear_extractor();
    };
    return table;
  }();
  return value;
}
#undef UNUSED_LEAF

static void sameSession(const XpUiReaderSession& actual, const XpUiReaderSession& expected) {
  assert(actual.anchor_ms == expected.anchor_ms && "session anchor_ms");
  assert(actual.anchor_epoch == expected.anchor_epoch && "session anchor_epoch");
  assert(actual.start_epoch == expected.start_epoch && "session start_epoch");
  assert(actual.duration == expected.duration && "session duration");
  assert(actual.anchor_progress == expected.anchor_progress && "session anchor_progress");
  assert(actual.start_progress == expected.start_progress && "session start_progress");
  assert(actual.end_progress == expected.end_progress && "session end_progress");
  assert(actual.has_anchor == expected.has_anchor && "session has_anchor");
  assert(actual.pending_forward == expected.pending_forward && "session pending_forward");
}
static void blockedPaint(void* context, void* frame) {
  auto& f = *static_cast<Fixture*>(context);
  assert(resourceDepth == 1 && !modelDepth && xp_epub_test_publication_frame_present(f.owner));
  XpEpubPublicationObservation painting{};
  xp_epub_test_publication_frame_observe(frame, &painting);
  assert(painting.session.pending_forward && !painting.remembered);
  f.painting.set_value();
  f.resume.get_future().wait();
  xp_epub_test_publication_frame_set_counter(frame, 14);
  xp_epub_test_publication_frame_complete(frame);
  xp_epub_test_publication_frame_mark(frame);
  assert(xp_epub_test_publication_accepts(f.owner));
  ++f.paints;
}
struct NextPaint {
  Fixture& fixture;
  int counter;
  bool manual;
};
static void nextPaint(void* context, void* frame) {
  auto& next = *static_cast<NextPaint*>(context);
  XpEpubPublicationObservation snapshot{};
  xp_epub_test_publication_frame_observe(frame, &snapshot);
  assert(snapshot.session.pending_forward && snapshot.remembered);
  assert(xp_epub_test_publication_frame_take_force(frame) == next.manual);
  assert(!xp_epub_test_publication_frame_take_force(frame));
  assert(xp_epub_test_publication_frame_counter(frame) == next.counter);
  xp_epub_test_publication_frame_set_counter(frame, 9);
  xp_epub_test_publication_frame_complete(frame);
  ++next.fixture.paints;
}
static void finalPaint(void* context, void* frame) {
  auto& f = *static_cast<Fixture*>(context);
  XpEpubPublicationObservation snapshot{};
  xp_epub_test_publication_frame_observe(frame, &snapshot);
  assert(!snapshot.session.pending_forward && !snapshot.refresh_requests);
  assert(!xp_epub_test_publication_frame_take_force(frame));
  assert(xp_epub_test_publication_frame_counter(frame) == 9);
  xp_epub_test_publication_frame_complete(frame);
  ++f.paints;
}
static void publicationMatrix() {
  // -2 invokes disable-fast; -1 leaves the counter alone; 0/1 set it explicitly.
  for (int counter : {-2, -1, 0, 1}) {
    for (bool manual : {false, true}) {
      Fixture f;
      xp_epub_test_publication_note(f.owner, 1, 1);
      auto paint = std::async(std::launch::async, [&] {
        RenderGuard lock(f);
        assert(xp_epub_test_publication_render(f.owner, blockedPaint, &f));
        assert(!xp_epub_test_publication_frame_present(f.owner));
      });
      f.painting.get_future().wait();
      const auto atBegin = f.observe();
      assert(!atBegin.session.pending_forward && !atBegin.refresh_requests);
      auto input = std::async(std::launch::async, [&] {
        assert(!resourceDepth);
        xp_epub_test_publication_note(f.owner, 1, 1);
        if (manual) xp_epub_test_publication_force(f.owner);
        if (counter == -2)
          xp_epub_test_publication_disable_fast(f.owner);
        else if (counter >= 0)
          xp_epub_test_publication_counter(f.owner, counter);
        xp_epub_test_publication_remember(f.owner);
        assert(!xp_epub_test_publication_accepts(f.owner));
        assert(!modelDepth && !resourceDepth);
      });
      assert(input.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
      input.get();
      assert(f.ownerResourceLocks == 0 && "input must not acquire RenderLock");
      f.resume.set_value();
      paint.get();
      const auto published = f.observe();
      assert(published.session.duration == 10 && published.session.end_progress == 20);
      assert(published.session.pending_forward == 1 && "pending input must survive publication");
      assert(published.remembered && published.rendered && xp_epub_test_publication_accepts(f.owner));
      const int expectedCounter = counter == -2 ? 0 : counter >= 0 ? counter : manual ? 1 : 14;
      assert(published.refresh_counter == expectedCounter && "counter-only refresh must survive publication");
      assert(published.refresh_requests == (manual ? 1 : 0) + (counter != -1 ? 2 : 0));
      f.now = 20000;
      f.epoch = 120;
      f.progress = 0.003f;
      {
        RenderGuard lock(f);
        NextPaint next{f, expectedCounter, manual};
        assert(xp_epub_test_publication_render(f.owner, nextPaint, &next));
        assert(!xp_epub_test_publication_frame_present(f.owner));
      }
      const auto twice = f.observe();
      assert(twice.refresh_counter == 9 && !twice.refresh_requests && !twice.session.pending_forward);
      assert(twice.remembered && twice.rendered && twice.session.duration == 20 && twice.session.end_progress == 30);
      f.now = 30000;
      f.epoch = 130;
      {
        RenderGuard lock(f);
        assert(xp_epub_test_publication_render(f.owner, finalPaint, &f));
      }
      assert(f.observe().session.duration == 20 && f.paints == 3 && f.progressCalls == 3);
      assert(f.ownerResourceLocks == 0);
      xp_epub_test_publication_reset(f.owner);
      const XpUiReaderSession empty{};
      sameSession(f.observe().session, empty);
      assert(f.observe().remembered && f.observe().rendered);
    }
  }
}
static void readyScopeGate() {
  Fixture f(false);
  auto paint = [](void* context, void* frame) {
    auto& f = *static_cast<Fixture*>(context);
    assert(frame && xp_epub_test_publication_frame_present(f.owner));
    assert(xp_epub_test_publication_frame_counter(frame) == 15);
    ++f.paints;
  };
  {
    RenderGuard lock(f);
    assert(!xp_epub_test_publication_render(f.owner, paint, &f));
    assert(!xp_epub_test_publication_frame_present(f.owner));
  }
  assert(!f.paints && !f.nativeCalls && !f.ownerResourceLocks);
  xp_epub_test_publication_ready(f.owner, 1);
  {
    RenderGuard lock(f);
    assert(xp_epub_test_publication_render(f.owner, paint, &f));
    assert(!xp_epub_test_publication_frame_present(f.owner));
  }
  assert(f.paints == 1 && !f.nativeCalls && !f.ownerResourceLocks);
  // This is the render_with scope gate, not app entry/failed-entry integration.
}
static void nestedPaint(void* context, void* frame) {
  auto& f = *static_cast<Fixture*>(context);
  assert(frame && xp_epub_test_publication_frame_present(f.owner));
  std::fputs("owner-nested-entry\n", stderr);
  std::fflush(stderr);
  xp_epub_test_publication_render(
      f.owner,
      [](void*, void*) {
        std::fputs("unexpected-nested-paint\n", stderr);
        std::_Exit(77);
      },
      nullptr);
  std::_Exit(78);
}
static void accountingBoundaries() {
  for (uint32_t elapsed : {0u, 1u, 2u, 1800u, 1801u}) {
    for (int64_t epoch : {int64_t{-1}, int64_t{0}, int64_t{100}, INT64_MAX - 5}) {
      for (int32_t progress : {INT32_MIN, -1, 0, 10000, 10001, INT32_MAX}) {
        for (bool pending : {false, true}) {
          for (bool wrapped : {false, true}) {
            XpUiReaderSession expected{};
            expected.anchor_ms = wrapped ? UINT32_MAX - 1000 : 1000;
            expected.anchor_epoch = epoch;
            expected.start_epoch = epoch;
            expected.duration = wrapped ? UINT32_MAX - 1 : 0;
            expected.anchor_progress = 10;
            expected.start_progress = 7;
            expected.end_progress = 9;
            expected.has_anchor = 1;
            auto actual = expected;
            xp_ui_reader_session_note(&expected, pending, 1);
            xp_epub_test_accounting_note(&actual, pending, 1);
            sameSession(actual, expected);
            const uint32_t now = expected.anchor_ms + elapsed * 1000u;
            xp_ui_reader_session_render(&expected, now, epoch, progress);
            xp_epub_test_accounting_render(&actual, now, epoch, progress);
            if (elapsed == 2 && epoch > 0 && pending && !wrapped) {
              assert(actual.duration == 2 && "session duration at two seconds");
            }
            sameSession(actual, expected);
            assert(xp_epub_test_accounting_worthy(&actual) == xp_ui_reader_session_worthy(&expected));
          }
        }
      }
    }
  }
  for (int64_t start : {int64_t{-1}, int64_t{0}, int64_t{1}, INT64_MAX - 5}) {
    for (uint32_t duration : {0u, 9u, 10u, UINT32_MAX}) {
      XpUiReaderSession state{};
      state.start_epoch = start;
      state.duration = duration;
      assert(xp_epub_test_accounting_worthy(&state) == xp_ui_reader_session_worthy(&state));
    }
  }
  for (bool anchored : {false, true}) {
    for (bool forward : {false, true}) {
      for (bool succeeded : {false, true}) {
        XpUiReaderSession expected{};
        expected.has_anchor = anchored;
        expected.anchor_epoch = 100;
        expected.pending_forward = 1;
        auto actual = expected;
        xp_ui_reader_session_note(&expected, forward, succeeded);
        xp_epub_test_accounting_note(&actual, forward, succeeded);
        sameSession(actual, expected);
        xp_ui_reader_session_render(&expected, 10000, 110, 20);
        xp_epub_test_accounting_render(&actual, 10000, 110, 20);
        sameSession(actual, expected);
      }
    }
  }
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "nested") {
    Fixture f;
    RenderGuard lock(f);
    xp_epub_test_publication_render(f.owner, nestedPaint, &f);
    return 1;
  }
  publicationMatrix();
  readyScopeGate();
  accountingBoundaries();
  assert(!active && !modelDepth && !resourceDepth);
}
