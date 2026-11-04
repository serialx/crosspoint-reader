// Every table slot has a correctly typed function; unexpected leaves fail immediately.
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) {
    leaf();
    assert(false && "unexpected navigation leaf");
    std::abort();
  }
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
  table.render_lock = []() -> void* { return new RenderLock; };
  table.render_unlock = [](void* token) {
    leaf();
    assert(token);
    delete static_cast<RenderLock*>(token);
  };
  table.model_lock = [] { assert(modelLocks++ == 0); };
  table.model_unlock = [] { assert(modelLocks-- == 1); };
  table.notify_render = [] { event("update"); };
  return table;
}();

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
  table.book.book_size = [](void* book) -> uint32_t {
    leaf();
    return static_cast<Epub*>(book)->getBookSize();
  };
  table.book.book_spine_count = [](void* book) -> int32_t {
    leaf();
    return static_cast<Epub*>(book)->getSpineItemsCount();
  };
  table.book.book_spine_for_size = [](void* book, uint32_t bytes) -> int32_t {
    leaf();
    return static_cast<Epub*>(book)->getSpineIndexForSize(bytes);
  };
  table.book.book_cumulative_size = [](void* book, int32_t spine) -> uint32_t {
    leaf();
    return static_cast<Epub*>(book)->getCumulativeSpineItemSize(spine);
  };
  table.book.book_resolve_href = [](void* book, XpAppText href) -> int32_t {
    leaf();
    return static_cast<Epub*>(book)->resolveHrefToSpineIndex(href);
  };
  table.book.section.section_close = [](void* section) {
    leaf();
    assert(section && !publishing);
    delete static_cast<Section*>(section);
  };
  table.book.section.section_page_count = [](void* section) -> uint16_t {
    publicationRead(0);
    return static_cast<Section*>(section)->pageCount;
  };
  table.book.section.section_estimated_pages = [](void* section) -> int32_t {
    publicationRead(1);
    return static_cast<Section*>(section)->estimatedTotalPages();
  };
  table.book.section.section_building = [](void* section) -> uint8_t {
    publicationRead(2);
    return static_cast<Section*>(section)->isBuilding();
  };
  table.book.section.section_complete = [](void*) -> uint8_t {
    publicationRead(3);
    assert(publishing);
    return 0;
  };
  table.book.section.section_partial = [](void*) -> uint8_t {
    publicationRead(4);
    assert(publishing);
    return 0;
  };
  table.book.section.section_page_for_offset = [](void* section, uint32_t offset, uint8_t preferFirst,
                                      uint16_t* page) -> uint8_t {
    leaf();
    assert(!preferFirst && page && !publishing);
    auto result = static_cast<Section*>(section)->getPageForVisibleTextOffset(offset);
    if (result) *page = *result;
    return result.has_value();
  };
  table.book.section.section_offset_for_page = [](void* section, uint16_t page, uint32_t* offset) -> uint8_t {
    leaf();
    assert(offset && !publishing);
    auto result = static_cast<Section*>(section)->getVisibleTextOffsetForPage(page);
    if (result) *offset = *result;
    return result.has_value();
  };
  table.physical.progress_to_reader = [](void* book, const XpEpubSavedProgress* saved, int32_t spine,
                                 int32_t count, XpEpubPosition* out) {
    leaf();
    assert(book && saved && out);
    std::printf("fallback [%.*s] %.9g %d %d\n", int(saved->xpath.len),
                reinterpret_cast<const char*>(saved->xpath.data), saved->percentage, spine, count);
    *out = {};
    out->spine = 4;
    out->page = 9;
  };
  table.physical.progress_save = [](void* book, const XpEpubPosition* position) -> uint8_t {
    leaf();
    assert(book && position);
    std::printf("save %d %d %d %d %u\n", position->spine, position->page, position->count,
                position->has_visible_offset, position->visible_offset);
    return 1;
  };
  table.physical.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    leaf();
    assert(severity == 0);
    std::printf("%.*s %.*s\n", int(origin.len), reinterpret_cast<const char*>(origin.data),
                int(message.len), reinterpret_cast<const char*>(message.data));
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

static void estimateRead(void* context, int32_t pages) {
  leaf();
  assert(!publishing && liveSections == 1);
  const auto* section = static_cast<const Section*>(context);
  assert(section && pages == section->pageCount);
  const auto observed = section->estimatedTotalPages();
  assert(pages == observed);
}

void NavigationFixture::initialize() {
  if (owner) return;
  assert(!locks && !modelLocks && !publishing);
  XpEpubTestNavigationSeed seed{};
  seed.book = epub.get();
  seed.section = section.get();
  seed.spine = currentSpineIndex;
  seed.active_page = activePage;
  seed.next_page = nextPageNumber;
  seed.cached_spine = cachedSpineIndex;
  seed.cached_count = cachedChapterTotalPageCount;
  seed.cached_offset = cachedVisibleTextOffset.value_or(0);
  seed.visible_offset = currentPageVisibleOffset.value_or(0);
  const int32_t history[] = {0, 1, 1, 2, 2, 3};
  std::copy(std::begin(history), std::end(history), seed.history);
  seed.history_depth = footnoteDepth;
  seed.has_cached_offset = cachedVisibleTextOffset.has_value();
  seed.has_visible_offset = currentPageVisibleOffset.has_value();
  seed.anchor = text(pendingAnchor);
  seed.estimate_context = section.get();
  seed.estimate_read = estimateRead;
  publicationStep = 0;
  publishing = true;
  owner = xp_epub_test_navigation_create_legacy_book(&seed,&leaves.physical, &runtime, &resources, &leaves.book);
  publishing = false;
  assert(owner && !locks && !modelLocks);
  assert(publicationStep == (section ? 5u : 0u));
  section.release();
}

NavigationFixture::~NavigationFixture() {
  if (owner) xp_epub_test_navigation_destroy(owner);
  assert(!locks && !modelLocks && !liveSections);
}
void NavigationFixture::jumpToPercent(int percent) {
  initialize();
  xp_epub_test_navigation_percent(owner, percent);
}
void NavigationFixture::applyProgressChange(const ProgressChangeResult& sync) {
  initialize();
  const XpAppProgressResult progress{sync.spineIndex, sync.page, sync.totalPages, text(sync.xpath),
                                     sync.percentage, sync.visibleTextOffset,
                                     uint8_t(sync.hasSavedProgress), uint8_t(sync.hasVisibleTextOffset)};
  xp_epub_test_navigation_progress(owner, &progress);
}
bool NavigationFixture::applyDeferredReposition() {
  initialize();
  return xp_epub_test_navigation_deferred(owner);
}
void NavigationFixture::clearDeferredReposition() {
  initialize();
  xp_epub_test_navigation_clear(owner);
}
bool NavigationFixture::saveProgress(int spine, int page, int count) {
  initialize();
  return xp_epub_test_navigation_save(owner, spine, page, count);
}
void NavigationFixture::rememberCurrentContentOffset() {
  initialize();
  xp_epub_test_navigation_remember(owner);
}
void NavigationFixture::navigateToHref(const std::string& href, bool savePosition) {
  initialize();
  xp_epub_test_navigation_href(owner, text(href), savePosition);
}
void NavigationFixture::restoreSavedPosition() {
  initialize();
  xp_epub_test_navigation_restore(owner);
}
void NavigationFixture::dump() {
  assert(owner);
  XpEpubTestNavigationObservation state{};
  xp_epub_test_navigation_observe(owner, &state);
  assert(bool(state.has_section) == bool(liveSections));
  std::printf("state %d %d %d %d %u %d %u %d %.9g [%.*s] %d", state.spine, state.next_page,
              state.has_section ? state.active_page : -999, state.cached_count, state.cached_offset,
              state.has_pending_offset, state.pending_offset, state.pending_percent, state.spine_progress,
              int(state.anchor.len), reinterpret_cast<const char*>(state.anchor.data), state.history_depth);
  for (int i = 0; i < 3; ++i) std::printf(" %d:%d", state.history[2 * i], state.history[2 * i + 1]);
  std::printf(" cached=%d\n", state.has_cached_offset);
  assert(!locks && !modelLocks);
}
