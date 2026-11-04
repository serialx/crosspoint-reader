#pragma once
#include <xp_epub_session.h>

#include <cstddef>
#include <type_traits>
// Explicit slots in the reader boundary.
static_assert(offsetof(XpEpubLeaves, resources_open) == 0 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::resources_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::resources_open)>>);
static_assert(offsetof(XpEpubLeaves, resources_close) == 1 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::resources_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::resources_close)>>);
static_assert(offsetof(XpEpubLeaves, render_services) == 2 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_services)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_services)>>);
static_assert(offsetof(XpEpubLeaves, render_bind_page) == 3 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_bind_page)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_bind_page)>>);
static_assert(offsetof(XpEpubLeaves, render_finish_page) == 4 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_finish_page)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_finish_page)>>);
static_assert(offsetof(XpEpubLeaves, render_margins) == 5 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_margins)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_margins)>>);
static_assert(offsetof(XpEpubLeaves, render_geometry) == 6 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_geometry)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_geometry)>>);
static_assert(offsetof(XpEpubLeaves, render_restore_bw) == 7 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_restore_bw)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_restore_bw)>>);
static_assert(offsetof(XpEpubLeaves, render_discard_bw) == 8 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_discard_bw)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_discard_bw)>>);
static_assert(offsetof(XpEpubLeaves, render_orientation) == 9 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_orientation)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_orientation)>>);
static_assert(offsetof(XpEpubLeaves, loan_count) == 10 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::loan_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::loan_count)>>);
static_assert(offsetof(XpEpubLeaves, strip_release_idle) == 11 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::strip_release_idle)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::strip_release_idle)>>);
static_assert(offsetof(XpEpubLeaves, book_facade_open) == 12 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_facade_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_facade_open)>>);
static_assert(offsetof(XpEpubLeaves, book_facade_close) == 13 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_facade_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_facade_close)>>);
static_assert(offsetof(XpEpubLeaves, book_set_extractor) == 14 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_set_extractor)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_set_extractor)>>);
static_assert(offsetof(XpEpubLeaves, protection_open) == 15 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_open)>>);
static_assert(offsetof(XpEpubLeaves, protection_close) == 16 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_close)>>);
static_assert(offsetof(XpEpubLeaves, protection_encrypted) == 17 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_encrypted)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_encrypted)>>);
static_assert(offsetof(XpEpubLeaves, protection_size) == 18 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_size)>>);
static_assert(offsetof(XpEpubLeaves, protection_stream) == 19 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_stream)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_stream)>>);
static_assert(offsetof(XpEpubLeaves, file_seek) == 20 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_seek)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_seek)>>);
static_assert(offsetof(XpEpubLeaves, file_size) == 21 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_size)>>);
static_assert(offsetof(XpEpubLeaves, path_remove_tree) == 22 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_remove_tree)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_remove_tree)>>);
static_assert(offsetof(XpEpubLeaves, progress_to_reader) == 23 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::progress_to_reader)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::progress_to_reader)>>);
static_assert(offsetof(XpEpubLeaves, progress_to_saved) == 24 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::progress_to_saved)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::progress_to_saved)>>);
static_assert(offsetof(XpEpubLeaves, progress_save) == 25 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::progress_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::progress_save)>>);
static_assert(offsetof(XpEpubLeaves, page_close) == 26 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_close)>>);
static_assert(offsetof(XpEpubLeaves, page_info) == 27 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_info)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_info)>>);
static_assert(offsetof(XpEpubLeaves, page_element) == 28 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_element)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_element)>>);
static_assert(offsetof(XpEpubLeaves, page_word) == 29 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_word)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_word)>>);
static_assert(offsetof(XpEpubLeaves, metadata_assign) == 30 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_assign)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_assign)>>);
static_assert(offsetof(XpEpubLeaves, metadata_clear) == 31 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_clear)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_clear)>>);
static_assert(offsetof(XpEpubLeaves, metadata_footnotes) == 32 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_footnotes)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_footnotes)>>);
static_assert(offsetof(XpEpubLeaves, metadata_links) == 33 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_links)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_links)>>);
static_assert(offsetof(XpEpubLeaves, metadata_footnote) == 34 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_footnote)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_footnote)>>);
static_assert(offsetof(XpEpubLeaves, metadata_link) == 35 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_link)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_link)>>);
static_assert(offsetof(XpEpubLeaves, paint_dither) == 36 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_dither)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_dither)>>);
static_assert(offsetof(XpEpubLeaves, paint_pixel) == 37 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_pixel)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_pixel)>>);
static_assert(offsetof(XpEpubLeaves, paint_status) == 38 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_status)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_status)>>);
static_assert(offsetof(XpEpubLeaves, image_clear_failures) == 39 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::image_clear_failures)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::image_clear_failures)>>);
static_assert(offsetof(XpEpubLeaves, image_release_cache) == 40 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::image_release_cache)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::image_release_cache)>>);
static_assert(offsetof(XpEpubLeaves, fonts_ensure_loaded) == 41 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_ensure_loaded)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_ensure_loaded)>>);
static_assert(offsetof(XpEpubLeaves, fonts_release_caches) == 42 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_release_caches)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_release_caches)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_begin) == 43 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_begin)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_begin)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_finish_scan) == 44 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_finish_scan)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_finish_scan)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_drop) == 45 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_drop)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_drop)>>);
static_assert(offsetof(XpEpubLeaves, fonts_take_corrupt_notice) == 46 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_take_corrupt_notice)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_take_corrupt_notice)>>);
static_assert(offsetof(XpEpubLeaves, font_sizes) == 47 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::font_sizes)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::font_sizes)>>);
static_assert(offsetof(XpEpubLeaves, settings_snapshot) == 48 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_snapshot)>>);
static_assert(offsetof(XpEpubLeaves, settings_render_spec) == 49 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_render_spec)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_render_spec)>>);
static_assert(offsetof(XpEpubLeaves, settings_orientation) == 50 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_orientation)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_orientation)>>);
static_assert(offsetof(XpEpubLeaves, settings_font_size) == 51 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_font_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_font_size)>>);
static_assert(offsetof(XpEpubLeaves, settings_line_spacing) == 52 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_line_spacing)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_line_spacing)>>);
static_assert(offsetof(XpEpubLeaves, settings_alignment) == 53 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_alignment)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_alignment)>>);
static_assert(offsetof(XpEpubLeaves, settings_focus_reading) == 54 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_focus_reading)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_focus_reading)>>);
static_assert(offsetof(XpEpubLeaves, settings_inversion) == 55 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_inversion)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_inversion)>>);
static_assert(offsetof(XpEpubLeaves, settings_ble_enabled) == 56 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_ble_enabled)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_ble_enabled)>>);
static_assert(offsetof(XpEpubLeaves, settings_save) == 57 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_save)>>);
static_assert(offsetof(XpEpubLeaves, input_services) == 58 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_services)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_services)>>);
static_assert(offsetof(XpEpubLeaves, input_facts) == 59 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_facts)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_facts)>>);
static_assert(offsetof(XpEpubLeaves, input_pressed) == 60 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_pressed)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_pressed)>>);
static_assert(offsetof(XpEpubLeaves, input_screen_long) == 61 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_screen_long)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_screen_long)>>);
static_assert(offsetof(XpEpubLeaves, input_screen_tap) == 62 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_screen_tap)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_screen_tap)>>);
static_assert(offsetof(XpEpubLeaves, input_raw_contact) == 63 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_raw_contact)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_raw_contact)>>);
static_assert(offsetof(XpEpubLeaves, render_busy) == 64 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_busy)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_busy)>>);
static_assert(offsetof(XpEpubLeaves, file_open_read) == 65 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_open_read)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_open_read)>>);
static_assert(offsetof(XpEpubLeaves, file_open_write) == 66 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_open_write)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_open_write)>>);
static_assert(offsetof(XpEpubLeaves, file_read) == 67 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_read)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_read)>>);
static_assert(offsetof(XpEpubLeaves, file_write) == 68 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_write)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_write)>>);
static_assert(offsetof(XpEpubLeaves, file_close) == 69 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_close)>>);
static_assert(offsetof(XpEpubLeaves, path_remove) == 70 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_remove)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_remove)>>);
static_assert(offsetof(XpEpubLeaves, path_rename) == 71 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_rename)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_rename)>>);
static_assert(offsetof(XpEpubLeaves, path_mkdir) == 72 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_mkdir)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_mkdir)>>);
static_assert(offsetof(XpEpubLeaves, native_path_hash) == 73 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::native_path_hash)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::native_path_hash)>>);
static_assert(offsetof(XpEpubLeaves, heap_snapshot) == 74 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::heap_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::heap_snapshot)>>);
static_assert(offsetof(XpEpubLeaves, radio_status) == 75 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_status)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_status)>>);
static_assert(offsetof(XpEpubLeaves, radio_before_build) == 76 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_before_build)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_before_build)>>);
static_assert(offsetof(XpEpubLeaves, radio_settle_start) == 77 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_settle_start)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_settle_start)>>);
static_assert(offsetof(XpEpubLeaves, radio_holds_heap) == 78 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_holds_heap)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_holds_heap)>>);
static_assert(offsetof(XpEpubLeaves, radio_after_paint) == 79 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_after_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_after_paint)>>);
static_assert(offsetof(XpEpubLeaves, radio_switch_off) == 80 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_switch_off)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_switch_off)>>);
static_assert(offsetof(XpEpubLeaves, radio_request_connect) == 81 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_request_connect)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_request_connect)>>);
static_assert(offsetof(XpEpubLeaves, trusted_now) == 82 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::trusted_now)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::trusted_now)>>);
static_assert(offsetof(XpEpubLeaves, trusted_sync) == 83 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::trusted_sync)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::trusted_sync)>>);
static_assert(offsetof(XpEpubLeaves, wifi_connected) == 84 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::wifi_connected)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::wifi_connected)>>);
static_assert(offsetof(XpEpubLeaves, wifi_disconnect) == 85 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::wifi_disconnect)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::wifi_disconnect)>>);
static_assert(offsetof(XpEpubLeaves, restart_reader) == 86 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::restart_reader)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::restart_reader)>>);
static_assert(offsetof(XpEpubLeaves, child_menu) == 87 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_menu)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_menu)>>);
static_assert(offsetof(XpEpubLeaves, child_chapter) == 88 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_chapter)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_chapter)>>);
static_assert(offsetof(XpEpubLeaves, child_percent) == 89 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_percent)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_percent)>>);
static_assert(offsetof(XpEpubLeaves, child_text_settings) == 90 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_text_settings)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_text_settings)>>);
static_assert(offsetof(XpEpubLeaves, child_footnote) == 91 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_footnote)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_footnote)>>);
static_assert(offsetof(XpEpubLeaves, child_dictionary) == 92 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_dictionary)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_dictionary)>>);
static_assert(offsetof(XpEpubLeaves, child_selection) == 93 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_selection)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_selection)>>);
static_assert(offsetof(XpEpubLeaves, child_bookmarks) == 94 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_bookmarks)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_bookmarks)>>);
static_assert(offsetof(XpEpubLeaves, child_clippings) == 95 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_clippings)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_clippings)>>);
static_assert(offsetof(XpEpubLeaves, child_qr) == 96 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_qr)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_qr)>>);
static_assert(offsetof(XpEpubLeaves, child_wifi) == 97 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_wifi)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_wifi)>>);
static_assert(offsetof(XpEpubLeaves, child_sync) == 98 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_sync)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_sync)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_open) == 99 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_open)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_close) == 100 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_close)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_begin) == 101 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_begin)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_begin)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_paint) == 102 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_paint)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_route) == 103 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_route)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_route)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_scroll) == 104 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_scroll)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_scroll)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_visible_rows) == 105 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_visible_rows)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_visible_rows)>>);
static_assert(offsetof(XpEpubLeaves, popup_open) == 106 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_open)>>);
static_assert(offsetof(XpEpubLeaves, popup_close) == 107 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_close)>>);
static_assert(offsetof(XpEpubLeaves, popup_active) == 108 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_active)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_active)>>);
static_assert(offsetof(XpEpubLeaves, popup_show) == 109 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_show)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_show)>>);
static_assert(offsetof(XpEpubLeaves, popup_dismiss) == 110 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_dismiss)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_dismiss)>>);
static_assert(offsetof(XpEpubLeaves, popup_input) == 111 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_input)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_input)>>);
static_assert(offsetof(XpEpubLeaves, popup_paint) == 112 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_paint)>>);
static_assert(offsetof(XpEpubLeaves, popup_process_render) == 113 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_process_render)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_process_render)>>);
static_assert(offsetof(XpEpubLeaves, end_open) == 114 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_open)>>);
static_assert(offsetof(XpEpubLeaves, end_close) == 115 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_close)>>);
static_assert(offsetof(XpEpubLeaves, end_load) == 116 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_load)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_load)>>);
static_assert(offsetof(XpEpubLeaves, end_menu_active) == 117 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_menu_active)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_menu_active)>>);
static_assert(offsetof(XpEpubLeaves, end_input) == 118 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_input)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_input)>>);
static_assert(offsetof(XpEpubLeaves, end_paint) == 119 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_paint)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_open) == 120 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_open)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_close) == 121 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_close)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_clear) == 122 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_clear)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_clear)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_load) == 123 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_load)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_load)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_save) == 124 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_save)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_count) == 125 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_count)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_row) == 126 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_row)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_row)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_retain) == 127 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_retain)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_retain)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_insert_front) == 128 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_insert_front)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_insert_front)>>);
static_assert(offsetof(XpEpubLeaves, clippings_load) == 129 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_load)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_load)>>);
static_assert(offsetof(XpEpubLeaves, clippings_unload) == 130 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_unload)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_unload)>>);
static_assert(offsetof(XpEpubLeaves, clippings_count) == 131 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_count)>>);
static_assert(offsetof(XpEpubLeaves, clippings_row) == 132 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_row)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_row)>>);
static_assert(offsetof(XpEpubLeaves, clippings_remove) == 133 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_remove)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_remove)>>);
static_assert(offsetof(XpEpubLeaves, clippings_insert) == 134 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_insert)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_insert)>>);
static_assert(offsetof(XpEpubLeaves, clippings_export) == 135 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_export)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_export)>>);
static_assert(offsetof(XpEpubLeaves, clippings_move_book) == 136 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_move_book)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_move_book)>>);
static_assert(offsetof(XpEpubLeaves, recents_remove) == 137 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_remove)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_remove)>>);
static_assert(offsetof(XpEpubLeaves, recents_add) == 138 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_add)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_add)>>);
static_assert(offsetof(XpEpubLeaves, recents_update_path) == 139 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_update_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_update_path)>>);
static_assert(offsetof(XpEpubLeaves, state_open_path) == 140 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_open_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_open_path)>>);
static_assert(offsetof(XpEpubLeaves, state_set_open_path) == 141 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_set_open_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_set_open_path)>>);
static_assert(offsetof(XpEpubLeaves, state_reset_reader_count) == 142 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_reset_reader_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_reset_reader_count)>>);
static_assert(offsetof(XpEpubLeaves, state_save) == 143 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_save)>>);
static_assert(offsetof(XpEpubLeaves, has_sync_credentials) == 144 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_sync_credentials)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_sync_credentials)>>);
static_assert(offsetof(XpEpubLeaves, has_exit_subscriber) == 145 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_exit_subscriber)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_exit_subscriber)>>);
static_assert(offsetof(XpEpubLeaves, has_session_subscriber) == 146 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_session_subscriber)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_session_subscriber)>>);
static_assert(offsetof(XpEpubLeaves, document_id) == 147 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::document_id)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::document_id)>>);
static_assert(offsetof(XpEpubLeaves, emit_open) == 148 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_open)>>);
static_assert(offsetof(XpEpubLeaves, emit_exit) == 149 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_exit)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_exit)>>);
static_assert(offsetof(XpEpubLeaves, emit_session) == 150 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_session)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_session)>>);
static_assert(offsetof(XpEpubLeaves, log) == 151 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::log)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::log)>>);
static_assert(offsetof(XpEpubLeaves, page_word_font) == 152 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_word_font)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_word_font)>>);
static_assert(offsetof(XpEpubLeaves, page_line_font) == 153 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_line_font)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_line_font)>>);
static_assert(offsetof(XpEpubLeaves, page_word_y_offset) == 154 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_word_y_offset)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_word_y_offset)>>);
static_assert(offsetof(XpEpubLeaves, page_line_ruby_shift) == 155 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_line_ruby_shift)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_line_ruby_shift)>>);
static_assert(offsetof(XpEpubLeaves, font_text_advance) == 156 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::font_text_advance)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::font_text_advance)>>);
static_assert(offsetof(XpEpubLeaves, log_enabled) == 157 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::log_enabled)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::log_enabled)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_reset_navigation) == 158 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_reset_navigation)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_reset_navigation)>>);
static_assert(offsetof(XpEpubLeaves, input_snapshot) == 159 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_snapshot)>>);
static_assert(offsetof(XpEpubLeaves, page_render) == 160 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_render)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_render)>>);
static_assert(offsetof(XpEpubLeaves, cache_lock) == 161 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::cache_lock)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::cache_lock)>>);
static_assert(offsetof(XpEpubLeaves, cache_unlock) == 162 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::cache_unlock)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::cache_unlock)>>);
static_assert(offsetof(XpEpubLeaves, scratch_claim) == 163 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::scratch_claim)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::scratch_claim)>>);
static_assert(offsetof(XpEpubLeaves, scratch_release) == 164 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::scratch_release)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::scratch_release)>>);
static_assert(offsetof(XpEpubLeaves, inflate_oom_log) == 165 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::inflate_oom_log)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::inflate_oom_log)>>);
static_assert(sizeof(XpEpubLeaves) == 180 * sizeof(void (*)()));
static_assert(alignof(XpEpubLeaves) == alignof(void (*)()));
static_assert(sizeof(XpAppResultView::kind) == 1);
static_assert(sizeof(XpAppResultView::cancelled) == 1);

// The native shortcut slot retires with the legacy EPUB owner.
static_assert(sizeof(XpAppLegacy) == 14 * sizeof(void*));
static_assert(offsetof(XpAppLegacy, reader_turn) == 12 * sizeof(void*));
static_assert(offsetof(XpAppLegacy, screenshot_info) == 13 * sizeof(void*));
static_assert(sizeof(XpAppPlatform) == 4 * sizeof(void*));
static_assert(offsetof(XpAppPlatform, epub) == 3 * sizeof(void*));
static_assert(sizeof(XpUiReaderRenderServices) == 3 * sizeof(void*));
static_assert(std::is_same_v<decltype(XpUiReaderRenderServices::read), uint32_t (*)(void*, uint8_t)>);
static_assert(
    std::is_same_v<decltype(XpUiReaderRenderServices::execute), uint32_t (*)(void*, const XpUiReaderRenderOp*)>);

static_assert(offsetof(XpEpubLeaves, build_metrics) == 166 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, build_allocator) == 167 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_variant) == 168 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_line_height) == 169 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_ready) == 170 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_codepoint) == 171 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, hyphen_language) == 172 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, file_position) == 173 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, file_flush) == 174 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, image_supported) == 175 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, image_dimensions) == 176 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, page_load) == 177 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, framebuffer_lend) == 178 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, framebuffer_restore) == 179 * sizeof(void (*)()));
