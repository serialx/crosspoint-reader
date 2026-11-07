#pragma once
#include <xp_epub_session.h>

#include <cstddef>
#include <type_traits>

#include "legacy_page_fixture.h"
// Explicit slots in the reader boundary.

static_assert(offsetof(XpEpubLeaves, render_margins) == 0 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_margins)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_margins)>>);
static_assert(offsetof(XpEpubLeaves, render_geometry) == 1 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_geometry)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_geometry)>>);
static_assert(offsetof(XpEpubLeaves, render_restore_bw) == 2 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_restore_bw)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_restore_bw)>>);
static_assert(offsetof(XpEpubLeaves, render_discard_bw) == 3 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_discard_bw)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_discard_bw)>>);
static_assert(offsetof(XpEpubLeaves, render_orientation) == 4 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_orientation)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_orientation)>>);
static_assert(offsetof(XpEpubLeaves, loan_count) == 5 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::loan_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::loan_count)>>);

static_assert(offsetof(XpEpubLeaves, book_facade_open) == 6 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_facade_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_facade_open)>>);
static_assert(offsetof(XpEpubLeaves, book_facade_close) == 7 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_facade_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_facade_close)>>);
static_assert(offsetof(XpEpubLeaves, book_set_extractor) == 8 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_set_extractor)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_set_extractor)>>);
static_assert(offsetof(XpEpubLeaves, protection_open) == 9 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_open)>>);
static_assert(offsetof(XpEpubLeaves, protection_close) == 10 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_close)>>);
static_assert(offsetof(XpEpubLeaves, protection_encrypted) == 11 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_encrypted)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_encrypted)>>);
static_assert(offsetof(XpEpubLeaves, protection_size) == 12 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_size)>>);
static_assert(offsetof(XpEpubLeaves, protection_stream) == 13 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_stream)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_stream)>>);
static_assert(offsetof(XpEpubLeaves, file_seek) == 14 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_seek)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_seek)>>);
static_assert(offsetof(XpEpubLeaves, file_size) == 15 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_size)>>);
static_assert(offsetof(XpEpubLeaves, path_remove_tree) == 16 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_remove_tree)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_remove_tree)>>);
static_assert(offsetof(XpEpubLeaves, progress_save) == 17 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::progress_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::progress_save)>>);
static_assert(offsetof(XpEpubLegacyPageServices, page_close) == 0 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::page_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::page_close)>>);
static_assert(offsetof(XpEpubLegacyPageServices, page_info) == 1 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::page_info)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::page_info)>>);
static_assert(offsetof(XpEpubLegacyPageServices, page_element) == 2 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::page_element)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::page_element)>>);
static_assert(offsetof(XpEpubLegacyPageServices, page_word) == 3 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::page_word)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::page_word)>>);
static_assert(offsetof(XpEpubLegacyPageServices, metadata_assign) == 4 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::metadata_assign)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::metadata_assign)>>);
static_assert(offsetof(XpEpubLegacyPageServices, metadata_clear) == 5 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::metadata_clear)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::metadata_clear)>>);
static_assert(offsetof(XpEpubLegacyPageServices, metadata_footnotes) == 6 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::metadata_footnotes)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::metadata_footnotes)>>);
static_assert(offsetof(XpEpubLegacyPageServices, metadata_links) == 7 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::metadata_links)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::metadata_links)>>);
static_assert(offsetof(XpEpubLegacyPageServices, metadata_footnote) == 8 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::metadata_footnote)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::metadata_footnote)>>);
static_assert(offsetof(XpEpubLegacyPageServices, metadata_link) == 9 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::metadata_link)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::metadata_link)>>);
static_assert(offsetof(XpEpubLeaves, paint_dither) == 18 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_dither)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_dither)>>);
static_assert(offsetof(XpEpubLeaves, paint_pixel) == 19 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_pixel)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_pixel)>>);
static_assert(offsetof(XpEpubLeaves, paint_status) == 20 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_status)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_status)>>);
static_assert(offsetof(XpEpubLeaves, image_clear_failures) == 21 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::image_clear_failures)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::image_clear_failures)>>);
static_assert(offsetof(XpEpubLeaves, image_release_cache) == 22 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::image_release_cache)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::image_release_cache)>>);
static_assert(offsetof(XpEpubLeaves, fonts_ensure_loaded) == 23 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_ensure_loaded)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_ensure_loaded)>>);
static_assert(offsetof(XpEpubLeaves, fonts_release_caches) == 24 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_release_caches)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_release_caches)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_begin) == 25 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_begin)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_begin)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_finish_scan) == 26 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_finish_scan)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_finish_scan)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_drop) == 27 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_drop)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_drop)>>);
static_assert(offsetof(XpEpubLeaves, fonts_take_corrupt_notice) == 28 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_take_corrupt_notice)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_take_corrupt_notice)>>);
static_assert(offsetof(XpEpubLeaves, font_sizes) == 29 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::font_sizes)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::font_sizes)>>);
static_assert(offsetof(XpEpubLeaves, settings_snapshot) == 30 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_snapshot)>>);
static_assert(offsetof(XpEpubLeaves, settings_render_spec) == 31 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_render_spec)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_render_spec)>>);
static_assert(offsetof(XpEpubLeaves, settings_orientation) == 32 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_orientation)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_orientation)>>);
static_assert(offsetof(XpEpubLeaves, settings_font_size) == 33 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_font_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_font_size)>>);
static_assert(offsetof(XpEpubLeaves, settings_line_spacing) == 34 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_line_spacing)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_line_spacing)>>);
static_assert(offsetof(XpEpubLeaves, settings_alignment) == 35 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_alignment)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_alignment)>>);
static_assert(offsetof(XpEpubLeaves, settings_focus_reading) == 36 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_focus_reading)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_focus_reading)>>);
static_assert(offsetof(XpEpubLeaves, settings_inversion) == 37 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_inversion)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_inversion)>>);
static_assert(offsetof(XpEpubLeaves, settings_ble_enabled) == 38 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_ble_enabled)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_ble_enabled)>>);
static_assert(offsetof(XpEpubLeaves, settings_save) == 39 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_save)>>);
static_assert(offsetof(XpEpubLeaves, input_services) == 40 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_services)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_services)>>);
static_assert(offsetof(XpEpubLeaves, input_facts) == 41 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_facts)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_facts)>>);
static_assert(offsetof(XpEpubLeaves, input_pressed) == 42 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_pressed)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_pressed)>>);
static_assert(offsetof(XpEpubLeaves, input_screen_point) == 43 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_screen_point)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_screen_point)>>);
static_assert(offsetof(XpEpubLeaves, input_screen_tap) == 44 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_screen_tap)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_screen_tap)>>);
static_assert(offsetof(XpEpubLeaves, input_raw_contact) == 45 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_raw_contact)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_raw_contact)>>);
static_assert(offsetof(XpEpubLeaves, render_busy) == 46 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_busy)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_busy)>>);
static_assert(offsetof(XpEpubLeaves, file_open_read) == 47 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_open_read)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_open_read)>>);
static_assert(offsetof(XpEpubLeaves, file_open_write) == 48 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_open_write)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_open_write)>>);
static_assert(offsetof(XpEpubLeaves, file_read) == 49 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_read)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_read)>>);
static_assert(offsetof(XpEpubLeaves, file_write) == 50 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_write)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_write)>>);
static_assert(offsetof(XpEpubLeaves, file_close) == 51 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_close)>>);
static_assert(offsetof(XpEpubLeaves, path_remove) == 52 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_remove)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_remove)>>);
static_assert(offsetof(XpEpubLeaves, path_rename) == 53 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_rename)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_rename)>>);
static_assert(offsetof(XpEpubLeaves, path_mkdir) == 54 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_mkdir)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_mkdir)>>);
static_assert(offsetof(XpEpubLeaves, native_path_hash) == 55 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::native_path_hash)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::native_path_hash)>>);
static_assert(offsetof(XpEpubLeaves, heap_snapshot) == 56 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::heap_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::heap_snapshot)>>);
static_assert(offsetof(XpEpubLeaves, radio_status) == 57 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_status)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_status)>>);
static_assert(offsetof(XpEpubLeaves, radio_before_build) == 58 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_before_build)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_before_build)>>);
static_assert(offsetof(XpEpubLeaves, radio_settle_start) == 59 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_settle_start)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_settle_start)>>);
static_assert(offsetof(XpEpubLeaves, radio_holds_heap) == 60 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_holds_heap)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_holds_heap)>>);
static_assert(offsetof(XpEpubLeaves, radio_after_paint) == 61 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_after_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_after_paint)>>);
static_assert(offsetof(XpEpubLeaves, radio_switch_off) == 62 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_switch_off)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_switch_off)>>);
static_assert(offsetof(XpEpubLeaves, radio_request_connect) == 63 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_request_connect)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_request_connect)>>);
static_assert(offsetof(XpEpubLeaves, trusted_now) == 64 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::trusted_now)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::trusted_now)>>);
static_assert(offsetof(XpEpubLeaves, trusted_sync) == 65 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::trusted_sync)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::trusted_sync)>>);
static_assert(offsetof(XpEpubLeaves, wifi_connected) == 66 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::wifi_connected)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::wifi_connected)>>);
static_assert(offsetof(XpEpubLeaves, wifi_disconnect) == 67 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::wifi_disconnect)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::wifi_disconnect)>>);
static_assert(offsetof(XpEpubLeaves, restart_reader) == 68 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::restart_reader)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::restart_reader)>>);
static_assert(offsetof(XpEpubLeaves, child_text_settings) == 69 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_text_settings)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_text_settings)>>);
static_assert(offsetof(XpEpubLeaves, child_selection) == 70 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_selection)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_selection)>>);
static_assert(offsetof(XpEpubLeaves, child_wifi) == 71 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_wifi)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_wifi)>>);
static_assert(offsetof(XpEpubLeaves, child_sync) == 72 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_sync)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_sync)>>);
static_assert(offsetof(XpEpubLeaves, popup_open) == 73 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_open)>>);
static_assert(offsetof(XpEpubLeaves, popup_close) == 74 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_close)>>);
static_assert(offsetof(XpEpubLeaves, popup_active) == 75 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_active)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_active)>>);
static_assert(offsetof(XpEpubLeaves, popup_show) == 76 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_show)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_show)>>);
static_assert(offsetof(XpEpubLeaves, popup_dismiss) == 77 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_dismiss)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_dismiss)>>);
static_assert(offsetof(XpEpubLeaves, popup_input) == 78 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_input)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_input)>>);
static_assert(offsetof(XpEpubLeaves, popup_paint) == 79 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_paint)>>);
static_assert(offsetof(XpEpubLeaves, popup_process_render) == 80 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_process_render)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_process_render)>>);
static_assert(offsetof(XpEpubLeaves, end_open) == 81 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_open)>>);
static_assert(offsetof(XpEpubLeaves, end_close) == 82 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_close)>>);
static_assert(offsetof(XpEpubLeaves, end_load) == 83 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_load)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_load)>>);
static_assert(offsetof(XpEpubLeaves, end_menu_active) == 84 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_menu_active)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_menu_active)>>);
static_assert(offsetof(XpEpubLeaves, end_input) == 85 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_input)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_input)>>);
static_assert(offsetof(XpEpubLeaves, end_paint) == 86 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_paint)>>);
static_assert(offsetof(XpEpubLeaves, recents_remove) == 87 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_remove)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_remove)>>);
static_assert(offsetof(XpEpubLeaves, recents_add) == 88 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_add)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_add)>>);
static_assert(offsetof(XpEpubLeaves, recents_update_path) == 89 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_update_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_update_path)>>);
static_assert(offsetof(XpEpubLeaves, state_open_path) == 90 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_open_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_open_path)>>);
static_assert(offsetof(XpEpubLeaves, state_set_open_path) == 91 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_set_open_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_set_open_path)>>);
static_assert(offsetof(XpEpubLeaves, state_reset_reader_count) == 92 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_reset_reader_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_reset_reader_count)>>);
static_assert(offsetof(XpEpubLeaves, state_save) == 93 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_save)>>);
static_assert(offsetof(XpEpubLeaves, has_sync_credentials) == 94 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_sync_credentials)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_sync_credentials)>>);
static_assert(offsetof(XpEpubLeaves, has_exit_subscriber) == 95 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_exit_subscriber)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_exit_subscriber)>>);
static_assert(offsetof(XpEpubLeaves, has_session_subscriber) == 96 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_session_subscriber)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_session_subscriber)>>);
static_assert(offsetof(XpEpubLeaves, document_id) == 97 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::document_id)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::document_id)>>);
static_assert(offsetof(XpEpubLeaves, emit_open) == 98 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_open)>>);
static_assert(offsetof(XpEpubLeaves, emit_exit) == 99 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_exit)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_exit)>>);
static_assert(offsetof(XpEpubLeaves, emit_session) == 100 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_session)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_session)>>);
static_assert(offsetof(XpEpubLeaves, log) == 101 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::log)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::log)>>);
static_assert(offsetof(XpEpubLegacyPageServices, page_word_font) == 10 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::page_word_font)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::page_word_font)>>);
static_assert(offsetof(XpEpubLegacyPageServices, page_line_font) == 11 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::page_line_font)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::page_line_font)>>);
static_assert(offsetof(XpEpubLegacyPageServices, page_word_y_offset) == 12 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::page_word_y_offset)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::page_word_y_offset)>>);
static_assert(offsetof(XpEpubLegacyPageServices, page_line_ruby_shift) == 13 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::page_line_ruby_shift)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::page_line_ruby_shift)>>);
static_assert(offsetof(XpEpubLeaves, font_text_advance) == 102 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::font_text_advance)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::font_text_advance)>>);
static_assert(offsetof(XpEpubLeaves, log_enabled) == 103 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::log_enabled)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::log_enabled)>>);
static_assert(offsetof(XpEpubLeaves, input_snapshot) == 104 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_snapshot)>>);
static_assert(offsetof(XpEpubLegacyPageServices, page_render) == 14 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::page_render)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::page_render)>>);
static_assert(offsetof(XpEpubLeaves, cache_lock) == 105 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::cache_lock)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::cache_lock)>>);
static_assert(offsetof(XpEpubLeaves, cache_unlock) == 106 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::cache_unlock)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::cache_unlock)>>);
static_assert(offsetof(XpEpubLeaves, scratch_claim) == 107 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::scratch_claim)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::scratch_claim)>>);
static_assert(offsetof(XpEpubLeaves, scratch_release) == 108 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::scratch_release)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::scratch_release)>>);
static_assert(offsetof(XpEpubLeaves, inflate_oom_log) == 109 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::inflate_oom_log)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::inflate_oom_log)>>);
static_assert(sizeof(XpEpubLeaves) == 158 * sizeof(void (*)()));
static_assert(alignof(XpEpubLeaves) == alignof(void (*)()));
static_assert(sizeof(XpAppResultView::kind) == 1);
static_assert(sizeof(XpAppResultView::cancelled) == 1);

// The Legacy table includes three temporary Definition-selection callbacks.
static_assert(sizeof(XpAppLegacy) == 17 * sizeof(void*));
static_assert(offsetof(XpAppLegacy, reader_turn) == 12 * sizeof(void*));
static_assert(offsetof(XpAppLegacy, screenshot_info) == 13 * sizeof(void*));
static_assert(sizeof(XpAppPlatform) == 4 * sizeof(void*));
static_assert(offsetof(XpAppPlatform, epub) == 3 * sizeof(void*));
static_assert(sizeof(XpUiReaderRenderServices) == 3 * sizeof(void*));
static_assert(std::is_same_v<decltype(XpUiReaderRenderServices::read), uint32_t (*)(void*, uint8_t)>);
static_assert(
    std::is_same_v<decltype(XpUiReaderRenderServices::execute), uint32_t (*)(void*, const XpUiReaderRenderOp*)>);

static_assert(offsetof(XpEpubLeaves, build_metrics) == 110 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, build_allocator) == 111 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_variant) == 112 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_line_height) == 113 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_ready) == 114 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_codepoint) == 115 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, hyphen_language) == 116 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, file_position) == 117 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, file_flush) == 118 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, image_supported) == 119 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, image_dimensions) == 120 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyPageServices, page_load) == 15 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, framebuffer_lend) == 121 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, framebuffer_restore) == 122 * sizeof(void (*)()));

static_assert(offsetof(XpEpubLeaves, paint_arena) == 123 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_arena)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_arena)>>);

static_assert(offsetof(XpEpubLeaves, image_needs_decode) == 124 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::image_needs_decode)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::image_needs_decode)>>);

static_assert(offsetof(XpEpubLeaves, font_ascent) == 125 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::font_ascent)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::font_ascent)>>);
template <class T, class = void>
struct Has_page_close : std::false_type {};
template <class T>
struct Has_page_close<T, std::void_t<decltype(&T::page_close)>> : std::true_type {};
static_assert(!Has_page_close<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_page_info : std::false_type {};
template <class T>
struct Has_page_info<T, std::void_t<decltype(&T::page_info)>> : std::true_type {};
static_assert(!Has_page_info<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_page_element : std::false_type {};
template <class T>
struct Has_page_element<T, std::void_t<decltype(&T::page_element)>> : std::true_type {};
static_assert(!Has_page_element<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_page_word : std::false_type {};
template <class T>
struct Has_page_word<T, std::void_t<decltype(&T::page_word)>> : std::true_type {};
static_assert(!Has_page_word<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_metadata_assign : std::false_type {};
template <class T>
struct Has_metadata_assign<T, std::void_t<decltype(&T::metadata_assign)>> : std::true_type {};
static_assert(!Has_metadata_assign<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_metadata_clear : std::false_type {};
template <class T>
struct Has_metadata_clear<T, std::void_t<decltype(&T::metadata_clear)>> : std::true_type {};
static_assert(!Has_metadata_clear<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_metadata_footnotes : std::false_type {};
template <class T>
struct Has_metadata_footnotes<T, std::void_t<decltype(&T::metadata_footnotes)>> : std::true_type {};
static_assert(!Has_metadata_footnotes<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_metadata_links : std::false_type {};
template <class T>
struct Has_metadata_links<T, std::void_t<decltype(&T::metadata_links)>> : std::true_type {};
static_assert(!Has_metadata_links<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_metadata_footnote : std::false_type {};
template <class T>
struct Has_metadata_footnote<T, std::void_t<decltype(&T::metadata_footnote)>> : std::true_type {};
static_assert(!Has_metadata_footnote<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_metadata_link : std::false_type {};
template <class T>
struct Has_metadata_link<T, std::void_t<decltype(&T::metadata_link)>> : std::true_type {};
static_assert(!Has_metadata_link<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_page_word_font : std::false_type {};
template <class T>
struct Has_page_word_font<T, std::void_t<decltype(&T::page_word_font)>> : std::true_type {};
static_assert(!Has_page_word_font<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_page_line_font : std::false_type {};
template <class T>
struct Has_page_line_font<T, std::void_t<decltype(&T::page_line_font)>> : std::true_type {};
static_assert(!Has_page_line_font<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_page_word_y_offset : std::false_type {};
template <class T>
struct Has_page_word_y_offset<T, std::void_t<decltype(&T::page_word_y_offset)>> : std::true_type {};
static_assert(!Has_page_word_y_offset<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_page_line_ruby_shift : std::false_type {};
template <class T>
struct Has_page_line_ruby_shift<T, std::void_t<decltype(&T::page_line_ruby_shift)>> : std::true_type {};
static_assert(!Has_page_line_ruby_shift<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_page_render : std::false_type {};
template <class T>
struct Has_page_render<T, std::void_t<decltype(&T::page_render)>> : std::true_type {};
static_assert(!Has_page_render<XpEpubLeaves>::value);
template <class T, class = void>
struct Has_page_load : std::false_type {};
template <class T>
struct Has_page_load<T, std::void_t<decltype(&T::page_load)>> : std::true_type {};
static_assert(!Has_page_load<XpEpubLeaves>::value);

static_assert(offsetof(XpEpubLeaves, render_physical_query) == 126 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, render_physical_operation) == 127 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, render_plan) == 128 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, image_read_window) == 129 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, display_source) == 130 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, framebuffer_view) == 131 * sizeof(void (*)()));
