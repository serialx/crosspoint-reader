// Every slot is a valid typed function, even when the fixture cannot call it.
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) {
    leaf();
    assert(false && "unexpected build-policy leaf");
    std::abort();
  }
};
#define UNUSED_LEAF(field) table.field = &Unexpected<decltype(table.field)>::call

static void beginPublication(Section* section) {
  leaf();
  assert(section && section == liveSection && !publishing);
  publishing = true;
  publicationStep = 0;
  // The old build fixture has no page-count or partial-cache inputs.
  publicationValues[0] = 0;
  publicationValues[1] = 0;
  publicationValues[2] = section->building;
  publicationValues[3] = section->complete;
  publicationValues[4] = 0;
}
static void publicationRead(void* section, unsigned step, uint16_t value) {
  leaf();
  assert(publishing && section == liveSection);
  assert(publicationStep++ == step && value == publicationValues[step]);
  if (step == 4) {
    assert(publicationStep == 5);
    publishing = false;
    ++publicationCount;
  }
}
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
  table.translate = [](uint16_t id) -> XpAppText {
    leaf();
    assert(id == static_cast<uint16_t>(StrId::STR_INDEXING));
    return {indexingLabel, sizeof(indexingLabel) - 1};
  };
  table.popup_draw = [](XpAppText label) -> XpUiRect {
    leaf();
    assert(activeFixture && label.data == indexingLabel && label.len == sizeof(indexingLabel) - 1);
    GUI.drawPopup(activeFixture->renderer, STR_INDEXING);
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
  table.now_ms = []() -> uint32_t { leaf(); return millis(); };
  table.delay_ms = [](uint32_t ms) { leaf(); delay(ms); };
  table.render_try_lock = []() -> void* {
    leaf();
    auto* token = new RenderLock(RenderLock::Mode::Try);
    if (!token->ownsLock()) {
      // The historical fake closes even a failed attempt; no token reaches Rust.
      delete token;
      return nullptr;
    }
    return token;
  };
  table.render_unlock = [](void* token) {
    leaf();
    assert(token);
    delete static_cast<RenderLock*>(token);
  };
  table.model_lock = [] { assert(modelDepth == 0); ++modelDepth; };
  table.model_unlock = [] { assert(modelDepth == 1); --modelDepth; };
  table.notify_render = [] { event("update"); };
  return table;
}();

static const XpEpubLeaves leaves = [] {
  XpEpubLeaves table{};
  static_assert(sizeof(XpEpubLeaves) == 200 * sizeof(void (*)()));
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
  table.strip_release_idle = [](void* native) -> uint8_t {
    leaf();
    assert(native && native == activeFixture);
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
    return static_cast<BuildFixture*>(native)->stripBuffers.releaseIdle();
#else
    return 0;
#endif
  };
  table.heap_snapshot = [](XpEpubHeap* out) {
    leaf();
    assert(out);
    out->free_bytes = ESP.getFreeHeap();
    out->largest_block = ESP.getMaxAllocHeap();
  };
  table.radio_status = [](XpEpubRadio* out, XpEpubPeer* peerOrNull) {
    leaf();
    assert(out && peerOrNull == nullptr);
    const auto status = bleturner::status();
    *out = {};
    out->compiled_in = 1;
    out->running = status.running;
    out->starting = status.starting;
    out->stopping = status.stopping;
  };
  table.radio_before_build = []() -> uint8_t {
    leaf();
    return bleturner::beforeChapterBuild() != bleturner::BuildRelease::NotHeld;
  };
  table.radio_holds_heap = []() -> uint8_t { leaf(); return bleturner::holdsHeap(); };
  table.render_geometry = [](XpEpubGeometry* out) {
    leaf();
    assert(out && activeFixture);
    *out = {};
    out->has_framebuffer = activeFixture->renderer.hasFrameBuffer();
  };
  table.input_pressed = [](uint32_t button) -> uint8_t {
    leaf();
    assert(activeFixture && button < 7);
    return activeFixture->mappedInput.isPressed(static_cast<MappedInputManager::Button>(button));
  };
  table.input_raw_contact = []() -> uint8_t { leaf(); return buttonContactClosed(); };
  table.loan_count = []() -> uint32_t {
    leaf();
    assert(activeFixture);
    return activeFixture->renderer.frameBufferLoanCount();
  };
  table.section_close = [](void* section) {
    leaf();
    assert(section && section == liveSection && !publishing);
    delete static_cast<Section*>(section);
  };
  table.section_page_count = [](void* section) -> uint16_t {
    publicationRead(section, 0, 0);
    return 0;
  };
  table.section_estimated_pages = [](void* section) -> int32_t {
    publicationRead(section, 1, 0);
    return 0;
  };
  table.section_building = [](void* section) -> uint8_t {
    leaf();
    assert(section && section == liveSection);
    const auto value = static_cast<Section*>(section)->isBuilding();
    if (publishing) publicationRead(section, 2, value);
    else assert(lockDepth == 1);
    return value;
  };
  table.section_complete = [](void* section) -> uint8_t {
    leaf();
    assert(section && section == liveSection);
    const auto value = static_cast<Section*>(section)->isBuildComplete();
    if (publishing) publicationRead(section, 3, value);
    else assert(lockDepth == 1);
    return value;
  };
  table.section_partial = [](void* section) -> uint8_t {
    publicationRead(section, 4, 0);
    return 0;
  };
  table.section_build_more = [](void* section, int32_t pages, uint8_t yieldOnInput) -> uint8_t {
    leaf();
    assert(section && section == liveSection && pages == 2 && yieldOnInput == 1);
    auto* value = static_cast<Section*>(section);
    const bool result = value->buildSomeMore(pages, &buttonContactClosed);
    beginPublication(value);
    return result;
  };
  table.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    leaf();
    assert(severity <= 1);
#ifdef BUILD_RELEASE_LOG
    if (severity == 0) return;
#endif
    std::printf("%.*s %.*s\n", int(origin.len), reinterpret_cast<const char*>(origin.data),
                int(message.len), reinterpret_cast<const char*>(message.data));
  };
  return table;
}();

#undef UNUSED_LEAF

static void buildingRead(void* context, uint8_t building) {
  leaf();
  assert(context == activeFixture && liveSection && !publishing && lockDepth == 0);
  const bool observed = liveSection->isBuilding();
  assert(building == uint8_t(observed));
}
static uint8_t reposition(void* context) {
  leaf();
  assert(context == activeFixture && liveSection && !publishing && lockDepth == 1);
  event("reposition", activeFixture->reposition);
  return activeFixture->reposition;
}

void BuildFixture::initialize() {
  if (owner) {
    assert(activeFixture == this);
    return;
  }
  assert(!activeFixture && !lockDepth && !modelDepth && !publishing);
  activeFixture = this;
  XpEpubTestBuildSeed seed{};
  seed.native = this;
  seed.section = section.get();
  seed.spine = currentSpineIndex;
  seed.active_page = activePage;
  seed.next_page = nextPageNumber;
  seed.failed_spine = backgroundBuildFailedSpine;
  seed.refresh = refresh;
  seed.radio_released = radioReleasedForBuild;
  seed.heap_paused = buildHeapPaused;
  seed.popup = buildPopupPending;
  seed.overlay = static_cast<uint8_t>(overlay);
  seed.pending_turn = pendingManualTurn;
  seed.stale = pageBufferStale;
  seed.context = this;
  seed.building_read = buildingRead;
  seed.reposition = ::reposition;
  const unsigned before = publicationCount;
  if (section) beginPublication(section.get());
  owner = xp_epub_test_build_create(&seed, &leaves, &runtime, &resources);
  assert(owner && !publishing && !lockDepth && !modelDepth);
  assert(publicationCount == before + (section ? 1u : 0u));
  section.release();
}
BuildFixture::~BuildFixture() {
  if (owner) xp_epub_test_build_destroy(owner);
  assert(activeFixture == this && !liveSection && !publishing && !lockDepth && !modelDepth);
  activeFixture = nullptr;
}
XpEpubTestBuildObservation BuildFixture::observe() {
  assert(owner && !publishing && !lockDepth && !modelDepth);
  XpEpubTestBuildObservation state{};
  xp_epub_test_build_observe(owner, &state);
  assert(!modelDepth);
  return state;
}
bool BuildFixture::makeRoomForChapterBuild() {
  initialize();
  return xp_epub_test_build_room(owner);
}
bool BuildFixture::buildTickHeapGate() {
  initialize();
  return xp_epub_test_build_heap(owner);
}
void BuildFixture::showBuildPopup() {
  initialize();
  xp_epub_test_build_popup(owner);
}
bool BuildFixture::anyButtonPressed() {
  initialize();
  return xp_epub_test_build_buttons(owner);
}
void BuildFixture::backgroundBuildTick() {
  initialize();
  xp_epub_test_build_background(owner);
  assert(!publishing && !lockDepth && !modelDepth);
}
bool BuildFixture::backgroundBuildWanted() {
  initialize();
  return xp_epub_test_build_wanted(owner);
}
bool BuildFixture::skipLoopDelay() {
  initialize();
  return xp_epub_test_build_delay(owner);
}
