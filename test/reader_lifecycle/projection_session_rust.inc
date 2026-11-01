#include <cstdlib>

namespace projection_adapter {
static EpubReaderActivity* activeFixture;
static Section* liveSection;
static bool quietLock, publishing, destroying;
static unsigned publicationStep, liveSections;
static int token;
static void leaf() { assert(modelDepth == 0); }
static void unlocked() { leaf(); assert(lockDepth == 0); }
static void locked() { leaf(); assert(lockDepth == 1); }
static void context(void* value) { unlocked(); assert(value && value == activeFixture && !publishing); }
static void publication(void* section, unsigned step) {
  locked(); assert(quietLock && publishing && liveSection && section == liveSection && publicationStep == step);
  ++publicationStep;
}
static void checkBook(void* book) { leaf(); assert(activeFixture && book && book == activeFixture->epub.get()); }
static XpAppText wire(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) { leaf(); assert(false && "unexpected projection leaf"); std::abort(); }
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
  table.translate = [](uint16_t id) -> XpAppText {
    unlocked(); assert(id == static_cast<uint16_t>(StrId::STR_SAVE_PROGRESS_FAILED));
    const char* value = tr(STR_SAVE_PROGRESS_FAILED);
    return {reinterpret_cast<const uint8_t*>(value), std::strlen(value)};
  };
  table.popup_draw = [](XpAppText text) -> XpUiRect {
    unlocked(); assert(activeFixture && text.data && text.len == std::strlen("Save progress failed"));
    assert(std::memcmp(text.data, "Save progress failed", text.len) == 0);
    const std::string value(reinterpret_cast<const char*>(text.data), text.len);
    GUI.drawPopup(activeFixture->renderer, value.c_str()); return {};
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
  table.model_lock = [] { leaf(); ++modelDepth; };
  table.model_unlock = [] { assert(modelDepth == 1); --modelDepth; };
  table.render_lock = []() -> void* {
    unlocked(); ++lockDepth; if (!quietLock) event("lock", lockDepth); return &token;
  };
  table.render_unlock = [](void* value) {
    locked(); assert(value == &token); if (!quietLock) event("unlock", lockDepth); --lockDepth;
  };
  table.now_ms = []() -> uint32_t { leaf(); assert(lockDepth <= 1); return millis(); };
  return table;
}();
static const XpEpubLeaves leaves = [] {
  XpEpubLeaves table{};
  static_assert(sizeof(XpEpubLeaves) == 205 * sizeof(void (*)()));
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
  UNUSED_LEAF(book_thumbnail);
  UNUSED_LEAF(book_spine);
  UNUSED_LEAF(book_toc);
  UNUSED_LEAF(book_spine_for_toc);
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
  UNUSED_LEAF(page_footnote);
  UNUSED_LEAF(page_link);
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
  UNUSED_LEAF(input_long_press);
  UNUSED_LEAF(input_released);
  UNUSED_LEAF(input_screen_long);
  UNUSED_LEAF(input_screen_tap);
  UNUSED_LEAF(input_raw_contact);
  UNUSED_LEAF(render_busy);
  UNUSED_LEAF(file_open_read);
  UNUSED_LEAF(file_open_write);
  UNUSED_LEAF(file_read);
  UNUSED_LEAF(file_write);
  UNUSED_LEAF(file_flush);
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
  table.section_page_count = [](void* section) -> uint16_t { publication(section, 0); return liveSection->pageCount; };
  table.section_estimated_pages = [](void* section) -> int32_t { publication(section, 1); return liveSection->estimated; };
  table.section_building = [](void* section) -> uint8_t { publication(section, 2); return liveSection->isBuilding(); };
  table.section_complete = [](void* section) -> uint8_t { publication(section, 3); return 0; };
  table.section_partial = [](void* section) -> uint8_t { publication(section, 4); return 0; };
  table.section_close = [](void* section) {
    leaf(); assert(section && section == liveSection && liveSections == 1 && !publishing);
    if (destroying) unlocked(); else locked();
    delete liveSection; liveSection = nullptr; --liveSections;
  };
  table.section_paragraph_for_page = [](void* section, uint16_t page, uint16_t* result) -> uint8_t {
    locked(); assert(result && section == liveSection && liveSection && !publishing);
    const auto value = liveSection->getParagraphIndexForPage(page);
    if (value) *result = *value;
    return value.has_value();
  };
  table.section_offset_for_page = [](void* section, uint16_t page, uint32_t* result) -> uint8_t {
    locked(); assert(result && section == liveSection && liveSection && !publishing);
    const auto value = liveSection->getVisibleTextOffsetForPage(page);
    if (value) *result = *value;
    return value.has_value();
  };
  table.section_toc_for_page = [](void* section, void* book, int32_t spine, int32_t page) -> int32_t {
    locked(); checkBook(book); assert(!publishing);
    if (section) {
      assert(section == liveSection && liveSection);
      return liveSection->getTocIndexForPage(page);
    }
    assert(!liveSection);
    const Section cached(activeFixture->epub, spine, activeFixture->renderer);
    return cached.getTocIndexForPage(page);
  };
  table.book_size = [](void* book) -> uint32_t { checkBook(book); return static_cast<Epub*>(book)->getBookSize(); };
  table.book_spine_count = [](void* book) -> int32_t { checkBook(book); return static_cast<Epub*>(book)->getSpineItemsCount(); };
  table.book_title = [](void* book, XpAppTextSink sink, void* context) -> uint8_t {
    checkBook(book); assert(sink); const auto title = static_cast<Epub*>(book)->getTitle(); return sink(context, wire(title));
  };
  table.book_progress = [](void* book, int32_t spine, float fraction) -> float {
    checkBook(book); return static_cast<Epub*>(book)->calculateProgress(spine, fraction);
  };
  table.input_facts = [](XpEpubInputFacts* out) {
    unlocked(); assert(out); *out = {}; out->touch = BoardConfig::hasTouch(); out->board_touch = BoardConfig::hasTouch();
  };
  table.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    unlocked(); assert(!resolve && out); *out = {}; out->long_menu = SETTINGS.longPressMenuFunction;
  };
  table.has_sync_credentials = []() -> uint8_t { unlocked(); return KOREADER_STORE.hasCredentials(); };
  return table;
}();
#undef UNUSED_LEAF

static uint8_t toolbar(void* value) { context(value); return activeFixture->usesToolbarMenu(); }
static uint8_t baseInput(void* value) { context(value); return activeFixture->ReaderActivity::inputOverPage(); }
static void acknowledge(void* value) { context(value); bleturner::acknowledgeLinkNote(); }
static void clear(void* value) {
  locked(); assert(value == activeFixture && !publishing); activeFixture->clearDeferredReposition();
}
static void fallback(void* value) { context(value); event("fallback-percent"); }
static void estimate(void* value, int32_t count) {
  context(value); assert(liveSection && count == liveSection->estimated);
  const auto observed = liveSection->estimatedTotalPages(); assert(count == observed);
}
}  // namespace projection_adapter

void EpubReaderActivity::initialize() {
  using namespace projection_adapter;
  unlocked(); assert(!owner && !activeFixture && !liveSection && !liveSections && !publishing);
  activeFixture = this; liveSection = section.release(); liveSections = liveSection ? 1 : 0;
  XpEpubTestProjectionSeed seed{};
  seed.native = this; seed.book = epub.get(); seed.section = liveSection;
  seed.spine = currentSpineIndex; seed.page = liveSection ? liveSection->currentPage : 0;
  seed.next_page = nextPageNumber; seed.cached_count = cachedChapterTotalPageCount;
  seed.last_turn = lastPageTurnTime; seed.pending_page = pendingPageJump.value_or(0);
  seed.automatic = automaticPageTurnActive; seed.sync_error = pendingSyncSaveError;
  seed.overlay = overlay != Overlay::None; seed.context = this;
  seed.toolbar_menu = projection_adapter::toolbar; seed.base_input = projection_adapter::baseInput;
  seed.acknowledge = acknowledge; seed.clear = clear; seed.fallback = fallback; seed.estimate = estimate;
  quietLock = true; auto* token = runtime.render_lock(); publishing = true; publicationStep = 0;
  owner = xp_epub_test_projection_create(&seed, &leaves, &runtime, &resources);
  assert(owner && publicationStep == (liveSection ? 5u : 0u));
  publishing = false; runtime.render_unlock(token); quietLock = false; unlocked();
}
void* EpubReaderActivity::getOwner() const {
  if (!owner) const_cast<EpubReaderActivity*>(this)->initialize();
  assert(projection_adapter::activeFixture == this); return owner;
}
EpubReaderActivity::~EpubReaderActivity() {
  using namespace projection_adapter;
  unlocked();
  if (!owner) return;
  assert(activeFixture == this && !publishing); destroying = true;
  xp_epub_test_projection_destroy(owner);
  destroying = false; owner = nullptr; activeFixture = nullptr;
  assert(!liveSection && !liveSections && !section); unlocked();
}
XpEpubTestProjectionObservation EpubReaderActivity::observe() const {
  XpEpubTestProjectionObservation value{};
  xp_epub_test_projection_observe(getOwner(), &value);
  projection_adapter::leaf(); assert(bool(value.has_section) == bool(projection_adapter::liveSection)); return value;
}
void EpubReaderActivity::dump() const {
  const auto value = observe();
  std::printf("state %d %d %d %d %lu %d %d %d\n", value.spine, value.next_page, value.pending_page,
              value.has_section ? value.page : -99, static_cast<unsigned long>(value.last_turn),
              value.automatic, value.sync_error, value.has_section);
}
ChapterPosition EpubReaderActivity::chapterPosition() const {
  XpEpubTestChapterPosition result{}; xp_epub_test_projection_chapter(getOwner(), &result); return {result.page, result.count};
}
int EpubReaderActivity::bookPercentFor(const ChapterPosition& value) const {
  return xp_epub_test_projection_percent(getOwner(), {value.pageIndex, value.totalPages});
}
int EpubReaderActivity::currentTocIndex() const {
  auto* owner = getOwner();
  // Shipping Contents/Chapter callers already hold this nonrecursive lock.
  // Suppress only the fixture's outer lock events; physical callbacks assert it.
  constexpr bool holdTocLock = true;
  projection_adapter::quietLock = true;
  auto* token = holdTocLock ? projection_adapter::runtime.render_lock() : nullptr;
  const auto result = xp_epub_test_projection_toc(owner);
  if (token) projection_adapter::runtime.render_unlock(token);
  projection_adapter::quietLock = false;
  projection_adapter::unlocked(); return result;
}
ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  XpAppScreenshot value{}; xp_epub_test_projection_screenshot(getOwner(), &value);
  ScreenshotInfo result{}; result.readerType = static_cast<ScreenshotInfo::ReaderType>(value.reader_type);
  std::memcpy(result.title, value.title, sizeof(result.title));
  result.spineIndex = value.spine; result.currentPage = value.current_page; result.totalPages = value.total_pages;
  result.progressPercent = value.progress_percent; return result;
}
int EpubReaderActivity::getProgressBasisPoints() const { return xp_epub_test_projection_basis(getOwner()); }
CrossPointPosition EpubReaderActivity::getCurrentPosition() const {
  XpEpubPosition value{}; xp_epub_test_projection_position(getOwner(), &value);
  CrossPointPosition result{}; result.spineIndex = value.spine; result.pageNumber = value.page; result.totalPages = value.count;
  result.visibleTextOffset = value.visible_offset; result.hasVisibleTextOffset = value.has_visible_offset;
  result.paragraphIndex = value.paragraph; result.hasParagraphIndex = value.has_paragraph;
  result.liIndex = value.list_item; result.hasLiIndex = value.has_list_item;
  std::memcpy(result.xpathAnchorId, value.anchor, sizeof(result.xpathAnchorId));
  result.hasResolvedSpineIndex = value.resolved_spine; result.hasMappedPage = value.mapped_page; return result;
}
unsigned long EpubReaderActivity::confirmLongPressThreshold() const { return xp_epub_test_projection_threshold(getOwner()); }
bool EpubReaderActivity::remoteShortcut(bool menu) {
  // The real owner decides; this adapter only dispatches the historical stubs.
  const auto selected = xp_epub_test_projection_remote(getOwner(), menu);
  switch (selected) {
    case 0: return false;
    case 1: startClipSelection(); break;
    case 2: openOverlay(Overlay::Toolbar); break;
    case 3: openReaderMenu(); break;
    default: assert(false); std::abort();
  }
  return true;
}
bool EpubReaderActivity::isAtEndOfBook() const { return xp_epub_test_projection_at_end(getOwner()); }
void EpubReaderActivity::onReturnFromEndOfBook() { xp_epub_test_projection_return_end(getOwner()); }
void EpubReaderActivity::onEndOfBookRendered() { xp_epub_test_projection_rendered_end(getOwner()); }
bool EpubReaderActivity::inputOverPage() const { return xp_epub_test_projection_input_over(getOwner()); }
bool EpubReaderActivity::pageTurn(bool forward) { return xp_epub_test_projection_turn(getOwner(), forward); }
bool EpubReaderActivity::skipPages(int amount) { return xp_epub_test_projection_skip(getOwner(), amount); }
bool EpubReaderActivity::applyPageMove(const XpUiReaderMove& movement, bool record) {
  return xp_epub_test_projection_move(getOwner(), &movement, record);
}

static void projectionOwnerEdges() {
  overrideProgress = true;
  struct Edge { uint32_t bits; int32_t percent, screenshot, basis; };
  unsigned edges = 0, moves = 0;
  for (const Edge edge : {Edge{0x7fc12345u, INT32_MAX, 100, 10000}, Edge{0xffc12345u, INT32_MIN, 0, 0},
                          Edge{0x7f800000u, 100, 100, 10000}, Edge{0xff800000u, 0, 0, 0},
                          Edge{0x7f7fffffu, 100, 100, 10000}, Edge{0xff7fffffu, 0, 0, 0}}) {
    progressBits = edge.bits; EpubReaderActivity owner;
    assert(owner.bookPercentFor({0, 4}) == edge.percent);
    assert(owner.getScreenshotInfo().progressPercent == edge.screenshot);
    assert(owner.getProgressBasisPoints() == edge.basis); ++edges;
  }
  overrideProgress = false;
  for (uint8_t kind = 0; kind < 5; ++kind)
    for (uint8_t flags = 0; flags < 4; ++flags) {
      EpubReaderActivity owner; owner.getOwner();
      lastClockDepth = -1; clockCalls = 0;
      XpUiReaderMove movement{}; movement.kind = kind; movement.value = 3; movement.last_page = flags & 1;
      const bool result = owner.applyPageMove(movement, flags & 2);
      const auto value = owner.observe();
      assert(result == (kind >= 1 && kind <= 3));
      assert(lockDepth == 0 && modelDepth == 0);
      assert(value.spine == ((kind == 2 || kind == 3) ? 3 : 0));
      assert(value.page == (kind == 1 ? 3 : 0));
      assert(value.next_page == (kind == 2 ? 0 : 2));
      assert(value.has_section == (kind != 2));
      assert(value.pending_page == ((kind == 2 && (flags & 1)) ? UINT16_MAX : 123));
      const bool recorded = result && (flags & 2);
      assert(value.last_turn == (recorded ? 1234u : 56u));
      assert(clockCalls == unsigned(recorded));
      assert(lastClockDepth == (recorded ? int(kind == 2) : -1)); ++moves;
    }
  assert(edges == 6 && moves == 20);
  std::puts("owner-edges 6 float 20 movement cases");
}
