#pragma once
#include <xp_epub_session.h>

#include <cstddef>
#include <type_traits>
// Explicit draft slots, copied from the reviewed current table.
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
static_assert(offsetof(XpEpubLeaves, loan_begin) == 10 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::loan_begin)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::loan_begin)>>);
static_assert(offsetof(XpEpubLeaves, loan_end) == 11 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::loan_end)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::loan_end)>>);
static_assert(offsetof(XpEpubLeaves, loan_drop) == 12 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::loan_drop)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::loan_drop)>>);
static_assert(offsetof(XpEpubLeaves, loan_count) == 13 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::loan_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::loan_count)>>);
static_assert(offsetof(XpEpubLeaves, strip_release_idle) == 14 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::strip_release_idle)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::strip_release_idle)>>);
static_assert(offsetof(XpEpubLeaves, book_open) == 15 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_open)>>);
static_assert(offsetof(XpEpubLeaves, book_load) == 16 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_load)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_load)>>);
static_assert(offsetof(XpEpubLeaves, book_close) == 17 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_close)>>);
static_assert(offsetof(XpEpubLeaves, book_size) == 18 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_size)>>);
static_assert(offsetof(XpEpubLeaves, book_spine_count) == 19 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_spine_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_spine_count)>>);
static_assert(offsetof(XpEpubLeaves, book_toc_count) == 20 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_toc_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_toc_count)>>);
static_assert(offsetof(XpEpubLeaves, book_path) == 21 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_path)>>);
static_assert(offsetof(XpEpubLeaves, book_cache_path) == 22 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_cache_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_cache_path)>>);
static_assert(offsetof(XpEpubLeaves, book_title) == 23 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_title)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_title)>>);
static_assert(offsetof(XpEpubLeaves, book_author) == 24 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_author)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_author)>>);
static_assert(offsetof(XpEpubLeaves, book_language) == 25 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_language)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_language)>>);
static_assert(offsetof(XpEpubLeaves, book_thumbnail) == 26 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_thumbnail)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_thumbnail)>>);
static_assert(offsetof(XpEpubLeaves, book_spine) == 27 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_spine)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_spine)>>);
static_assert(offsetof(XpEpubLeaves, book_toc) == 28 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_toc)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_toc)>>);
static_assert(offsetof(XpEpubLeaves, book_spine_for_toc) == 29 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_spine_for_toc)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_spine_for_toc)>>);
static_assert(offsetof(XpEpubLeaves, book_toc_for_spine) == 30 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_toc_for_spine)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_toc_for_spine)>>);
static_assert(offsetof(XpEpubLeaves, book_cumulative_size) == 31 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_cumulative_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_cumulative_size)>>);
static_assert(offsetof(XpEpubLeaves, book_spine_for_size) == 32 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_spine_for_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_spine_for_size)>>);
static_assert(offsetof(XpEpubLeaves, book_text_reference) == 33 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_text_reference)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_text_reference)>>);
static_assert(offsetof(XpEpubLeaves, book_progress) == 34 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_progress)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_progress)>>);
static_assert(offsetof(XpEpubLeaves, book_resolve_href) == 35 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_resolve_href)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_resolve_href)>>);
static_assert(offsetof(XpEpubLeaves, book_setup_cache) == 36 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_setup_cache)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_setup_cache)>>);
static_assert(offsetof(XpEpubLeaves, book_clear_cache) == 37 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_clear_cache)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_clear_cache)>>);
static_assert(offsetof(XpEpubLeaves, book_install_extractor) == 38 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_install_extractor)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_install_extractor)>>);
static_assert(offsetof(XpEpubLeaves, book_clear_extractor) == 39 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_clear_extractor)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_clear_extractor)>>);
static_assert(offsetof(XpEpubLeaves, book_protection_error) == 40 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::book_protection_error)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::book_protection_error)>>);
static_assert(offsetof(XpEpubLeaves, progress_to_reader) == 41 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::progress_to_reader)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::progress_to_reader)>>);
static_assert(offsetof(XpEpubLeaves, progress_to_saved) == 42 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::progress_to_saved)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::progress_to_saved)>>);
static_assert(offsetof(XpEpubLeaves, progress_save) == 43 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::progress_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::progress_save)>>);
static_assert(offsetof(XpEpubLeaves, section_open) == 44 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_open)>>);
static_assert(offsetof(XpEpubLeaves, section_close) == 45 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_close)>>);
static_assert(offsetof(XpEpubLeaves, section_page_count) == 46 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_page_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_page_count)>>);
static_assert(offsetof(XpEpubLeaves, section_estimated_pages) == 47 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_estimated_pages)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_estimated_pages)>>);
static_assert(offsetof(XpEpubLeaves, section_building) == 48 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_building)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_building)>>);
static_assert(offsetof(XpEpubLeaves, section_complete) == 49 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_complete)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_complete)>>);
static_assert(offsetof(XpEpubLeaves, section_partial) == 50 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_partial)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_partial)>>);
static_assert(offsetof(XpEpubLeaves, section_load_cache) == 51 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_load_cache)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_load_cache)>>);
static_assert(offsetof(XpEpubLeaves, section_create_cache) == 52 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_create_cache)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_create_cache)>>);
static_assert(offsetof(XpEpubLeaves, section_start_build) == 53 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_start_build)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_start_build)>>);
static_assert(offsetof(XpEpubLeaves, section_build_more) == 54 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_build_more)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_build_more)>>);
static_assert(offsetof(XpEpubLeaves, section_abandon_build) == 55 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_abandon_build)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_abandon_build)>>);
static_assert(offsetof(XpEpubLeaves, section_clear_cache) == 56 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_clear_cache)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_clear_cache)>>);
static_assert(offsetof(XpEpubLeaves, section_html_cached) == 57 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_html_cached)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_html_cached)>>);
static_assert(offsetof(XpEpubLeaves, section_find_anchor) == 58 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_find_anchor)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_find_anchor)>>);
static_assert(offsetof(XpEpubLeaves, section_cached_anchor) == 59 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_cached_anchor)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_cached_anchor)>>);
static_assert(offsetof(XpEpubLeaves, section_toc_for_page) == 60 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_toc_for_page)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_toc_for_page)>>);
static_assert(offsetof(XpEpubLeaves, section_page_for_offset) == 61 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_page_for_offset)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_page_for_offset)>>);
static_assert(offsetof(XpEpubLeaves, section_offset_for_page) == 62 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_offset_for_page)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_offset_for_page)>>);
static_assert(offsetof(XpEpubLeaves, section_paragraph_for_page) == 63 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_paragraph_for_page)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_paragraph_for_page)>>);
static_assert(offsetof(XpEpubLeaves, section_reached_offset) == 64 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_reached_offset)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_reached_offset)>>);
static_assert(offsetof(XpEpubLeaves, section_load_page) == 65 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_load_page)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_load_page)>>);
static_assert(offsetof(XpEpubLeaves, section_page_text) == 66 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::section_page_text)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::section_page_text)>>);
static_assert(offsetof(XpEpubLeaves, page_close) == 67 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_close)>>);
static_assert(offsetof(XpEpubLeaves, page_info) == 68 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_info)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_info)>>);
static_assert(offsetof(XpEpubLeaves, page_element) == 69 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_element)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_element)>>);
static_assert(offsetof(XpEpubLeaves, page_word) == 70 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_word)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_word)>>);
static_assert(offsetof(XpEpubLeaves, page_footnote) == 71 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_footnote)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_footnote)>>);
static_assert(offsetof(XpEpubLeaves, page_link) == 72 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_link)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_link)>>);
static_assert(offsetof(XpEpubLeaves, metadata_assign) == 73 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_assign)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_assign)>>);
static_assert(offsetof(XpEpubLeaves, metadata_clear) == 74 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_clear)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_clear)>>);
static_assert(offsetof(XpEpubLeaves, metadata_footnotes) == 75 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_footnotes)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_footnotes)>>);
static_assert(offsetof(XpEpubLeaves, metadata_links) == 76 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_links)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_links)>>);
static_assert(offsetof(XpEpubLeaves, metadata_footnote) == 77 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_footnote)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_footnote)>>);
static_assert(offsetof(XpEpubLeaves, metadata_link) == 78 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::metadata_link)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::metadata_link)>>);
static_assert(offsetof(XpEpubLeaves, paint_dither) == 79 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_dither)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_dither)>>);
static_assert(offsetof(XpEpubLeaves, paint_pixel) == 80 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_pixel)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_pixel)>>);
static_assert(offsetof(XpEpubLeaves, paint_status) == 81 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::paint_status)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::paint_status)>>);
static_assert(offsetof(XpEpubLeaves, image_clear_failures) == 82 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::image_clear_failures)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::image_clear_failures)>>);
static_assert(offsetof(XpEpubLeaves, image_release_cache) == 83 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::image_release_cache)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::image_release_cache)>>);
static_assert(offsetof(XpEpubLeaves, fonts_ensure_loaded) == 84 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_ensure_loaded)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_ensure_loaded)>>);
static_assert(offsetof(XpEpubLeaves, fonts_release_caches) == 85 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_release_caches)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_release_caches)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_begin) == 86 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_begin)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_begin)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_finish_scan) == 87 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_finish_scan)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_finish_scan)>>);
static_assert(offsetof(XpEpubLeaves, fonts_prewarm_drop) == 88 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_prewarm_drop)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_prewarm_drop)>>);
static_assert(offsetof(XpEpubLeaves, fonts_take_corrupt_notice) == 89 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::fonts_take_corrupt_notice)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::fonts_take_corrupt_notice)>>);
static_assert(offsetof(XpEpubLeaves, font_sizes) == 90 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::font_sizes)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::font_sizes)>>);
static_assert(offsetof(XpEpubLeaves, settings_snapshot) == 91 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_snapshot)>>);
static_assert(offsetof(XpEpubLeaves, settings_render_spec) == 92 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_render_spec)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_render_spec)>>);
static_assert(offsetof(XpEpubLeaves, settings_orientation) == 93 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_orientation)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_orientation)>>);
static_assert(offsetof(XpEpubLeaves, settings_font_size) == 94 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_font_size)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_font_size)>>);
static_assert(offsetof(XpEpubLeaves, settings_line_spacing) == 95 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_line_spacing)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_line_spacing)>>);
static_assert(offsetof(XpEpubLeaves, settings_alignment) == 96 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_alignment)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_alignment)>>);
static_assert(offsetof(XpEpubLeaves, settings_focus_reading) == 97 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_focus_reading)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_focus_reading)>>);
static_assert(offsetof(XpEpubLeaves, settings_inversion) == 98 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_inversion)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_inversion)>>);
static_assert(offsetof(XpEpubLeaves, settings_ble_enabled) == 99 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_ble_enabled)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_ble_enabled)>>);
static_assert(offsetof(XpEpubLeaves, settings_save) == 100 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::settings_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::settings_save)>>);
static_assert(offsetof(XpEpubLeaves, input_services) == 101 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_services)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_services)>>);
static_assert(offsetof(XpEpubLeaves, input_facts) == 102 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_facts)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_facts)>>);
static_assert(offsetof(XpEpubLeaves, input_pressed) == 103 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_pressed)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_pressed)>>);
static_assert(offsetof(XpEpubLeaves, input_long_press) == 104 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_long_press)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_long_press)>>);
static_assert(offsetof(XpEpubLeaves, input_released) == 105 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_released)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_released)>>);
static_assert(offsetof(XpEpubLeaves, input_screen_long) == 106 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_screen_long)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_screen_long)>>);
static_assert(offsetof(XpEpubLeaves, input_screen_tap) == 107 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_screen_tap)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_screen_tap)>>);
static_assert(offsetof(XpEpubLeaves, input_raw_contact) == 108 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_raw_contact)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_raw_contact)>>);
static_assert(offsetof(XpEpubLeaves, render_busy) == 109 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::render_busy)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::render_busy)>>);
static_assert(offsetof(XpEpubLeaves, file_open_read) == 110 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_open_read)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_open_read)>>);
static_assert(offsetof(XpEpubLeaves, file_open_write) == 111 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_open_write)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_open_write)>>);
static_assert(offsetof(XpEpubLeaves, file_read) == 112 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_read)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_read)>>);
static_assert(offsetof(XpEpubLeaves, file_write) == 113 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_write)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_write)>>);
static_assert(offsetof(XpEpubLeaves, file_flush) == 114 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_flush)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_flush)>>);
static_assert(offsetof(XpEpubLeaves, file_close) == 115 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::file_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::file_close)>>);
static_assert(offsetof(XpEpubLeaves, path_remove) == 116 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_remove)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_remove)>>);
static_assert(offsetof(XpEpubLeaves, path_rename) == 117 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_rename)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_rename)>>);
static_assert(offsetof(XpEpubLeaves, path_mkdir) == 118 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::path_mkdir)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::path_mkdir)>>);
static_assert(offsetof(XpEpubLeaves, native_path_hash) == 119 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::native_path_hash)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::native_path_hash)>>);
static_assert(offsetof(XpEpubLeaves, heap_snapshot) == 120 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::heap_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::heap_snapshot)>>);
static_assert(offsetof(XpEpubLeaves, radio_status) == 121 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_status)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_status)>>);
static_assert(offsetof(XpEpubLeaves, radio_before_build) == 122 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_before_build)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_before_build)>>);
static_assert(offsetof(XpEpubLeaves, radio_settle_start) == 123 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_settle_start)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_settle_start)>>);
static_assert(offsetof(XpEpubLeaves, radio_holds_heap) == 124 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_holds_heap)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_holds_heap)>>);
static_assert(offsetof(XpEpubLeaves, radio_after_paint) == 125 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_after_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_after_paint)>>);
static_assert(offsetof(XpEpubLeaves, radio_switch_off) == 126 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_switch_off)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_switch_off)>>);
static_assert(offsetof(XpEpubLeaves, radio_request_connect) == 127 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::radio_request_connect)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::radio_request_connect)>>);
static_assert(offsetof(XpEpubLeaves, trusted_now) == 128 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::trusted_now)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::trusted_now)>>);
static_assert(offsetof(XpEpubLeaves, trusted_sync) == 129 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::trusted_sync)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::trusted_sync)>>);
static_assert(offsetof(XpEpubLeaves, wifi_connected) == 130 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::wifi_connected)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::wifi_connected)>>);
static_assert(offsetof(XpEpubLeaves, wifi_disconnect) == 131 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::wifi_disconnect)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::wifi_disconnect)>>);
static_assert(offsetof(XpEpubLeaves, restart_reader) == 132 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::restart_reader)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::restart_reader)>>);
static_assert(offsetof(XpEpubLeaves, child_menu) == 133 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_menu)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_menu)>>);
static_assert(offsetof(XpEpubLeaves, child_chapter) == 134 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_chapter)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_chapter)>>);
static_assert(offsetof(XpEpubLeaves, child_percent) == 135 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_percent)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_percent)>>);
static_assert(offsetof(XpEpubLeaves, child_text_settings) == 136 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_text_settings)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_text_settings)>>);
static_assert(offsetof(XpEpubLeaves, child_footnote) == 137 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_footnote)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_footnote)>>);
static_assert(offsetof(XpEpubLeaves, child_dictionary) == 138 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_dictionary)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_dictionary)>>);
static_assert(offsetof(XpEpubLeaves, child_selection) == 139 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_selection)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_selection)>>);
static_assert(offsetof(XpEpubLeaves, child_bookmarks) == 140 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_bookmarks)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_bookmarks)>>);
static_assert(offsetof(XpEpubLeaves, child_clippings) == 141 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_clippings)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_clippings)>>);
static_assert(offsetof(XpEpubLeaves, child_qr) == 142 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_qr)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_qr)>>);
static_assert(offsetof(XpEpubLeaves, child_wifi) == 143 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_wifi)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_wifi)>>);
static_assert(offsetof(XpEpubLeaves, child_sync) == 144 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::child_sync)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::child_sync)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_open) == 145 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_open)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_close) == 146 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_close)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_begin) == 147 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_begin)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_begin)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_paint) == 148 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_paint)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_route) == 149 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_route)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_route)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_scroll) == 150 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_scroll)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_scroll)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_visible_rows) == 151 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_visible_rows)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_visible_rows)>>);
static_assert(offsetof(XpEpubLeaves, popup_open) == 152 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_open)>>);
static_assert(offsetof(XpEpubLeaves, popup_close) == 153 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_close)>>);
static_assert(offsetof(XpEpubLeaves, popup_active) == 154 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_active)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_active)>>);
static_assert(offsetof(XpEpubLeaves, popup_show) == 155 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_show)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_show)>>);
static_assert(offsetof(XpEpubLeaves, popup_dismiss) == 156 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_dismiss)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_dismiss)>>);
static_assert(offsetof(XpEpubLeaves, popup_input) == 157 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_input)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_input)>>);
static_assert(offsetof(XpEpubLeaves, popup_paint) == 158 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_paint)>>);
static_assert(offsetof(XpEpubLeaves, popup_process_render) == 159 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::popup_process_render)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::popup_process_render)>>);
static_assert(offsetof(XpEpubLeaves, end_open) == 160 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_open)>>);
static_assert(offsetof(XpEpubLeaves, end_close) == 161 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_close)>>);
static_assert(offsetof(XpEpubLeaves, end_load) == 162 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_load)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_load)>>);
static_assert(offsetof(XpEpubLeaves, end_menu_active) == 163 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_menu_active)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_menu_active)>>);
static_assert(offsetof(XpEpubLeaves, end_input) == 164 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_input)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_input)>>);
static_assert(offsetof(XpEpubLeaves, end_paint) == 165 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::end_paint)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::end_paint)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_open) == 166 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_open)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_close) == 167 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_close)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_close)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_clear) == 168 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_clear)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_clear)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_load) == 169 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_load)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_load)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_save) == 170 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_save)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_count) == 171 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_count)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_row) == 172 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_row)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_row)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_retain) == 173 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_retain)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_retain)>>);
static_assert(offsetof(XpEpubLeaves, bookmarks_insert_front) == 174 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::bookmarks_insert_front)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::bookmarks_insert_front)>>);
static_assert(offsetof(XpEpubLeaves, clippings_load) == 175 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_load)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_load)>>);
static_assert(offsetof(XpEpubLeaves, clippings_unload) == 176 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_unload)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_unload)>>);
static_assert(offsetof(XpEpubLeaves, clippings_count) == 177 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_count)>>);
static_assert(offsetof(XpEpubLeaves, clippings_row) == 178 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_row)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_row)>>);
static_assert(offsetof(XpEpubLeaves, clippings_remove) == 179 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_remove)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_remove)>>);
static_assert(offsetof(XpEpubLeaves, clippings_insert) == 180 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_insert)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_insert)>>);
static_assert(offsetof(XpEpubLeaves, clippings_export) == 181 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_export)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_export)>>);
static_assert(offsetof(XpEpubLeaves, clippings_move_book) == 182 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::clippings_move_book)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::clippings_move_book)>>);
static_assert(offsetof(XpEpubLeaves, recents_remove) == 183 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_remove)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_remove)>>);
static_assert(offsetof(XpEpubLeaves, recents_add) == 184 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_add)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_add)>>);
static_assert(offsetof(XpEpubLeaves, recents_update_path) == 185 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::recents_update_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::recents_update_path)>>);
static_assert(offsetof(XpEpubLeaves, state_open_path) == 186 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_open_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_open_path)>>);
static_assert(offsetof(XpEpubLeaves, state_set_open_path) == 187 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_set_open_path)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_set_open_path)>>);
static_assert(offsetof(XpEpubLeaves, state_reset_reader_count) == 188 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_reset_reader_count)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_reset_reader_count)>>);
static_assert(offsetof(XpEpubLeaves, state_save) == 189 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::state_save)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::state_save)>>);
static_assert(offsetof(XpEpubLeaves, has_sync_credentials) == 190 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_sync_credentials)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_sync_credentials)>>);
static_assert(offsetof(XpEpubLeaves, has_exit_subscriber) == 191 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_exit_subscriber)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_exit_subscriber)>>);
static_assert(offsetof(XpEpubLeaves, has_session_subscriber) == 192 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::has_session_subscriber)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::has_session_subscriber)>>);
static_assert(offsetof(XpEpubLeaves, document_id) == 193 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::document_id)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::document_id)>>);
static_assert(offsetof(XpEpubLeaves, emit_open) == 194 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_open)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_open)>>);
static_assert(offsetof(XpEpubLeaves, emit_exit) == 195 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_exit)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_exit)>>);
static_assert(offsetof(XpEpubLeaves, emit_session) == 196 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::emit_session)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::emit_session)>>);
static_assert(offsetof(XpEpubLeaves, log) == 197 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::log)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::log)>>);
static_assert(offsetof(XpEpubLeaves, page_word_font) == 198 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_word_font)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_word_font)>>);
static_assert(offsetof(XpEpubLeaves, page_line_font) == 199 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_line_font)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_line_font)>>);
static_assert(offsetof(XpEpubLeaves, page_word_y_offset) == 200 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_word_y_offset)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_word_y_offset)>>);
static_assert(offsetof(XpEpubLeaves, page_line_ruby_shift) == 201 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_line_ruby_shift)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_line_ruby_shift)>>);
static_assert(offsetof(XpEpubLeaves, font_text_advance) == 202 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::font_text_advance)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::font_text_advance)>>);
static_assert(offsetof(XpEpubLeaves, log_enabled) == 203 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::log_enabled)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::log_enabled)>>);
static_assert(offsetof(XpEpubLeaves, toolbar_reset_navigation) == 204 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::toolbar_reset_navigation)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::toolbar_reset_navigation)>>);
static_assert(offsetof(XpEpubLeaves, input_snapshot) == 205 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::input_snapshot)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::input_snapshot)>>);
static_assert(offsetof(XpEpubLeaves, page_render) == 206 * sizeof(void (*)()));
static_assert(std::is_pointer_v<decltype(XpEpubLeaves::page_render)> &&
              std::is_function_v<std::remove_pointer_t<decltype(XpEpubLeaves::page_render)>>);
static_assert(sizeof(XpEpubLeaves) == 207 * sizeof(void (*)()));
static_assert(alignof(XpEpubLeaves) == alignof(void (*)()));
static_assert(sizeof(XpAppResultView::kind) == 1);
static_assert(sizeof(XpAppResultView::cancelled) == 1);
