namespace view_values_adapter {
static EpubReaderActivity* active;
static int modelDepth, lockDepth;
static bool publishing;
static unsigned publicationStep;
static unsigned flagsReads, peerReads;
static const char* connectedFormatOverride;
static std::string paintedTitle;
static void leaf() { assert(modelDepth == 0 && lockDepth == 1 && active); }
static void publication(void* section, unsigned step) {
  leaf(); assert(section == active->nativeSection && publishing && publicationStep == step);
  ++publicationStep;
}
static XpAppText wire(const char* value) {
  return {reinterpret_cast<const uint8_t*>(value), std::strlen(value)};
}
static std::string nativeText(XpAppText text) {
  assert(text.data || !text.len);
  return text.len ? std::string(reinterpret_cast<const char*>(text.data), text.len) : std::string();
}
static constexpr StrId keys[] = {
  StrId::STR_ALIGN_LEFT,
  StrId::STR_ALIGN_RIGHT,
  StrId::STR_AUTO_TURN_ENABLED,
  StrId::STR_BOOK_S_STYLE,
  StrId::STR_BT_CONNECTED_TO,
  StrId::STR_BT_CONNECTING,
  StrId::STR_BT_FAILED,
  StrId::STR_BT_LOW_MEMORY,
  StrId::STR_CENTER,
  StrId::STR_EXTRA_WIDE,
  StrId::STR_FOCUS_READING,
  StrId::STR_FONT,
  StrId::STR_FONT_SIZE,
  StrId::STR_JUSTIFY,
  StrId::STR_LANDSCAPE_CCW,
  StrId::STR_LANDSCAPE_CW,
  StrId::STR_LINE_SPACING,
  StrId::STR_NORMAL,
  StrId::STR_NOTO_SANS,
  StrId::STR_ORIENTATION_INVERTED,
  StrId::STR_PARA_ALIGNMENT,
  StrId::STR_PORTRAIT,
  StrId::STR_STATE_OFF,
  StrId::STR_TIGHT,
  StrId::STR_UNNAMED,
  StrId::STR_WIDE,
};
// Action order is the existing MenuAction enum; labels come from buildMenuItems.
static constexpr StrId actionLabels[] = {
  StrId::STR_SELECT_CHAPTER, StrId::STR_FOOTNOTES, StrId::STR_TEXT_SETTINGS,
  StrId::STR_NIGHT_MODE, StrId::STR_FRONTLIGHT, StrId::STR_BT_PAGE_TURNER,
  StrId::STR_GO_TO_PERCENT, StrId::STR_AUTO_TURN_PAGES_PER_MIN, StrId::STR_ORIENTATION,
  StrId::STR_BOOKMARKS, StrId::STR_SAVE_CLIPPING, StrId::STR_VIEW_CLIPPINGS,
  StrId::STR_TOGGLE_BOOKMARK, StrId::STR_SCREENSHOT_BUTTON, StrId::STR_DISPLAY_QR,
  StrId::STR_GO_HOME_BUTTON, StrId::STR_SYNC_PROGRESS, StrId::STR_DELETE_CACHE, StrId::STR_LOOKUP,
};
static constexpr const char* actionTexts[] = {
  "STR_SELECT_CHAPTER", "STR_FOOTNOTES", "STR_TEXT_SETTINGS", "STR_NIGHT_MODE", "STR_FRONTLIGHT",
  "STR_BT_PAGE_TURNER", "STR_GO_TO_PERCENT", "STR_AUTO_TURN_PAGES_PER_MIN", "STR_ORIENTATION",
  "STR_BOOKMARKS", "STR_SAVE_CLIPPING", "STR_VIEW_CLIPPINGS", "STR_TOGGLE_BOOKMARK",
  "STR_SCREENSHOT_BUTTON", "STR_DISPLAY_QR", "STR_GO_HOME_BUTTON", "STR_SYNC_PROGRESS",
  "STR_DELETE_CACHE", "STR_LOOKUP",
};
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) { leaf(); assert(false && "unexpected view-values leaf"); std::abort(); }
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
    leaf();
    if (key == static_cast<uint16_t>(StrId::STR_BT_CONNECTED_TO) && connectedFormatOverride)
      return wire(connectedFormatOverride);
    for (size_t i = 0; i < std::size(keys); ++i)
      if (key == static_cast<uint16_t>(keys[i])) return wire(tr(static_cast<FixtureStrId>(i)));
    for (size_t i = 0; i < std::size(actionLabels); ++i)
      if (key == static_cast<uint16_t>(actionLabels[i])) return wire(actionTexts[i]);
    assert(false && "unknown view-values translation"); std::abort();
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
  table.model_unlock = [] { assert(modelDepth == 1 && lockDepth == 1); --modelDepth; };
  return table;
}();
static const XpEpubLeaves leaves = [] {
  XpEpubLeaves table{};
  static_assert(sizeof(XpEpubLeaves) == 206 * sizeof(void (*)()));
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
  UNUSED_LEAF(input_snapshot);
  table.section_page_count = [](void* section) -> uint16_t {
    publication(section, 0); return active->nativeSection->count;
  };
  table.section_estimated_pages = [](void* section) -> int32_t {
    leaf(); assert(section && section == active->nativeSection);
    if (publishing) publication(section, 1);
    return active->nativeSection->estimatedTotalPages();
  };
  table.section_building = [](void* section) -> uint8_t {
    leaf(); assert(section && section == active->nativeSection);
    if (publishing) publication(section, 2);
    return active->nativeSection->isBuilding();
  };
  table.section_complete = [](void* section) -> uint8_t { publication(section, 3); return 0; };
  table.section_partial = [](void* section) -> uint8_t { publication(section, 4); return 0; };
  table.section_close = [](void* section) {
    leaf(); assert(!publishing && section && section == active->nativeSection);
    delete active->nativeSection; active->nativeSection = nullptr;
  };
  table.book_progress = [](void* book, int32_t spine, float fraction) -> float {
    leaf(); assert(book && book == active->epub.get());
    return active->epub->calculateProgress(spine, fraction);
  };
  table.book_toc_for_spine = [](void* book, int32_t spine) -> int32_t {
    leaf(); assert(book && book == active->epub.get());
    return active->epub->getTocIndexForSpineIndex(spine);
  };
  table.book_title = [](void* book, XpAppTextSink sink, void* context) -> uint8_t {
    leaf(); assert(book && book == active->epub.get());
    const auto title = active->epub->getTitle();
    return sink(context, wire(title.c_str()));
  };
  table.book_toc = [](void* book, int32_t index, XpEpubTocSink sink, void* context) -> uint8_t {
    leaf(); assert(book && book == active->epub.get());
    const auto item = active->epub->getTocItem(index);
    const XpEpubToc toc{wire(item.title.c_str()), wire(""), active->currentSpineIndex, 0};
    return sink(context, &toc);
  };
  table.render_geometry = [](XpEpubGeometry* out) {
    leaf(); assert(!publishing && out); *out = {};
    const auto& theme = UITheme::getInstance();
    out->status_height = theme.getStatusBarHeight();
    out->progress_height = theme.getProgressBarHeight();
    out->status_margin = theme.getMetrics().statusBarVerticalMargin;
    out->title_mode = SETTINGS.statusBarSpec().titleMode;
    assert(out->status_height == theme.status && out->progress_height == 4 && out->status_margin == 5);
    assert(out->title_mode == SETTINGS.spec.titleMode);
  };
  table.radio_status = [](XpEpubRadio* out, XpEpubPeer* peer) {
    leaf(); assert(!publishing && out); *out = {};
    out->note = static_cast<uint8_t>(bleturner::linkNote());
    assert(out->note == static_cast<uint8_t>(bleturner::note));
    if (peer) {
      assert(flagsReads == 1 && peerReads++ == 0);
      assert(out->note == 3 && SETTINGS.spec.titleMode != CrossPointSettings::HIDE_TITLE);
      const auto linked = bleturner::linked();
      *peer = {};
      std::snprintf(reinterpret_cast<char*>(peer->name), sizeof(peer->name), "%s", linked.name);
      std::snprintf(reinterpret_cast<char*>(peer->addr), sizeof(peer->addr), "%s", linked.addr);
      const size_t nameBytes = std::min(std::strlen(linked.name), sizeof(peer->name) - 1);
      const size_t addrBytes = std::min(std::strlen(linked.addr), sizeof(peer->addr) - 1);
      assert(std::memcmp(peer->name, linked.name, nameBytes) == 0 && peer->name[nameBytes] == 0);
      assert(std::memcmp(peer->addr, linked.addr, addrBytes) == 0 && peer->addr[addrBytes] == 0);
    } else {
      assert(flagsReads++ == 0 && peerReads == 0);
    }
  };
  table.settings_snapshot = [](uint8_t resolveFont, XpEpubSettings* out) {
    leaf(); assert(!publishing && !resolveFont && out); *out = {};
    out->font_size = SETTINGS.fontPointSize;
    out->line_spacing = SETTINGS.lineSpacing;
    out->alignment = SETTINGS.paragraphAlignment;
    out->orientation = SETTINGS.orientation;
    out->menu_style = SETTINGS.readerMenuStyle;
    out->font_family = wire(SETTINGS.sdFontFamilyName);
    assert(out->font_family.len == std::strlen(SETTINGS.sdFontFamilyName));
  };
  table.paint_status = [](const XpEpubStatus* value) {
    leaf(); assert(!publishing && value);
    paintedTitle = nativeText(value->title);
    GUI.drawStatusBar(active->renderer, value->book_progress, value->page, static_cast<float>(value->pages),
                      nativeText(value->title), 0, value->y_offset, true, value->bookmarked, value->building);
  };
  return table;
}();
#undef UNUSED_LEAF

struct Entry {
  EpubReaderActivity& fixture;
  explicit Entry(const EpubReaderActivity& value) : fixture(const_cast<EpubReaderActivity&>(value)) {
    assert(!active && modelDepth == 0 && lockDepth == 0);
    active = &fixture; lockDepth = 1;
    if (!fixture.rustOwner) {
      fixture.nativeSection = fixture.section.release();
      const XpEpubTestViewSeed seed{
        &fixture, fixture.epub.get(), fixture.nativeSection, fixture.currentSpineIndex,
        fixture.nativeSection ? fixture.nativeSection->currentPage : 0,
        static_cast<uint32_t>(fixture.pageTurnDuration), static_cast<uint8_t>(fixture.automaticPageTurnActive),
        static_cast<uint8_t>(fixture.currentPageBookmarked), &fixture,
        [](void* context, int32_t row) -> uint16_t {
          leaf(); assert(context == active && row >= 0 && static_cast<size_t>(row) < active->moreItems.size());
          assert(active->moreItems[row].labelId == STR_UNNAMED);
          return static_cast<uint16_t>(StrId::STR_UNNAMED);
        },
        [](void* context, uint8_t shown, uint8_t note) {
          leaf(); assert(context == active && active->rustOwner);
          XpEpubTestViewObservation observed{};
          xp_epub_test_view_observe(active->rustOwner, &observed);
          assert(observed.note_shown == shown && observed.note == (shown ? note : 0));
          assert(shown == (SETTINGS.spec.titleMode != CrossPointSettings::HIDE_TITLE));
          assert(note == static_cast<uint8_t>(bleturner::note));
          std::printf("note %d %d\n", shown, note);
        },
      };
      publishing = true; publicationStep = 0;
      fixture.rustOwner = xp_epub_test_view_create(&seed, &leaves, &runtime, &resources);
      assert(fixture.rustOwner && publicationStep == (fixture.nativeSection ? 5u : 0u));
      publishing = false;
    }
    uint8_t actions[19]{};
    assert(fixture.moreItems.size() <= std::size(actions));
    for (size_t i = 0; i < fixture.moreItems.size(); ++i) actions[i] = static_cast<uint8_t>(fixture.moreItems[i].action);
    xp_epub_test_view_seed_rows(fixture.rustOwner, actions, static_cast<uint8_t>(fixture.moreItems.size()),
                               static_cast<uint8_t>(fixture.autoTurnOption));
  }
  ~Entry() { assert(modelDepth == 0 && lockDepth == 1); lockDepth = 0; active = nullptr; }
};
static std::string value(const EpubReaderActivity& fixture, uint8_t operation, int32_t row = 0) {
  Entry entry(fixture);
  struct Output { std::string value; unsigned calls = 0; } output;
  assert(xp_epub_test_view_text(fixture.rustOwner, operation, row,
    [](void* raw, XpAppText value) -> uint8_t {
      leaf(); auto& output = *static_cast<Output*>(raw);
      assert(output.calls++ == 0); output.value = nativeText(value); return 1;
    }, &output));
  assert(output.calls == 1);
  return output.value;
}
static void realFacts() {
  EpubReaderActivity fixture;
  { Entry entry(fixture); xp_epub_test_view_real_labels(fixture.rustOwner); }
  for (uint8_t action = 0; action < std::size(actionLabels); ++action) {
    assert(xp_epub_test_view_action_label(action) == static_cast<uint16_t>(actionLabels[action]));
    fixture.moreItems = {{static_cast<EpubReaderMenuActivity::MenuAction>(action), STR_UNNAMED}};
    assert(fixture.moreRowName(0) == actionTexts[action]);
    assert(fixture.moreRowName(-1).empty() && fixture.moreRowName(1).empty());
  }
  SETTINGS.spec.titleMode = CrossPointSettings::BOOK_TITLE;
  bleturner::note = bleturner::LinkNote::Connected;
  std::memset(bleturner::peer.name, 'M', sizeof(bleturner::peer.name) - 1);
  bleturner::peer.name[sizeof(bleturner::peer.name) - 1] = 0;
  fixture.renderStatusBar();
  assert(paintedTitle == "Connected " + std::string(53, 'M') && "connected title must preserve the 63-byte limit");
  bleturner::peer.name[0] = 0;
  fixture.renderStatusBar();
  assert(paintedTitle == "Connected AA:BB" && "empty peer name must fall back to address");
  connectedFormatOverride = "Link %% %s / done";
  fixture.renderStatusBar();
  assert(paintedTitle == "Link % AA:BB / done");
  connectedFormatOverride = nullptr;
  std::puts("real view facts: 19 canonical action labels; connected 63-byte limit, address fallback, percent escape");
}
}  // namespace view_values_adapter

EpubReaderActivity::~EpubReaderActivity() {
  if (rustOwner) {
    view_values_adapter::Entry entry(*this);
    xp_epub_test_view_destroy(rustOwner);
    rustOwner = nullptr;
    assert(!nativeSection);
  }
}
void EpubReaderActivity::renderStatusBar() const {
  view_values_adapter::Entry entry(*this);
  view_values_adapter::flagsReads = view_values_adapter::peerReads = 0;
  xp_epub_test_view_status(rustOwner);
  assert(view_values_adapter::flagsReads == 1);
  assert(view_values_adapter::peerReads == static_cast<unsigned>(
      bleturner::note == bleturner::LinkNote::Connected && SETTINGS.spec.titleMode != CrossPointSettings::HIDE_TITLE));
}
bool EpubReaderActivity::usesToolbarMenu() const {
  view_values_adapter::Entry entry(*this);
  return xp_epub_test_view_toolbar(rustOwner);
}
std::string EpubReaderActivity::currentChapterTitle() const { return view_values_adapter::value(*this, 0); }
std::string EpubReaderActivity::textRowName(int row) const { return view_values_adapter::value(*this, 1, row); }
std::string EpubReaderActivity::textRowValue(int row) const { return view_values_adapter::value(*this, 2, row); }
std::string EpubReaderActivity::moreRowName(int row) const { return view_values_adapter::value(*this, 3, row); }
std::string EpubReaderActivity::moreRowValue(int row) const { return view_values_adapter::value(*this, 4, row); }
