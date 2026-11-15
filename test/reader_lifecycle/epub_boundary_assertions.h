#include "epub_fixture_abi.h"
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

static_assert(offsetof(XpEpubLeaves, protection_open) == 6 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_open)>>);
static_assert(offsetof(XpEpubLeaves, protection_close) == 7 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_close)>>);
static_assert(offsetof(XpEpubLeaves, protection_encrypted) == 8 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_encrypted)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_encrypted)>>);
static_assert(offsetof(XpEpubLeaves, protection_size) == 9 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_size)>>);
static_assert(offsetof(XpEpubLeaves, protection_stream) == 10 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::protection_stream)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::protection_stream)>>);
static_assert(offsetof(XpEpubLeaves, file_seek) == 11 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_seek)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_seek)>>);
static_assert(offsetof(XpEpubLeaves, file_size) == 12 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_size)>>);
static_assert(offsetof(XpEpubLeaves, path_remove_tree) == 13 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_remove_tree)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_remove_tree)>>);
static_assert(offsetof(XpEpubLeaves, storage_read_release) == 14 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::storage_read_release)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::storage_read_release)>>);
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
static_assert(offsetof(XpEpubLeaves, paint_dither) == 15 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_dither)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_dither)>>);
static_assert(offsetof(XpEpubLeaves, paint_pixel) == 16 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_pixel)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_pixel)>>);
static_assert(offsetof(XpEpubLeaves, paint_status) == 17 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_status)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_status)>>);
static_assert(offsetof(XpEpubLeaves, fonts_ensure_loaded) == 18 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_ensure_loaded)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_ensure_loaded)>>);
static_assert(offsetof(XpEpubLeaves, fonts_release_caches) == 19 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_release_caches)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_release_caches)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_begin) == 20 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_begin)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_begin)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_finish_scan) == 21 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_finish_scan)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_finish_scan)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_drop) == 22 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_drop)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_drop)>>);
static_assert(offsetof(XpEpubLeaves, fonts_take_corrupt_notice) == 23 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_take_corrupt_notice)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_take_corrupt_notice)>>);
#ifndef XP_SD_FONT_CATALOG
static_assert(offsetof(XpEpubLeaves, font_sizes) == 24 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::font_sizes)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::font_sizes)>>);
#endif
static_assert(offsetof(XpEpubLeaves, settings_snapshot) == xpTestEpubLeafSlot(25) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_snapshot)>>);
static_assert(offsetof(XpEpubLeaves, settings_render_spec) == xpTestEpubLeafSlot(26) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_render_spec)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_render_spec)>>);
static_assert(offsetof(XpEpubLeaves, settings_orientation) == xpTestEpubLeafSlot(27) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_orientation)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_orientation)>>);
static_assert(offsetof(XpEpubLeaves, settings_font_size) == xpTestEpubLeafSlot(28) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_font_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_font_size)>>);
static_assert(offsetof(XpEpubLeaves, settings_line_spacing) == xpTestEpubLeafSlot(29) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_line_spacing)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_line_spacing)>>);
static_assert(offsetof(XpEpubLeaves, settings_alignment) == xpTestEpubLeafSlot(30) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_alignment)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_alignment)>>);
static_assert(offsetof(XpEpubLeaves, settings_focus_reading) == xpTestEpubLeafSlot(31) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_focus_reading)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_focus_reading)>>);
static_assert(offsetof(XpEpubLeaves, settings_inversion) == xpTestEpubLeafSlot(32) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_inversion)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_inversion)>>);
static_assert(offsetof(XpEpubLeaves, settings_ble_enabled) == xpTestEpubLeafSlot(33) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_ble_enabled)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_ble_enabled)>>);
static_assert(offsetof(XpEpubLeaves, settings_save) == xpTestEpubLeafSlot(34) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_save)>>);
static_assert(offsetof(XpEpubLeaves, input_services) == xpTestEpubLeafSlot(35) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_services)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_services)>>);
static_assert(offsetof(XpEpubLeaves, input_facts) == xpTestEpubLeafSlot(36) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_facts)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_facts)>>);
static_assert(offsetof(XpEpubLeaves, input_pressed) == xpTestEpubLeafSlot(37) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_pressed)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_pressed)>>);
static_assert(offsetof(XpEpubLeaves, input_screen_point) == xpTestEpubLeafSlot(38) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_screen_point)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_screen_point)>>);
static_assert(offsetof(XpEpubLeaves, input_screen_tap) == xpTestEpubLeafSlot(39) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_screen_tap)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_screen_tap)>>);
static_assert(offsetof(XpEpubLeaves, input_raw_contact) == xpTestEpubLeafSlot(40) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_raw_contact)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_raw_contact)>>);
static_assert(offsetof(XpEpubLeaves, render_busy) == xpTestEpubLeafSlot(41) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_busy)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_busy)>>);
static_assert(offsetof(XpEpubLeaves, file_open_read) == xpTestEpubLeafSlot(42) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_open_read)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_open_read)>>);
static_assert(offsetof(XpEpubLeaves, file_open_write) == xpTestEpubLeafSlot(43) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_open_write)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_open_write)>>);
static_assert(offsetof(XpEpubLeaves, file_read) == xpTestEpubLeafSlot(44) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_read)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_read)>>);
static_assert(offsetof(XpEpubLeaves, file_write) == xpTestEpubLeafSlot(45) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_write)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_write)>>);
static_assert(offsetof(XpEpubLeaves, file_close) == xpTestEpubLeafSlot(46) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_close)>>);
static_assert(offsetof(XpEpubLeaves, path_remove) == xpTestEpubLeafSlot(47) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_remove)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_remove)>>);
static_assert(offsetof(XpEpubLeaves, path_rename) == xpTestEpubLeafSlot(48) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_rename)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_rename)>>);
static_assert(offsetof(XpEpubLeaves, path_mkdir) == xpTestEpubLeafSlot(49) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_mkdir)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_mkdir)>>);
static_assert(offsetof(XpEpubLeaves, native_path_hash) == xpTestEpubLeafSlot(50) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::native_path_hash)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::native_path_hash)>>);
static_assert(offsetof(XpEpubLeaves, heap_snapshot) == xpTestEpubLeafSlot(51) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::heap_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::heap_snapshot)>>);
static_assert(offsetof(XpEpubLeaves, radio_status) == xpTestEpubLeafSlot(52) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_status)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_status)>>);
static_assert(offsetof(XpEpubLeaves, radio_before_build) == xpTestEpubLeafSlot(53) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_before_build)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_before_build)>>);
static_assert(offsetof(XpEpubLeaves, radio_settle_start) == xpTestEpubLeafSlot(54) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_settle_start)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_settle_start)>>);
static_assert(offsetof(XpEpubLeaves, radio_holds_heap) == xpTestEpubLeafSlot(55) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_holds_heap)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_holds_heap)>>);
static_assert(offsetof(XpEpubLeaves, radio_after_paint) == xpTestEpubLeafSlot(56) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_after_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_after_paint)>>);
static_assert(offsetof(XpEpubLeaves, radio_switch_off) == xpTestEpubLeafSlot(57) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_switch_off)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_switch_off)>>);
static_assert(offsetof(XpEpubLeaves, radio_request_connect) == xpTestEpubLeafSlot(58) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_request_connect)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_request_connect)>>);
static_assert(offsetof(XpEpubLeaves, trusted_now) == xpTestEpubLeafSlot(59) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::trusted_now)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::trusted_now)>>);
static_assert(offsetof(XpEpubLeaves, trusted_sync) == xpTestEpubLeafSlot(60) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::trusted_sync)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::trusted_sync)>>);
static_assert(offsetof(XpEpubLeaves, wifi_connected) == xpTestEpubLeafSlot(61) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::wifi_connected)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::wifi_connected)>>);
static_assert(offsetof(XpEpubLeaves, wifi_disconnect) == xpTestEpubLeafSlot(62) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::wifi_disconnect)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::wifi_disconnect)>>);
static_assert(offsetof(XpEpubLeaves, restart_reader) == xpTestEpubLeafSlot(63) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::restart_reader)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::restart_reader)>>);
static_assert(offsetof(XpEpubLeaves, child_text_settings) == xpTestEpubLeafSlot(64) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_text_settings)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_text_settings)>>);
static_assert(offsetof(XpEpubLeaves, child_selection) == xpTestEpubLeafSlot(65) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_selection)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_selection)>>);
static_assert(offsetof(XpEpubLeaves, child_wifi) == xpTestEpubLeafSlot(66) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_wifi)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_wifi)>>);
static_assert(offsetof(XpEpubLeaves, child_sync) == xpTestEpubLeafSlot(67) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_sync)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_sync)>>);
static_assert(offsetof(XpEpubLeaves, popup_open) == xpTestEpubLeafSlot(68) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_open)>>);
static_assert(offsetof(XpEpubLeaves, popup_close) == xpTestEpubLeafSlot(69) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_close)>>);
static_assert(offsetof(XpEpubLeaves, popup_active) == xpTestEpubLeafSlot(70) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_active)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_active)>>);
static_assert(offsetof(XpEpubLeaves, popup_show) == xpTestEpubLeafSlot(71) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_show)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_show)>>);
static_assert(offsetof(XpEpubLeaves, popup_dismiss) == xpTestEpubLeafSlot(72) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_dismiss)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_dismiss)>>);
static_assert(offsetof(XpEpubLeaves, popup_input) == xpTestEpubLeafSlot(73) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_input)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_input)>>);
static_assert(offsetof(XpEpubLeaves, popup_paint) == xpTestEpubLeafSlot(74) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_paint)>>);
static_assert(offsetof(XpEpubLeaves, popup_process_render) == xpTestEpubLeafSlot(75) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_process_render)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_process_render)>>);
static_assert(offsetof(XpEpubLeaves, end_open) == xpTestEpubLeafSlot(76) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_open)>>);
static_assert(offsetof(XpEpubLeaves, end_close) == xpTestEpubLeafSlot(77) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_close)>>);
static_assert(offsetof(XpEpubLeaves, end_load) == xpTestEpubLeafSlot(78) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_load)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_load)>>);
static_assert(offsetof(XpEpubLeaves, end_menu_active) == xpTestEpubLeafSlot(79) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_menu_active)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_menu_active)>>);
static_assert(offsetof(XpEpubLeaves, end_input) == xpTestEpubLeafSlot(80) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_input)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_input)>>);
static_assert(offsetof(XpEpubLeaves, end_paint) == xpTestEpubLeafSlot(81) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_paint)>>);
static_assert(offsetof(XpEpubLeaves, recents_remove) == xpTestEpubLeafSlot(82) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_remove)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_remove)>>);
static_assert(offsetof(XpEpubLeaves, recents_add) == xpTestEpubLeafSlot(83) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_add)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_add)>>);
static_assert(offsetof(XpEpubLeaves, recents_update_path) == xpTestEpubLeafSlot(84) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_update_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_update_path)>>);
static_assert(offsetof(XpEpubLeaves, state_open_path) == xpTestEpubLeafSlot(85) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_open_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_open_path)>>);
static_assert(offsetof(XpEpubLeaves, state_set_open_path) == xpTestEpubLeafSlot(86) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_set_open_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_set_open_path)>>);
static_assert(offsetof(XpEpubLeaves, state_reset_reader_count) == xpTestEpubLeafSlot(87) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_reset_reader_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_reset_reader_count)>>);
static_assert(offsetof(XpEpubLeaves, state_save) == xpTestEpubLeafSlot(88) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_save)>>);
static_assert(offsetof(XpEpubLeaves, has_sync_credentials) == xpTestEpubLeafSlot(89) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_sync_credentials)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_sync_credentials)>>);
static_assert(offsetof(XpEpubLeaves, has_exit_subscriber) == xpTestEpubLeafSlot(90) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_exit_subscriber)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_exit_subscriber)>>);
static_assert(offsetof(XpEpubLeaves, has_session_subscriber) == xpTestEpubLeafSlot(91) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_session_subscriber)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_session_subscriber)>>);
static_assert(offsetof(XpEpubLeaves, document_id) == xpTestEpubLeafSlot(92) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::document_id)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::document_id)>>);
static_assert(offsetof(XpEpubLeaves, emit_open) == xpTestEpubLeafSlot(93) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_open)>>);
static_assert(offsetof(XpEpubLeaves, emit_exit) == xpTestEpubLeafSlot(94) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_exit)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_exit)>>);
static_assert(offsetof(XpEpubLeaves, emit_session) == xpTestEpubLeafSlot(95) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_session)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_session)>>);
static_assert(offsetof(XpEpubLeaves, log) == xpTestEpubLeafSlot(96) * sizeof(void (*)()));
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
static_assert(offsetof(XpEpubLeaves, font_text_advance) == xpTestEpubLeafSlot(97) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::font_text_advance)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::font_text_advance)>>);
static_assert(offsetof(XpEpubLeaves, log_enabled) == xpTestEpubLeafSlot(98) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::log_enabled)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::log_enabled)>>);
static_assert(offsetof(XpEpubLeaves, input_snapshot) == xpTestEpubLeafSlot(99) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_snapshot)>>);
static_assert(offsetof(XpEpubLegacyPageServices, page_render) == 14 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLegacyPageServices::page_render)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLegacyPageServices::page_render)>>);
static_assert(offsetof(XpEpubLeaves, cache_lock) == xpTestEpubLeafSlot(100) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::cache_lock)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::cache_lock)>>);
static_assert(offsetof(XpEpubLeaves, cache_unlock) == xpTestEpubLeafSlot(101) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::cache_unlock)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::cache_unlock)>>);
static_assert(offsetof(XpEpubLeaves, scratch_claim) == xpTestEpubLeafSlot(102) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::scratch_claim)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::scratch_claim)>>);
static_assert(offsetof(XpEpubLeaves, scratch_release) == xpTestEpubLeafSlot(103) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::scratch_release)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::scratch_release)>>);
static_assert(offsetof(XpEpubLeaves, inflate_oom_log) == xpTestEpubLeafSlot(104) * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::inflate_oom_log)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::inflate_oom_log)>>);
static_assert(sizeof(XpEpubLeaves) == xpTestEpubLeafCount * sizeof(void (*)()));
static_assert(alignof(XpEpubLeaves) == alignof(void (*)()));
static_assert(sizeof(XpAppResultView::kind) == 1);
static_assert(sizeof(XpAppResultView::cancelled) == 1);

// Definition selection uses the generic result path; Legacy has 14 callbacks.
static_assert(sizeof(XpAppLegacy) == 14 * sizeof(void*));
static_assert(offsetof(XpAppLegacy, reader_turn) == 12 * sizeof(void*));
static_assert(offsetof(XpAppLegacy, screenshot_info) == 13 * sizeof(void*));
static_assert(sizeof(XpAppPlatform) == 4 * sizeof(void*));
static_assert(offsetof(XpAppPlatform, epub) == 3 * sizeof(void*));
static_assert(sizeof(XpUiReaderRenderServices) == 3 * sizeof(void*));
static_assert(std::is_same_v<decltype(XpUiReaderRenderServices::read), uint32_t (*)(void*, uint8_t)>);
static_assert(
    std::is_same_v<decltype(XpUiReaderRenderServices::execute), uint32_t (*)(void*, const XpUiReaderRenderOp*)>);

static_assert(offsetof(XpEpubLeaves, build_metrics) == xpTestEpubLeafSlot(105) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, build_allocator) == xpTestEpubLeafSlot(106) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_variant) == xpTestEpubLeafSlot(107) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_line_height) == xpTestEpubLeafSlot(108) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_ready) == xpTestEpubLeafSlot(109) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, font_codepoint) == xpTestEpubLeafSlot(110) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, hyphen_language) == xpTestEpubLeafSlot(111) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, file_position) == xpTestEpubLeafSlot(112) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, file_flush) == xpTestEpubLeafSlot(113) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLegacyPageServices, page_load) == 15 * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, framebuffer_lend) == xpTestEpubLeafSlot(114) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, framebuffer_restore) == xpTestEpubLeafSlot(115) * sizeof(void (*)()));

static_assert(offsetof(XpEpubLeaves, font_ascent) == xpTestEpubLeafSlot(116) * sizeof(void (*)()));
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

static_assert(offsetof(XpEpubLeaves, render_physical_query) == xpTestEpubLeafSlot(117) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, render_physical_operation) == xpTestEpubLeafSlot(118) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, image_read_window) == xpTestEpubLeafSlot(119) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, display_source) == xpTestEpubLeafSlot(120) * sizeof(void (*)()));
static_assert(offsetof(XpEpubLeaves, framebuffer_view) == xpTestEpubLeafSlot(121) * sizeof(void (*)()));
