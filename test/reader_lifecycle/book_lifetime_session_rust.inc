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

static const XpEpubLeaves leaves = [] {
  XpEpubLeaves table{};
  static_assert(sizeof(XpEpubLeaves) == 213 * sizeof(void (*)()));
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
  UNUSED_LEAF(book_spine);
  UNUSED_LEAF(book_toc);
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
  UNUSED_LEAF(page_word_font);
  UNUSED_LEAF(page_line_font);
  UNUSED_LEAF(page_word_y_offset);
  UNUSED_LEAF(page_line_ruby_shift);
  UNUSED_LEAF(font_text_advance);
  UNUSED_LEAF(log_enabled);
  UNUSED_LEAF(toolbar_reset_navigation);
  UNUSED_LEAF(input_snapshot);
  UNUSED_LEAF(page_render);
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
  table.book_open = [](XpAppText path) -> void* {
    leaf();
    assert(!liveBook);
    auto loaded = makeUniqueNoThrow<Epub>(owned(path), "/.crosspoint");
    if (!loaded) return nullptr;
    auto handle = std::make_unique<BookHandle>();
    handle->loading = std::move(loaded);
    liveBook = handle.release();
    return liveBook;
  };
  table.book_load = [](void* handle, uint8_t buildMissing, uint8_t skipCss) -> uint8_t {
    leaf();
    assert(activeFixture->renderer.framebuffer == config.cached);
    auto& resource = book(handle);
    if (!resource.value().load(buildMissing != 0, skipCss != 0)) return 0;
    if (resource.loading) resource.book = std::move(resource.loading);
    return 1;
  };
  table.book_close = [](void* handle) {
    leaf();
    assert(handle == liveBook && !liveFile);
    assert(ImageBlock::context != &book(handle).value());
    delete liveBook;
    liveBook = nullptr;
  };
  table.book_path = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
    leaf();
    return sendText(book(handle).value().getPath(), sink, context);
  };
  table.book_cache_path = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
    leaf();
    return sendText(book(handle).value().getCachePath(), sink, context);
  };
  table.book_title = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
    leaf();
    return sendText(book(handle).value().getTitle(), sink, context);
  };
  table.book_author = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
    leaf();
    return sendText(book(handle).value().getAuthor(), sink, context);
  };
  table.book_protection_error = [](void* handle, XpAppTextSink sink, void* context) -> uint8_t {
    leaf();
    assert(config.error.empty() || config.error == "access expired");
    return sendText(book(handle).value().getProtectionError(), sink, context);
  };
  table.book_text_reference = [](void* handle) -> int32_t {
    restorationScope();
    return book(handle).value().getSpineIndexForTextReference();
  };
  table.book_setup_cache = [](void* handle) {
    leaf();
    book(handle).value().setupCacheDir();
  };
  table.book_install_extractor = [](void* handle) {
    leaf();
    assert(book(handle).book && !book(handle).loading);
    ImageBlock::setExtractor(&book(handle).value(), [](void* raw, const char* source, const char* destination) {
      return static_cast<Epub*>(raw)->extractItemToFile(source, destination);
    });
  };
  table.book_clear_extractor = [] { leaf(); ImageBlock::setExtractor(nullptr, nullptr); };
  table.image_clear_failures = [] { leaf(); ImageBlock::clearRenderFailures(); };
  table.settings_snapshot = [](uint8_t resolveFont, XpEpubSettings* out) {
    leaf();
    assert(resolveFont == 0);
    assert(out);
    *out = {};
    out->embedded_style = SETTINGS.embeddedStyle;
  };
  table.loan_begin = [](void* root) -> uint8_t {
    leaf();
    assert(root == activeFixture && !activeFixture->loan);
    activeFixture->loan.emplace(activeFixture->renderer);
    return 1;
  };
  table.loan_drop = [](void* root) {
    leaf();
    assert(root == activeFixture && activeFixture->loan);
    activeFixture->loan.reset();
  };
  table.file_open_read = [](XpAppText path, XpAppText origin) -> void* {
    assert(origin.len == 3 && std::memcmp(origin.data, "ERS", 3) == 0);
    leaf();
    assert(!liveFile && owned(path) == "/cache/progress.bin");
    auto file = std::make_unique<HalFile>();
    if (!Storage.openFileForRead("ERS", owned(path), *file)) return nullptr;
    liveFile = file.release();
    return liveFile;
  };
  table.file_read = [](void* handle, uint8_t* bytes, uint32_t length) -> int32_t {
    leaf();
    assert(handle == liveFile && liveFile->open && length == 10);
    return liveFile->read(bytes, length);
  };
  table.file_close = [](void* handle) -> uint8_t {
    leaf();
    assert(handle == liveFile && liveFile->open);
    delete liveFile;
    liveFile = nullptr;
   return 1; };
  table.section_close = [](void* handle) {
    leaf();
    assert(handle && handle == liveSection);
    delete liveSection;
    liveSection = nullptr;
  };
  table.clippings_load = [](void* handle) -> uint8_t {
    restorationScope();
    const auto& value = book(handle).value();
    return CLIPPINGS.loadForBook(value.getPath(), value.getTitle(), value.getAuthor(), "epub");
  };
  table.clippings_unload = [] { leaf(); CLIPPINGS.unload(); };
  table.radio_after_paint = [] { leaf(); bleturner::afterPaint(); };
  table.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    leaf();
    assert(severity <= 1);
#ifdef LIFETIME_RELEASE_LOG
    if (severity == 0) return;
#endif
    std::printf("%.*s %.*s\n", static_cast<int>(origin.len), reinterpret_cast<const char*>(origin.data),
                static_cast<int>(message.len), reinterpret_cast<const char*>(message.data));
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
  owner = xp_epub_test_lifetime_create(&seed, &leaves, &runtime, &resources);
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
