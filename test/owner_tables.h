#pragma once
#include <xp_epub_session.h>

#include <cassert>
#include <cstdlib>

#include "reader_lifecycle/legacy_book_fixture.h"
namespace retirement_fixture {
template <typename Signature>
struct Unexpected;
template <typename Result, typename... Args>
struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) {
    assert(false && "unexpected retirement fixture leaf");
    std::abort();
  }
};
inline unsigned modelDepth;
#define UNUSED_LEAF(field) table.field = &Unexpected<decltype(table.field)>::call
inline XpAppResources resourcesDefaults() {
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

inline XpAppRuntime runtimeDefaults() {
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

inline XpEpubLeaves leavesDefaults() {
  XpEpubLeaves table{};
  static_assert(sizeof(XpEpubLeaves) == 167 * sizeof(void (*)()));
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

  UNUSED_LEAF(font_text_advance);
  UNUSED_LEAF(log_enabled);
  UNUSED_LEAF(toolbar_reset_navigation);
  UNUSED_LEAF(input_snapshot);

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

  UNUSED_LEAF(framebuffer_lend);
  UNUSED_LEAF(framebuffer_restore);
  UNUSED_LEAF(paint_arena);
  UNUSED_LEAF(image_needs_decode);
  UNUSED_LEAF(font_ascent);
  UNUSED_LEAF(book_facade_open);
  UNUSED_LEAF(book_facade_close);
  UNUSED_LEAF(book_set_extractor);
  return table;
}

inline LegacyEpubLeaves legacyLeavesDefaults() {
  LegacyEpubLeaves table{};
  table.physical = leavesDefaults();
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
  UNUSED_LEAF(book.bookmarks_load);
  UNUSED_LEAF(book.bookmarks_save);
  UNUSED_LEAF(book.clippings_load);
  UNUSED_LEAF(book.recents_add);
  return table;
}

#undef UNUSED_LEAF
inline XpAppRuntime modelRuntime() {
  auto runtime = runtimeDefaults();
  runtime.model_lock = [] {
    assert(modelDepth == 0);
    ++modelDepth;
  };
  runtime.model_unlock = [] {
    assert(modelDepth == 1);
    --modelDepth;
  };
  return runtime;
}
}  // namespace retirement_fixture
