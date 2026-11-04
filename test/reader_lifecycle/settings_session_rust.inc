#include <cstdlib>
#include <cstring>

namespace settings_adapter {
static EpubReaderActivity* activeFixture;
static Section* liveSection;
static bool publishing, quietLock, ownerLockLabel, destroying;
static unsigned publicationStep, liveSections;
static int token;
static void leaf() { assert(modelDepth == 0); }
static void unlocked() { leaf(); assert(lockDepth == 0); }
static void locked() { leaf(); assert(lockDepth == 1); }
static void context(void* value) { leaf(); assert(value && value == activeFixture && !publishing); }
static void publication(void* section, unsigned step) {
  locked(); assert(publishing && quietLock && section == liveSection && publicationStep == step);
  ++publicationStep;
}
static XpAppText wire(const char* value) {
  return {reinterpret_cast<const uint8_t*>(value), std::strlen(value)};
}
static std::string nativeText(XpAppText value) {
  assert(value.data || !value.len);
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}
static constexpr StrId nativeKeys[] = {
  StrId::STR_FONT_SIZE, StrId::STR_LINE_SPACING, StrId::STR_PARA_ALIGNMENT, StrId::STR_TIGHT,
  StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE, StrId::STR_JUSTIFY,
  StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT, StrId::STR_BOOK_S_STYLE,
};
static const char* translatedKeys[] = {
  "fixture-key-0", "fixture-key-1", "fixture-key-2", "fixture-key-3", "fixture-key-4", "fixture-key-5",
  "fixture-key-6", "fixture-key-7", "fixture-key-8", "fixture-key-9", "fixture-key-10", "fixture-key-11",
};
static FixtureStrId fixtureId(const std::string& value) {
  for (size_t i = 0; i < std::size(translatedKeys); ++i)
    if (value == translatedKeys[i]) return static_cast<FixtureStrId>(i);
  assert(false && "unknown physical translation"); std::abort();
}
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) { leaf(); assert(false && "unexpected settings leaf"); std::abort(); }
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
  table.translate = [](uint16_t key) -> XpAppText {
    locked();
    for (size_t i = 0; i < std::size(nativeKeys); ++i)
      if (key == static_cast<uint16_t>(nativeKeys[i])) return wire(translatedKeys[i]);
    assert(false && "unexpected settings translation"); std::abort();
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
    unlocked(); ++lockDepth;
    // Both historical constructors lock the same native singleton. The typed
    // orientation entry supplies only the constructor's historical event label.
    if (!quietLock) event(ownerLockLabel ? "owner-lock" : "lock", lockDepth);
    return &token;
  };
  table.render_unlock = [](void* value) {
    locked(); assert(value == &token);
    if (!quietLock) event("unlock", lockDepth);
    --lockDepth;
  };
  table.now_ms = []() -> uint32_t { unlocked(); return millis(); };
  table.notify_render = [] { unlocked(); event("update", lockDepth); };
  return table;
}();

static auto legacySection = unexpectedLegacySectionServices();
static const XpEpubLeaves leaves = [] {
  XpEpubLeaves table{};
  static_assert(sizeof(XpEpubLeaves) == 180 * sizeof(void (*)()));
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
  UNUSED_LEAF(page_load);
  UNUSED_LEAF(framebuffer_lend);
  UNUSED_LEAF(framebuffer_restore);
  legacySection.section_page_count = [](void* section) -> uint16_t {
    locked(); assert(section == liveSection && liveSection);
    if (publishing) publication(section, 0);
    return liveSection->pageCount;
  };
  legacySection.section_estimated_pages = [](void* section) -> int32_t {
    publication(section, 1); return liveSection->pageCount;
  };
  legacySection.section_building = [](void* section) -> uint8_t { publication(section, 2); return 0; };
  legacySection.section_complete = [](void* section) -> uint8_t { publication(section, 3); return 0; };
  legacySection.section_partial = [](void* section) -> uint8_t { publication(section, 4); return 0; };
  legacySection.section_close = [](void* section) {
    leaf(); assert(section && section == liveSection && liveSections == 1 && !publishing);
    if (destroying) unlocked(); else locked();
    delete liveSection; liveSection = nullptr; --liveSections;
  };
  table.settings_snapshot = [](uint8_t resolveFont, XpEpubSettings* out) {
    leaf(); assert(!resolveFont && out && !publishing);
    *out = {};
    out->orientation = SETTINGS.orientation;
    out->font_size = SETTINGS.fontPointSize;
    out->line_spacing = SETTINGS.lineSpacing;
    out->alignment = SETTINGS.paragraphAlignment;
    out->font_family = wire(SETTINGS.sdFontFamilyName);
  };
  table.settings_orientation = [](uint8_t value) { locked(); SETTINGS.orientation = value; };
  table.settings_font_size = [](uint8_t value) { unlocked(); SETTINGS.fontPointSize = value; };
  table.settings_line_spacing = [](uint8_t value) { unlocked(); SETTINGS.lineSpacing = value; };
  table.settings_alignment = [](uint8_t value) { unlocked(); SETTINGS.paragraphAlignment = value; };
  table.settings_save = [] { leaf(); assert(lockDepth <= 1); SETTINGS.saveToFile(); };
  table.render_orientation = [](uint8_t value) {
    locked(); assert(activeFixture); ReaderUtils::applyOrientation(activeFixture->renderer, value);
  };
  table.render_geometry = [](XpEpubGeometry* out) {
    unlocked(); assert(out); *out = {};
    // The observation callbacks emit each historical getter at its actual read.
    out->status_height = UITheme::getInstance().status;
    out->progress_height = UITheme::getInstance().progress;
  };
  table.fonts_ensure_loaded = [] {
    locked(); assert(activeFixture); sdFontSystem.ensureLoaded(activeFixture->renderer);
  };
  table.font_sizes = [](XpAppText family, XpAppTextSink sink, void* sinkContext) -> uint8_t {
    unlocked(); assert(sink);
    const auto name = nativeText(family);
    const auto values = readerFontPointSizes(&sdFontSystem.registry(), name.c_str());
    return sink(sinkContext, {values.data(), values.size()});
  };
  table.popup_show = [](void* popup, XpAppText title, XpAppText message, uint32_t count, int32_t selected,
                        uint8_t (*label)(void*, uint32_t, XpAppTextSink, void*), void* labelContext,
                        void (*choose)(void*, int32_t), void* choiceContext) {
    locked(); assert(activeFixture && popup == &activeFixture->overlayPopup && message.len == 0);
    assert(label && choose && choiceContext == activeFixture->owner);
    const auto titleId = fixtureId(nativeText(title));
    std::vector<std::string> labels;
    labels.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
      struct Label { std::string value; unsigned calls = 0; } result;
      const auto accepted = label(labelContext, index, [](void* opaque, XpAppText text) -> uint8_t {
        locked(); auto& result = *static_cast<Label*>(opaque);
        assert(result.calls++ == 0); result.value = nativeText(text); return 1;
      }, &result);
      locked(); assert(accepted == 1 && result.calls == 1);
      labels.push_back(std::move(result.value));
    }
    // Retain only the production choice continuation; labels are copied now.
    auto callback = [choose, choiceContext](int index) {
      unlocked(); assert(activeFixture && activeFixture->owner == choiceContext);
      choose(choiceContext, index);
      unlocked();
    };
    if (titleId == STR_FONT_SIZE) {
      activeFixture->overlayPopup.show(titleId, labels, selected, std::move(callback));
    } else {
      assert(titleId == STR_LINE_SPACING || titleId == STR_PARA_ALIGNMENT);
      std::vector<FixtureStrId> ids;
      for (const auto& text : labels) ids.push_back(fixtureId(text));
      activeFixture->overlayPopup.show(titleId, ids.data(), ids.size(), selected, std::move(callback));
    }
  };
    UNUSED_LEAF(book_facade_open);
    UNUSED_LEAF(book_facade_close);
    UNUSED_LEAF(book_set_extractor);
  return table;
}();
#undef UNUSED_LEAF

static void initial(void* value) { context(value); unlocked(); activeFixture->ReaderActivity::applyInitialOrientation(); }
static void remember(void* value) { context(value); locked(); activeFixture->rememberCurrentContentOffset(); }
static void discard(void* value) { context(value); locked(); activeFixture->discardOverlayPage(); }
static void paint(void* value) { context(value); unlocked(); activeFixture->paintOverlayPopup(); }
static void statusHeight(void* value, int32_t height) {
  context(value); unlocked(); assert(height == UITheme::getInstance().status);
  const auto observed = UITheme::getInstance().getStatusBarHeight(); assert(height == observed);
}
static void progressHeight(void* value, int32_t height) {
  context(value); unlocked(); assert(height == UITheme::getInstance().progress);
  const auto observed = UITheme::getInstance().getProgressBarHeight(); assert(height == observed);
}
static void snap(void* value, uint8_t current) {
  context(value); unlocked(); assert(current == SETTINGS.fontPointSize && !sizes.empty()); event("snap", current);
}
}  // namespace settings_adapter

void EpubReaderActivity::initialize() {
  using namespace settings_adapter;
  unlocked(); assert(!owner && !activeFixture && !liveSection && !liveSections && !publishing && !destroying);
  activeFixture = this;
  liveSection = section.release();
  liveSections = liveSection ? 1 : 0;
  XpEpubTestSettingsSeed seed{};
  seed.native = this; seed.section = liveSection; seed.popup = &overlayPopup;
  seed.spine = currentSpineIndex; seed.page = liveSection ? liveSection->currentPage : 0;
  seed.cached_spine = cachedSpineIndex; seed.cached_count = cachedChapterTotalPageCount; seed.next_page = nextPageNumber;
  seed.last_turn = lastPageTurnTime; seed.duration = pageTurnDuration; seed.orientation = appliedOrientation;
  seed.automatic = automaticPageTurnActive;
  seed.hooks = {this, initial, remember, discard, paint, statusHeight, progressHeight, snap};
  quietLock = true;
  auto* token = runtime.render_lock();
  publishing = true; publicationStep = 0;
  owner = xp_epub_test_settings_create_legacy_section(&seed, &leaves, &runtime, &resources, &legacySection);
  assert(owner && publicationStep == (liveSection ? 5u : 0u));
  publishing = false;
  runtime.render_unlock(token); quietLock = false;
  unlocked();
}
EpubReaderActivity::~EpubReaderActivity() {
  using namespace settings_adapter;
  unlocked(); assert(owner && activeFixture == this && !overlayPopup.callback && !publishing);
  destroying = true;
  xp_epub_test_settings_destroy(owner);
  destroying = false;
  owner = nullptr; activeFixture = nullptr;
  unlocked(); assert(!liveSection && !liveSections && !section);
}
void EpubReaderActivity::observe() {
  using namespace settings_adapter;
  unlocked(); assert(owner && activeFixture == this);
  XpEpubTestSettingsObservation observed{};
  xp_epub_test_settings_observe(owner, &observed);
  cachedSpineIndex = observed.cached_spine; cachedChapterTotalPageCount = observed.cached_count;
  nextPageNumber = observed.next_page; lastPageTurnTime = observed.last_turn; pageTurnDuration = observed.duration;
  appliedOrientation = observed.orientation; automaticPageTurnActive = observed.automatic;
  hasSection = observed.has_section;
  assert(hasSection == bool(liveSection)); unlocked();
}
void EpubReaderActivity::applyInitialOrientation() { xp_epub_test_settings_initial(owner); }
void EpubReaderActivity::applyOrientation(uint8_t value) {
  settings_adapter::unlocked(); assert(!settings_adapter::ownerLockLabel);
  settings_adapter::ownerLockLabel = true;
  xp_epub_test_settings_orientation(owner, value);
  settings_adapter::ownerLockLabel = false;
  settings_adapter::unlocked();
}
void EpubReaderActivity::toggleAutoPageTurn(uint8_t value) { xp_epub_test_settings_auto(owner, value); }
void EpubReaderActivity::applyReaderTextSettings() { xp_epub_test_settings_text(owner); }
void EpubReaderActivity::applyTextSettingLive() { xp_epub_test_settings_live(owner); }
void EpubReaderActivity::showTextRowPopup(int row) { xp_epub_test_settings_popup(owner, row); }
