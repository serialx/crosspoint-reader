namespace bookmark_adapter {
static void unlocked() { leaf(); assert(lockDepth == 0); }
static void locked() { leaf(); assert(lockDepth == 1); }
static XpAppText wire(const std::string& value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
static std::string nativeText(XpAppText value) {
  assert(value.data || !value.len);
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}
static XpEpubBookmark wire(const BookmarkEntry& row) {
  return {wire(row.xpath), wire(row.summary), wire(row.name), row.percentage,
          row.computedSpineIndex, row.computedChapterPageCount, row.computedChapterProgress,
          uint8_t(row.hasVisibleTextOffset), row.visibleTextOffset};
}
static BookmarkEntry nativeRow(const XpEpubBookmark& row) {
  BookmarkEntry result;
  result.xpath = nativeText(row.xpath); result.summary = nativeText(row.summary); result.name = nativeText(row.name);
  result.percentage = row.percentage; result.computedSpineIndex = row.spine;
  result.computedChapterPageCount = row.page_count; result.computedChapterProgress = row.chapter_page;
  result.hasVisibleTextOffset = row.has_offset; result.visibleTextOffset = row.offset;
  return result;
}
static std::vector<BookmarkEntry>& rows(void* handle) {
  assert(activeFixture && handle == &activeFixture->cachedBookmarks);
  return *static_cast<std::vector<BookmarkEntry>*>(handle);
}
static HalFile* liveFile;
static void publicationRead(unsigned step) {
  leaf();
  assert(publishing && publicationStep == step);
  ++publicationStep;
}
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) { leaf(); assert(false && "unexpected bookmark leaf"); std::abort(); }
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
  table.path_exists = [](XpAppText path) -> uint8_t { unlocked(); return Storage.exists(nativeText(path).c_str()); };
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
  table.model_lock = [] { assert(modelDepth == 0); ++modelDepth; };
  table.model_unlock = [] { assert(modelDepth == 1); --modelDepth; };
  table.render_lock = []() -> void* { leaf(); return new RenderLock; };
  table.render_unlock = [](void* token) {
    locked(); assert(token); delete static_cast<RenderLock*>(token);
    if (dropAfterUnlock) {
      dropAfterUnlock = false;
      assert(activeFixture && activeFixture->owner);
      xp_epub_test_bookmark_drop_section(activeFixture->owner);
    }
  };
  table.notify_render = [] { unlocked(); event("update", lockDepth); };
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
  table.book.book_progress = [](void* book, int32_t spine, float fraction) -> float {
    unlocked(); assert(book); return static_cast<Epub*>(book)->calculateProgress(spine, fraction);
  };
  table.book.book_spine_count = [](void* book) -> int32_t {
    unlocked(); assert(book); return static_cast<Epub*>(book)->getSpineItemsCount();
  };
  table.book.book_cache_path = [](void* book, XpAppTextSink sink, void* context) -> uint8_t {
    unlocked(); assert(book && sink); const auto path = static_cast<Epub*>(book)->getCachePath();
    return sink(context, wire(path));
  };
  table.book.section.section_page_count = [](void* section) -> uint16_t {
    publicationRead(0); assert(section == liveSection); return liveSection->pageCount;
  };
  table.book.section.section_estimated_pages = [](void* section) -> int32_t {
    leaf(); assert(section && section == liveSection);
    if (publishing) publicationRead(1); else locked();
    return liveSection->estimatedTotalPages();
  };
  table.book.section.section_building = [](void* section) -> uint8_t { publicationRead(2); assert(section == liveSection); return 0; };
  table.book.section.section_complete = [](void* section) -> uint8_t { publicationRead(3); assert(section == liveSection); return 0; };
  table.book.section.section_partial = [](void* section) -> uint8_t { publicationRead(4); assert(section == liveSection); return 0; };
  table.book.section.section_close = [](void* section) {
    leaf(); assert(section && section == liveSection && !publishing); delete liveSection;
  };
  table.book.section.section_page_text = [](void* section, int32_t page, XpAppTextSink sink, void* context) -> uint8_t {
    locked(); assert(section && section == liveSection && sink);
    const auto text = liveSection->getTextFromSectionFile(page);
    return sink(context, wire(text));
  };
  table.book.section.section_offset_for_page = [](void* section, uint16_t page, uint32_t* out) -> uint8_t {
    locked(); assert(section && section == liveSection && out);
    const auto value = liveSection->getVisibleTextOffsetForPage(page);
    if (value) *out = *value;
    return value.has_value();
  };
  table.physical.progress_to_saved = [](void* book, const XpEpubPosition* position, XpEpubSavedProgressSink sink,
                                void* context) -> uint8_t {
    unlocked(); assert(book && position && sink);
    const auto saved = ProgressMapper::toSavedProgress(*static_cast<Epub*>(book), CrossPointPosition{});
    const XpEpubSavedProgress value{wire(saved.xpath), saved.percentage};
    const auto result = sink(context, &value);
    unlocked(); return result;
  };
  table.physical.bookmarks_count = [](void* handle) -> uint32_t { unlocked(); return rows(handle).size(); };
  table.physical.bookmarks_row = [](void* handle, uint32_t index, XpEpubBookmarkSink sink, void* context) -> uint8_t {
    unlocked(); auto& entries = rows(handle); assert(index < entries.size() && sink);
    const auto row = wire(entries[index]); const auto result = sink(context, &row); unlocked(); return result;
  };
  // Vector mutations require owner ResourceAccess; matching and persistence reads stay outside it.
  table.physical.bookmarks_clear = [](void* handle) { locked(); rows(handle).clear(); };
  table.book.bookmarks_load = [](void* handle, void* book) -> uint8_t {
    locked(); assert(book); return BookmarkFile::load(static_cast<Epub*>(book)->getPath(), rows(handle));
  };
  table.book.bookmarks_save = [](void* handle, void* book) -> uint8_t {
    unlocked(); assert(book); return BookmarkFile::save(static_cast<Epub*>(book)->getPath(), rows(handle));
  };
  table.physical.bookmarks_retain = [](void* handle, uint8_t (*keep)(void*, const XpEpubBookmark*), void* context) -> uint32_t {
    locked(); assert(keep); auto& entries = rows(handle); const auto before = entries.size();
    const auto end = std::remove_if(entries.begin(), entries.end(), [&](const auto& entry) {
      const auto value = wire(entry); const auto retained = keep(context, &value); locked(); return retained == 0;
    });
    entries.erase(end, entries.end()); return before - entries.size();
  };
  table.physical.bookmarks_insert_front = [](void* handle, const XpEpubBookmark* entry) -> uint8_t {
    locked(); assert(entry); auto& entries = rows(handle); entries.insert(entries.begin(), nativeRow(*entry)); return 1;
  };
  table.physical.file_open_read = [](XpAppText path, XpAppText origin) -> void* {
    assert(origin.len == 3 && std::memcmp(origin.data, "ERS", 3) == 0);
    unlocked(); assert(!liveFile); auto file = std::make_unique<HalFile>();
    if (!Storage.openFileForRead("ERS", nativeText(path), *file)) return nullptr;
    liveFile = file.release(); return liveFile;
  };
  table.physical.file_open_write = [](XpAppText path, XpAppText origin) -> void* {
    assert(origin.len == 3 && std::memcmp(origin.data, "ERS", 3) == 0);
    unlocked(); assert(!liveFile); auto file = std::make_unique<HalFile>();
    if (!Storage.openFileForWrite("ERS", nativeText(path), *file)) return nullptr;
    liveFile = file.release(); return liveFile;
  };
  table.physical.file_read = [](void* file, uint8_t* bytes, uint32_t length) -> int32_t {
    unlocked(); assert(file == liveFile && liveFile && length == 13); return liveFile->read(bytes, length);
  };
  table.physical.file_write = [](void* file, const uint8_t* bytes, uint32_t length) -> uint32_t {
    unlocked(); assert(file == liveFile && liveFile && length <= 13); return liveFile->write(bytes, length);
  };
  table.physical.file_close = [](void* file) -> uint8_t {
    unlocked(); assert(file == liveFile && liveFile); delete liveFile; liveFile = nullptr;
   return 1; };
  table.physical.path_remove = [](XpAppText path) -> uint8_t {
    unlocked(); assert(!liveFile); return Storage.remove(nativeText(path).c_str());
  };
  table.physical.log = [](uint8_t severity, XpAppText origin, XpAppText text) {
    unlocked(); assert(severity <= 1 && nativeText(origin) == "ERS");
#ifdef BOOKMARK_RELEASE_LOG
    if (severity == 0) return;
#endif
    std::printf("%s %.*s\n", severity == 0 ? "debug" : "error", int(text.len), reinterpret_cast<const char*>(text.data));
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
}  // namespace bookmark_adapter

void BookmarkFixture::initialize() {
  if (owner) { assert(activeFixture == this); return; }
  bookmark_adapter::unlocked(); assert(!activeFixture && !publishing); activeFixture = this;
  XpEpubTestBookmarkSeed seed{};
  seed.book = epub.get(); seed.section = section.get(); seed.bookmarks = &cachedBookmarks;
  seed.spine = currentSpineIndex; seed.page = activePage; seed.visible_offset = currentPageVisibleOffset.value_or(0);
  seed.has_visible_offset = currentPageVisibleOffset.has_value(); seed.removed = bookmarkRemoved;
  seed.bookmarked = currentPageBookmarked; seed.history_depth = footnoteDepth;
  for (int i = 0; i < 3; ++i) { seed.history[2*i] = savedPositions[i].spineIndex; seed.history[2*i+1] = savedPositions[i].pageNumber; }
  seed.context = this;
  seed.position = [](void* context, XpEpubPosition* out) {
    bookmark_adapter::unlocked(); assert(context == activeFixture && out); event("position", lockDepth); *out = {};
  };
  seed.sanitize = [](void* context, XpAppText text) {
    bookmark_adapter::unlocked(); assert(context == activeFixture); BookmarkUtil::observeSummary(bookmark_adapter::nativeText(text));
  };
  seed.estimate = [](void* context, int32_t count) {
    bookmark_adapter::unlocked(); assert(context == activeFixture && liveSection && count == liveSection->estimated);
    const auto observed = liveSection->estimatedTotalPages(); assert(count == observed);
  };
  publishing = true; publicationStep = 0;
  owner = xp_epub_test_bookmark_create_legacy_book(&seed,&bookmark_adapter::leaves.physical, &bookmark_adapter::runtime, &bookmark_adapter::resources, &bookmark_adapter::leaves.book);
  publishing = false;
  assert(owner && publicationStep == (section ? 5u : 0u)); section.release(); bookmark_adapter::unlocked();
}
BookmarkFixture::~BookmarkFixture() {
  assert(owner && activeFixture == this); xp_epub_test_bookmark_destroy(owner);
  bookmark_adapter::unlocked(); assert(!liveSection && !bookmark_adapter::liveFile); activeFixture = nullptr;
}
XpEpubTestBookmarkObservation BookmarkFixture::observe() {
  assert(owner); XpEpubTestBookmarkObservation out{}; xp_epub_test_bookmark_observe(owner, &out);
  bookmark_adapter::unlocked(); assert(bool(out.has_section) == bool(liveSection)); return out;
}
void BookmarkFixture::seedHistory() {
  initialize(); int32_t values[6];
  for (int i = 0; i < 3; ++i) { values[2*i] = savedPositions[i].spineIndex; values[2*i+1] = savedPositions[i].pageNumber; }
  xp_epub_test_bookmark_seed_history(owner, footnoteDepth, values);
}
void BookmarkFixture::updateBookmarkFlag() { initialize(); xp_epub_test_bookmark_flag(owner); }
void BookmarkFixture::addBookmark() { initialize(); xp_epub_test_bookmark_add(owner); }
void BookmarkFixture::loadCachedBookmarks() { initialize(); xp_epub_test_bookmark_load(owner); }
void BookmarkFixture::saveLinkStack() { initialize(); xp_epub_test_bookmark_save_links(owner); }
void BookmarkFixture::loadLinkStack() { initialize(); xp_epub_test_bookmark_load_links(owner); }

static ProgressRange getPageProgressRange(const std::shared_ptr<Epub>& book, int spine, int page, int count) {
  const auto range = xp_epub_test_bookmark_range(page, count);
  return {book->calculateProgress(spine, range.start), book->calculateProgress(spine, range.end)};
}
static bool bookmarkMatchesProgress(const BookmarkEntry& entry, int spine, int page, int count, const ProgressRange& range) {
  const auto row = bookmark_adapter::wire(entry);
  return xp_epub_test_bookmark_matches(&row, spine, page, count, {range.start, range.end});
}
