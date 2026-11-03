static unsigned modelDepth = 0, resourceDepth = 0;
static EpubReaderActivity* activeFixture = nullptr;
struct BookHandle {
  std::unique_ptr<Epub> loading;
  std::shared_ptr<Epub> book;
  Epub& value() const {
    assert(bool(loading) != bool(book));
    return loading ? *loading : *book;
  }
};
static BookHandle* liveBook = nullptr;
static Section* liveSection = nullptr;
static HalFile* liveFile = nullptr;

struct ResourceScope {
  ResourceScope() { assert(!resourceDepth && !modelDepth); ++resourceDepth; }
  ~ResourceScope() { assert(resourceDepth == 1 && !modelDepth); --resourceDepth; }
};
static void leaf() { assert(!modelDepth && resourceDepth == 1 && activeFixture); }
static std::string owned(XpAppText value) {
  assert(value.data || !value.len);
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string{};
}
static XpAppText wire(const std::string& value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), static_cast<uint32_t>(value.size())};
}
static uint8_t sendText(std::string value, XpAppTextSink sink, void* context) {
  assert(sink);
  return sink(context, wire(value));
}
static BookHandle& book(void* handle) {
  assert(handle && handle == liveBook);
  return *static_cast<BookHandle*>(handle);
}
static void restorationScope() {
  leaf();
  assert(bool(liveFile) == config.file);
  if (liveFile) assert(liveFile->open);
}

template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) {
    leaf();
    assert(false && "unexpected book-lifetime leaf");
    std::abort();
  }
};
#define UNUSED_LEAF(field) table.field = &Unexpected<decltype(table.field)>::call
static const uint8_t indexingLabel[] = "indexing";

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
  table.path_exists = [](XpAppText path) -> uint8_t {
    leaf();
    const auto text = owned(path);
    assert(text == "/cache/book.bin");
    return Storage.exists(text.c_str());
  };
  table.translate = [](uint16_t id) -> XpAppText {
    leaf();
    assert(id == static_cast<uint16_t>(StrId::STR_INDEXING));
    return {indexingLabel, sizeof(indexingLabel) - 1};
  };
  table.popup_draw = [](XpAppText label) -> XpUiRect {
    leaf();
    assert(label.data == indexingLabel && label.len == sizeof(indexingLabel) - 1);
    GUI.drawPopup(activeFixture->renderer, tr(STR_INDEXING));
    return {};
  };
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
  table.model_lock = [] { assert(!modelDepth && resourceDepth == 1); ++modelDepth; };
  table.model_unlock = [] { assert(modelDepth == 1 && resourceDepth == 1); --modelDepth; };
  return table;
}();

static const LegacyEpubLeaves leaves = [] {
  LegacyEpubLeaves table{};
  static_assert(sizeof(XpEpubLeaves) == 192 * sizeof(void (*)()));
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
  UNUSED_LEAF(physical.loan_begin);
  UNUSED_LEAF(physical.loan_end);
  UNUSED_LEAF(physical.loan_drop);
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
  UNUSED_LEAF(physical.section_open);
  UNUSED_LEAF(physical.section_close);
  UNUSED_LEAF(physical.section_page_count);
  UNUSED_LEAF(physical.section_estimated_pages);
  UNUSED_LEAF(physical.section_building);
  UNUSED_LEAF(physical.section_complete);
  UNUSED_LEAF(physical.section_partial);
  UNUSED_LEAF(physical.section_load_cache);
  UNUSED_LEAF(physical.section_create_cache);
  UNUSED_LEAF(physical.section_start_build);
  UNUSED_LEAF(physical.section_build_more);
  UNUSED_LEAF(physical.section_abandon_build);
  UNUSED_LEAF(physical.section_clear_cache);
  UNUSED_LEAF(physical.section_html_cached);
  UNUSED_LEAF(physical.section_find_anchor);
  UNUSED_LEAF(physical.section_cached_anchor);
  UNUSED_LEAF(physical.section_toc_for_page);
  UNUSED_LEAF(physical.section_page_for_offset);
  UNUSED_LEAF(physical.section_offset_for_page);
  UNUSED_LEAF(physical.section_paragraph_for_page);
  UNUSED_LEAF(physical.section_reached_offset);
  UNUSED_LEAF(physical.section_load_page);
  UNUSED_LEAF(physical.section_page_text);
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
  table.book.book_open = [](XpAppText path) -> void* {
    leaf();
    assert(!liveBook);
    auto loaded = makeUniqueNoThrow<Epub>(owned(path), "/.crosspoint");
    if (!loaded) return nullptr;
    auto handle = std::make_unique<BookHandle>();
    handle->loading = std::move(loaded);
    liveBook = handle.release();
    return liveBook;
  };
  table.book.book_load = [](void* handle, uint8_t buildMissing, uint8_t skipCss) -> uint8_t {
    leaf();
    assert(activeFixture->renderer.framebuffer == config.cached);
    auto& resource = book(handle);
    if (!resource.value().load(buildMissing != 0, skipCss != 0)) return 0;
    if (resource.loading) resource.book = std::move(resource.loading);
    return 1;
  };
  table.book.book_close = [](void* handle) {
    leaf();
    assert(handle == liveBook && !liveFile);
    assert(ImageBlock::context != &book(handle).value());
    delete liveBook;
    liveBook = nullptr;
  };
  table.book.book_path = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
    leaf();
    return sendText(book(handle).value().getPath(), sink, context);
  };
  table.book.book_cache_path = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
    leaf();
    return sendText(book(handle).value().getCachePath(), sink, context);
  };
  table.book.book_title = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
    leaf();
    return sendText(book(handle).value().getTitle(), sink, context);
  };
  table.book.book_author = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
    leaf();
    return sendText(book(handle).value().getAuthor(), sink, context);
  };
  table.book.book_protection_error = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
    leaf();
    assert(config.error.empty() || config.error == "access expired");
    return sendText(book(handle).value().getProtectionError(), sink, context);
  };
  table.book.book_text_reference = [](void* handle) -> int32_t {
    restorationScope();
    return book(handle).value().getSpineIndexForTextReference();
  };
  table.book.book_setup_cache = [](void* handle) {
    leaf();
    book(handle).value().setupCacheDir();
  };
  table.book.book_install_extractor = [](void* handle) {
    leaf();
    assert(book(handle).book && !book(handle).loading);
    ImageBlock::setExtractor(&book(handle).value(), [](void* raw, const char* source, const char* destination) {
      return static_cast<Epub*>(raw)->extractItemToFile(source, destination);
    });
  };
  table.book.book_clear_extractor = [] { leaf(); ImageBlock::setExtractor(nullptr, nullptr); };
  table.physical.image_clear_failures = [] { leaf(); ImageBlock::clearRenderFailures(); };
  table.physical.settings_snapshot = [](uint8_t resolveFont, XpEpubSettings* out) {
    leaf();
    assert(resolveFont == 0);
    assert(out);
    *out = {};
    out->embedded_style = SETTINGS.embeddedStyle;
  };
  table.physical.loan_begin = [](void* root) -> uint8_t {
    leaf();
    assert(root == activeFixture && !activeFixture->loan);
    activeFixture->loan.emplace(activeFixture->renderer);
    return 1;
  };
  table.physical.loan_drop = [](void* root) {
    leaf();
    assert(root == activeFixture && activeFixture->loan);
    activeFixture->loan.reset();
  };
  table.physical.file_open_read = [](XpAppText path, XpAppText origin) -> void* {
    assert(origin.len == 3 && std::memcmp(origin.data, "ERS", 3) == 0);
    leaf();
    assert(!liveFile && owned(path) == "/cache/progress.bin");
    auto file = std::make_unique<HalFile>();
    if (!Storage.openFileForRead("ERS", owned(path), *file)) return nullptr;
    liveFile = file.release();
    return liveFile;
  };
  table.physical.file_read = [](void* handle, uint8_t* bytes, uint32_t length) -> int32_t {
    leaf();
    assert(handle == liveFile && liveFile->open && length == 10);
    return liveFile->read(bytes, length);
  };
  table.physical.file_close = [](void* handle) -> uint8_t {
    leaf();
    assert(handle == liveFile && liveFile->open);
    delete liveFile;
    liveFile = nullptr;
   return 1; };
  table.physical.section_close = [](void* handle) {
    leaf();
    assert(handle && handle == liveSection);
    delete liveSection;
    liveSection = nullptr;
  };
  table.book.clippings_load = [](void* handle) -> uint8_t {
    restorationScope();
    const auto& value = book(handle).value();
    return CLIPPINGS.loadForBook(value.getPath(), value.getTitle(), value.getAuthor(), "epub");
  };
  table.physical.clippings_unload = [] { leaf(); CLIPPINGS.unload(); };
  table.physical.radio_after_paint = [] { leaf(); bleturner::afterPaint(); };
  table.physical.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    leaf();
    assert(severity <= 1);
#ifdef LIFETIME_RELEASE_LOG
    if (severity == 0) return;
#endif
    std::printf("%.*s %.*s\n", static_cast<int>(origin.len), reinterpret_cast<const char*>(origin.data),
                static_cast<int>(message.len), reinterpret_cast<const char*>(message.data));
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

void EpubReaderActivity::initialize() {
  assert(resourceDepth == 1 && !modelDepth);
  if (owner) {
    assert(activeFixture == this);
    return;
  }
  assert(!activeFixture && !liveBook && !liveSection && !liveFile);
  assert(config.error.empty() || config.error == "access expired");
  activeFixture = this;
  if (epub) {
    liveBook = new BookHandle;
    liveBook->book = std::move(epub);
  }
  liveSection = section.release();
  XpEpubTestLifetimeSeed seed{};
  seed.native = this;
  seed.book = liveBook;
  seed.section = liveSection;
  seed.path = wire(bookPath);
  seed.spine = currentSpineIndex;
  seed.next_page = nextPageNumber;
  seed.cached_spine = cachedSpineIndex;
  seed.cached_count = cachedChapterTotalPageCount;
  seed.cached_offset = cachedVisibleTextOffset.value_or(0);
  seed.has_cached_offset = cachedVisibleTextOffset.has_value();
  seed.radio_released = radioReleasedForBuild.load();
  assert(footnoteDepth >= 0 && footnoteDepth <= 3);
  seed.history_depth = static_cast<uint8_t>(footnoteDepth);
  seed.move_to_read = pendingReadFolderMove;
  seed.context = this;
  seed.settle_overlay = [](void* context) {
    leaf(); assert(context == activeFixture);
    activeFixture->settleOverlayRefresh();
  };
  seed.discard_overlay = [](void* context) {
    leaf(); assert(context == activeFixture);
    activeFixture->discardOverlayPage();
  };
  seed.save_links = [](void* context) {
    leaf(); assert(context == activeFixture && liveBook);
    activeFixture->saveLinkStack();
  };
  seed.load_links = [](void* context) {
    restorationScope(); assert(context == activeFixture);
    activeFixture->loadLinkStack();
  };
  seed.load_bookmarks = [](void* context) {
    restorationScope(); assert(context == activeFixture);
    activeFixture->loadCachedBookmarks();
  };
  seed.disable_fast = [](void* context) {
    leaf(); assert(context == activeFixture);
    activeFixture->disableFastInitialRefresh();
  };
  seed.destination = [](void* context, XpAppText source, XpAppTextSink sink, void* sinkContext) -> uint8_t {
    leaf(); assert(context == activeFixture && liveBook);
    return sendText(buildReadFolderDestination(owned(source)), sink, sinkContext);
  };
  seed.move_book = [](void* context, XpAppText source, XpAppText destination, XpAppText cache) {
    leaf(); assert(context == activeFixture && !liveBook);
    moveFinishedBookToReadFolder(owned(source), owned(destination), owned(cache));
  };
  owner = xp_epub_test_lifetime_create_legacy_book(&seed, &leaves.physical, &runtime, &resources, &leaves.book);
  assert(owner && !modelDepth && !epub && !section);
}
bool EpubReaderActivity::loadBook() {
  ResourceScope access;
  initialize();
  const bool loaded = xp_epub_test_lifetime_load(owner) != 0;
  assert(!modelDepth && !loan && !liveFile && renderer.framebuffer);
  return loaded;
}
void EpubReaderActivity::observe() {
  ResourceScope access;
  initialize();
  XpEpubTestLifetimeObservation state{};
  xp_epub_test_lifetime_observe(owner, &state);
  currentSpineIndex = state.spine;
  nextPageNumber = state.next_page;
  cachedSpineIndex = state.cached_spine;
  cachedChapterTotalPageCount = state.cached_count;
  cachedVisibleTextOffset = state.has_cached_offset ? std::optional<uint32_t>(state.cached_offset) : std::nullopt;
  observedBook = state.has_book != 0;
  assert(observedBook == bool(liveBook));
  switch (state.failure) {
    case 0: loadProtectionError.clear(); break;
    case 2: loadProtectionError = "access expired"; break;
    default: assert(false && "failure tag outside the historical fixture cases"); std::abort();
  }
  assert(!modelDepth);
}
EpubReaderActivity::~EpubReaderActivity() {
  ResourceScope access;
  initialize();
  xp_epub_test_lifetime_destroy(owner);
  owner = nullptr;
  assert(!modelDepth && !loan && !liveFile && !liveBook && !liveSection && renderer.framebuffer);
  assert(!ImageBlock::context && !ImageBlock::extractor);
  activeFixture = nullptr;
}
