namespace book_render_adapter {
static EpubReaderActivity* active;
static int modelDepth, lockDepth;
static bool publishing;
static unsigned publicationStep, publicationCount;
static Page* livePage;
static bool contentsCalled, metadataSucceeded;
static std::unique_ptr<GfxRenderer::FrameBufferLoan> loan;
static void leaf() { assert(active && modelDepth == 0 && lockDepth == 1); }
static XpAppText wire(const char* value) {
  return {reinterpret_cast<const uint8_t*>(value), std::strlen(value)};
}
static std::string nativeText(XpAppText value) {
  assert(value.data || !value.len);
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}
static void publicationStart() {
  leaf(); assert(!publishing); publishing = true; publicationStep = 0;
}
static void sectionRead(void* raw, unsigned step) {
  leaf(); assert(raw && raw == active->nativeSection);
  if (publishing) {
    assert(publicationStep++ == step);
    if (publicationStep == 5) { publishing = false; ++publicationCount; }
  }
}
static void stable() { leaf(); assert(!publishing); }
static Section& section(void* raw) {
  stable(); assert(raw && raw == active->nativeSection); return *active->nativeSection;
}
static ReaderRenderSpec renderSpec(const XpEpubRenderSpec& value) {
  stable(); return {value.width, value.height};
}
static const auto nativeStartBuild = /* ACTUAL_SECTION_START_BUILD */;
static XpEpubTestBookObservation observation() {
  leaf(); assert(active->rustOwner);
  XpEpubTestBookObservation out{};
  xp_epub_test_book_observe(active->rustOwner, &out);
  assert(out.section == active->nativeSection);
  if (active->nativeSection) {
    const auto& s = *active->nativeSection;
    assert(out.published_pages == s.pageCount && out.published_estimate == s.config.finalCount);
    assert(out.published_building == s.config.building && out.published_complete == s.config.complete);
    assert(out.published_partial == s.config.partial);
  } else { assert(out.published_pages == -1 && out.published_estimate == -1); }
  return out;
}
static constexpr StrId translationKeys[] = {
  StrId::STR_SAVE_PROGRESS_FAILED, StrId::STR_BACK, StrId::STR_INDEX_FAILED, StrId::STR_BT_BUILD_REFUSED,
  StrId::STR_INDEXING, StrId::STR_EMPTY_CHAPTER, StrId::STR_OUT_OF_BOUNDS, StrId::STR_PAGE_LOAD_ERROR,
  StrId::STR_BOOKMARK_REMOVED, StrId::STR_BOOKMARK_ADDED, StrId::STR_DICT_NO_DICT_SET, StrId::STR_FONT_DAMAGED,
};
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) { leaf(); assert(false && "unexpected book-render leaf"); std::abort(); }
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
    for (size_t i = 0; i < std::size(translationKeys); ++i)
      if (key == static_cast<uint16_t>(translationKeys[i])) return wire(tr(static_cast<int>(i)));
    if (key == static_cast<uint16_t>(StrId::STR_CLIPPING_SAVED)) return wire(I18N.get(0));
    assert(false && "unexpected book-render translation"); std::abort();
  };
  table.popup_draw = [](XpAppText value) -> XpUiRect {
    leaf(); GUI.drawPopup(active->renderer, nativeText(value).c_str()); return {};
  };
  table.footer_draw = [](const XpAppText values[4]) {
    leaf(); std::string labels[4];
    for (size_t i = 0; i < 4; ++i) labels[i] = nativeText(values[i]);
    const auto mapped = active->mappedInput.mapLabels(labels[0].c_str(), labels[1].c_str(), labels[2].c_str(), labels[3].c_str());
    GUI.drawButtonHints(active->renderer, mapped.btn1, mapped.btn2, mapped.btn3, mapped.btn4);
  };
  table.text_width = [](XpAppText value, int32_t font, uint8_t style) -> int32_t {
    leaf(); assert(value.len && font == UI_12_FONT_ID && style == 1); return 47;
  };
  table.text_draw = [](const XpUiCommand* command, XpUiRect clip, XpAppText value) {
    leaf(); assert(command && command->font == UI_12_FONT_ID && command->style == 1);
    assert(command->max_lines == 0 && command->rect.x == (active->renderer.getScreenWidth() - 47) / 2);
    assert(command->rect.y == 300 && command->paint != 1);
    assert(clip.x == 0 && clip.y == 0 && clip.w == 600 && clip.h == 800);
    active->renderer.drawCenteredText(command->font, command->rect.y, nativeText(value).c_str(), true, EpdFontFamily::BOLD);
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
  table.now_ms = []() -> uint32_t { leaf(); return millis(); };
  table.notify_render = [] { leaf(); active->requestUpdate(); };
  table.screenshot = [] { leaf(); ScreenshotUtil::takeScreenshot(active->renderer); };
  return table;
}();
static const XpUiReaderRenderServices renderServices = {
  nullptr,
  [](void*, uint8_t) -> uint32_t { leaf(); assert(false && "unexpected book-render physical query"); return 0; },
  [](void*, const XpUiReaderRenderOp* op) -> uint32_t {
    stable(); assert(op);
    switch (op->kind) {
      case XP_UI_READER_RENDER_CLEAR: active->renderer.clearScreen(); return 0;
      case XP_UI_READER_RENDER_DISPLAY: active->renderer.displayBuffer(); return 0;
      case XP_UI_READER_RENDER_STORE_BW:
        assert(!livePage); return active->renderer.storeBwBuffer();
      default: assert(false && "unexpected book-render operation"); return 0;
    }
  },
};
static const LegacyEpubLeaves leaves = [] {
  LegacyEpubLeaves table{};
  static_assert(sizeof(XpEpubLeaves) == 167 * sizeof(void (*)()));
  UNUSED_LEAF(physical.render_margins);
  UNUSED_LEAF(physical.render_geometry);
  UNUSED_LEAF(physical.render_restore_bw);
  UNUSED_LEAF(physical.render_discard_bw);
  UNUSED_LEAF(physical.render_orientation);
  UNUSED_LEAF(physical.loan_count);
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
  UNUSED_LEAF(book.section.page.page_close);
  UNUSED_LEAF(book.section.page.page_info);
  UNUSED_LEAF(book.section.page.page_element);
  UNUSED_LEAF(book.section.page.page_word);
  UNUSED_LEAF(book.section.page.metadata_assign);
  UNUSED_LEAF(book.section.page.metadata_clear);
  UNUSED_LEAF(book.section.page.metadata_footnotes);
  UNUSED_LEAF(book.section.page.metadata_links);
  UNUSED_LEAF(book.section.page.metadata_footnote);
  UNUSED_LEAF(book.section.page.metadata_link);
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
  UNUSED_LEAF(book.section.page.page_word_font);
  UNUSED_LEAF(book.section.page.page_line_font);
  UNUSED_LEAF(book.section.page.page_word_y_offset);
  UNUSED_LEAF(book.section.page.page_line_ruby_shift);
  UNUSED_LEAF(physical.font_text_advance);
  UNUSED_LEAF(physical.log_enabled);
  UNUSED_LEAF(physical.toolbar_reset_navigation);
  UNUSED_LEAF(physical.input_snapshot);
  UNUSED_LEAF(book.section.page.page_render);
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
  UNUSED_LEAF(book.section.page.page_load);
  UNUSED_LEAF(physical.framebuffer_lend);
  UNUSED_LEAF(physical.framebuffer_restore);
  UNUSED_LEAF(physical.paint_arena);
  UNUSED_LEAF(physical.image_needs_decode);
  UNUSED_LEAF(physical.font_ascent);
  UNUSED_LEAF(physical.render_physical_query);
  UNUSED_LEAF(physical.render_physical_operation);
  UNUSED_LEAF(physical.render_plan);
  UNUSED_LEAF(physical.image_read_window);
  UNUSED_LEAF(physical.display_source);
  UNUSED_LEAF(physical.framebuffer_view);
  table.book.section.page.render_services = [](void* native) -> const XpUiReaderRenderServices* {
    leaf(); assert(native == active); return &renderServices;
  };
  table.physical.render_margins = [](int32_t* margins) {
    stable(); active->renderer.getOrientedViewableTRBL(&margins[0], &margins[1], &margins[2], &margins[3]);
  };
  table.physical.render_geometry = [](XpEpubGeometry* out) {
    leaf(); assert(out); *out = {};
    const auto& theme = UITheme::getInstance();
    out->width = active->renderer.getScreenWidth(); out->height = active->renderer.getScreenHeight();
    out->status_height = theme.getStatusBarHeight(); out->progress_height = theme.getProgressBarHeight();
    out->status_margin = theme.getMetrics().statusBarVerticalMargin;
    out->ui_font_12 = UI_12_FONT_ID;
    out->has_framebuffer = active->renderer.hasFrameBuffer();
    // Native getTtfFonts returns a const map reference. Copy the same cheap fact.
    out->has_ttf = active->renderer.ttf;
  };
  table.physical.render_discard_bw = [] { leaf(); assert(active->overlayPageStored); };
  table.physical.framebuffer_lend = []() -> uint8_t {
    stable(); assert(!loan);
    loan = std::make_unique<GfxRenderer::FrameBufferLoan>(active->renderer); return 1;
  };
  table.physical.framebuffer_restore = [](uint8_t owned) {
    leaf(); assert(owned && loan); loan->end(); loan.reset();
  };
  table.book.book_spine_count = [](void* book) -> int32_t {
    stable(); assert(book && book == active->epub.get()); return active->epub->getSpineItemsCount();
  };
  table.book.book_spine = [](void* book, int32_t index, XpEpubSpineSink sink, void* context) -> uint8_t {
    stable(); assert(book && book == active->epub.get());
    const auto item = active->epub->getSpineItem(index);
    const XpEpubSpine view{wire(item.href.c_str()), 0};
    return sink(context, &view);
  };
  table.book.book_cumulative_size = [](void* book, int32_t index) -> uint32_t {
    stable(); assert(book && book == active->epub.get()); return active->epub->getCumulativeSpineItemSize(index);
  };
  table.book.section.section_open = [](void* book, int32_t spine) -> void* {
    stable(); assert(book && book == active->epub.get() && !active->nativeSection);
    active->nativeSection = new Section(active->epub, spine, active->renderer);
    publicationStart(); return active->nativeSection;
  };
  table.book.section.section_close = [](void* section) {
    stable(); assert(section && section == active->nativeSection);
    // The owner must withdraw published facts before releasing native storage.
    if (active->rustOwner) {
      XpEpubTestBookObservation out{}; xp_epub_test_book_observe(active->rustOwner, &out);
      assert(!out.section && out.published_pages == -1);
    }
    delete active->nativeSection; active->nativeSection = nullptr;
  };
  table.book.section.section_page_count = [](void* s) -> uint16_t { sectionRead(s, 0); return active->nativeSection->pageCount; };
  table.book.section.section_estimated_pages = [](void* s) -> int32_t { sectionRead(s, 1); return active->nativeSection->estimatedTotalPages(); };
  table.book.section.section_building = [](void* s) -> uint8_t { sectionRead(s, 2); return active->nativeSection->isBuilding(); };
  table.book.section.section_complete = [](void* s) -> uint8_t { sectionRead(s, 3); return active->nativeSection->isBuildComplete(); };
  table.book.section.section_partial = [](void* s) -> uint8_t { sectionRead(s, 4); return active->nativeSection->isPartial(); };
  table.book.section.section_load_cache = [](void* s, const XpEpubRenderSpec* spec) -> uint8_t {
    stable(); assert(s == active->nativeSection && spec);
    const bool result = active->nativeSection->loadSectionFile({spec->width, spec->height});
    publicationStart(); return result;
  };
  table.book.section.section_create_cache = [](void* s, const XpEpubRenderSpec* spec, XpEpubBuildPopup callback, void* context) -> uint8_t {
    stable(); assert(s == active->nativeSection && spec && callback && context == active->rustOwner);
    const bool result = active->nativeSection->createSectionFile({spec->width, spec->height}, [=] { leaf(); callback(context); });
    publicationStart(); return result;
  };
  table.book.section.section_start_build = [](void* s, const XpEpubRenderSpec* spec, XpEpubBuildPopup callback, void* context) -> uint8_t {
    stable(); assert(s == active->nativeSection && spec);
    if (callback) assert(context == active->rustOwner);
    else assert(!context);
    const bool result = nativeStartBuild(s, spec, callback, context);
    publicationStart(); return result;
  };
  table.book.section.section_build_more = [](void* s, int32_t pages, uint8_t yielding) -> uint8_t {
    stable(); assert(s == active->nativeSection && !yielding);
    const bool result = active->nativeSection->buildSomeMore(pages);
    publicationStart(); return result;
  };
  table.book.section.section_abandon_build = [](void* s) { stable(); assert(s == active->nativeSection); active->nativeSection->abandonBuild(); };
  table.book.section.section_clear_cache = [](void* s) -> uint8_t { stable(); assert(s == active->nativeSection); active->nativeSection->clearCache(); return 1; };
  table.book.section.section_html_cached = [](void* s) -> uint8_t { stable(); assert(s == active->nativeSection); return active->nativeSection->hasHtmlCache(); };
  table.book.section.section_find_anchor = [](void* s, XpAppText value, uint16_t* page) -> uint8_t {
    stable(); assert(s == active->nativeSection);
    const auto found = active->nativeSection->findAnchor(nativeText(value)); if (found) *page = *found; return bool(found);
  };
  table.book.section.section_cached_anchor = [](void* s, XpAppText value, uint16_t* page) -> uint8_t {
    stable(); assert(s == active->nativeSection);
    const auto found = active->nativeSection->getPageForAnchor(nativeText(value)); if (found) *page = *found; return bool(found);
  };
  table.book.section.section_reached_offset = [](void* s, uint32_t offset) -> uint8_t {
    stable(); assert(s == active->nativeSection); return active->nativeSection->buildReachedVisibleTextOffset(offset);
  };
  table.book.section.section_page_for_offset = [](void* s, uint32_t offset, uint8_t first, uint16_t* page) -> uint8_t {
    stable(); assert(s == active->nativeSection && !first);
    const auto found = active->nativeSection->getPageForVisibleTextOffset(offset); if (found) *page = *found; return bool(found);
  };
  table.book.section.section_load_page = [](void* s, int32_t page) -> void* {
    stable(); assert(s == active->nativeSection && !livePage); observation();
    livePage = active->nativeSection->loadPage(page).release(); contentsCalled = metadataSucceeded = false; return livePage;
  };
  table.book.section.page.page_close = [](void* page) {
    stable(); assert(page && page == livePage);
    if (metadataSucceeded) assert(contentsCalled);
    else assert(!active->nativeSection);
    delete livePage; livePage = nullptr;
  };
  table.book.section.page.page_info = [](void* page, XpEpubPageInfo* out) {
    stable(); assert(page && page == livePage && out); *out = {}; out->visible_offset = livePage->visibleTextOffset;
  };
  table.book.section.page.metadata_assign = [](void* native, void* page) -> uint8_t {
    stable(); assert(native == active && page && page == livePage);
    metadataSucceeded = active->currentPageMetadata.assign(*livePage); return metadataSucceeded;
  };
  table.book.section.page.metadata_clear = [](void* native) { stable(); assert(native == active); active->currentPageMetadata.clear(); };
  table.physical.fonts_release_caches = [] {
    stable(); if (auto* cache = active->renderer.getFontCacheManager()) cache->releaseSdFontCaches();
  };
  table.physical.fonts_take_corrupt_notice = []() -> uint8_t { stable(); return SdCardFont::takeCorruptGlyphNotice(); };
  table.physical.settings_snapshot = [](uint8_t, XpEpubSettings* out) {
    leaf(); assert(out); *out = {}; out->screen_margin = SETTINGS.screenMargin;
  };
  table.physical.settings_render_spec = [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
    stable(); assert(out); const auto spec = SETTINGS.readerRenderSpec(width, height);
    *out = {}; out->width = spec.width; out->height = spec.height;
  };
  table.physical.heap_snapshot = [](XpEpubHeap* out) {
    stable();
#ifdef BOOK_RELEASE_LOG
    assert(false && "release logging must not sample heap");
#endif
    out->free_bytes = ESP.getFreeHeap(); out->largest_block = ESP.getMaxAllocHeap();
  };
  table.physical.radio_settle_start = [] { stable(); bleturner::settleStart(); };
  table.physical.popup_active = [](void* popup) -> uint8_t {
    stable(); assert(popup == &active->overlayPopup); return active->overlayPopup.isActive();
  };
  table.physical.popup_paint = [](void* popup) { stable(); assert(popup == &active->overlayPopup); active->overlayPopup.render(active->renderer); };
  table.physical.log_enabled = [](uint8_t severity) -> uint8_t {
    leaf();
#ifdef BOOK_RELEASE_LOG
    return severity != 0;
#else
    return 1;
#endif
  };
  table.physical.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    leaf();
#ifdef BOOK_RELEASE_LOG
    if (severity == 0) return;
#endif
    const auto tag = nativeText(origin), text = nativeText(message);
    if (text.find("Rendered page in") == 0) assert(!livePage && contentsCalled);
    std::printf("%s %s\n", tag.c_str(), text.c_str());
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
static XpEpubTestBookValues seedValues(const EpubReaderActivity& f) {
  XpEpubTestBookValues v{};
  v.spine = f.currentSpineIndex; v.page = f.nativeSection ? f.nativeSection->currentPage : 0;
  v.next_page = f.nextPageNumber; v.cached_spine = f.cachedSpineIndex; v.cached_count = f.cachedChapterTotalPageCount;
  v.last_spine = f.lastSavedSpineIndex; v.last_page = f.lastSavedPage; v.last_count = f.lastSavedPageCount;
  v.pending_page = f.pendingPageJump.value_or(0); v.has_pending_page = bool(f.pendingPageJump);
  v.pending_offset = f.pendingOffsetJump.value_or(0); v.has_pending_offset = bool(f.pendingOffsetJump);
  v.cached_offset = f.cachedVisibleTextOffset.value_or(0); v.has_cached_offset = bool(f.cachedVisibleTextOffset);
  v.visible_offset = f.currentPageVisibleOffset.value_or(0); v.has_visible_offset = bool(f.currentPageVisibleOffset);
  v.partial_failed = f.partialRebuildStartFailed; v.percent = f.pendingPercentJump; v.popup = f.buildPopupPending;
  v.stale = f.pageBufferStale; v.automatic = f.automaticPageTurnActive; v.sync_error = f.pendingSyncSaveError;
  v.screenshot = f.pendingScreenshot; v.bookmark_notice = f.showBookmarkMessage; v.bookmark_removed = f.bookmarkRemoved;
  v.dictionary_notice = f.showDictionaryMessage; v.font_notice = f.showFontDamagedMessage;
  v.clipping_notice = f.showClippingMessage; v.stored = f.overlayPageStored; v.overlay = static_cast<int>(f.overlay);
  v.retry = f.pageLoadRetryCount; v.refresh = f.pagesUntilFullRefresh;
  v.width = f.buildViewportWidth; v.height = f.buildViewportHeight;
  v.left = f.currentPageLinkMarginLeft; v.top = f.currentPageLinkMarginTop;
  v.last_render = f.lastRenderCompleteMs; v.font_since = f.fontDamagedMessageTime;
  v.spine_progress = f.pendingSpineProgress;
  return v;
}
static XpEpubTestBookHooks hooks() {
  XpEpubTestBookHooks h{}; h.context = active;
  h.settle = [](void* context) { stable(); assert(context == active); active->settleOverlayRefresh(); };
  h.room = [](void* context) -> uint8_t { stable(); assert(context == active); observation(); return active->makeRoomForChapterBuild(); };
  h.deferred = [](void* context) -> uint8_t { stable(); assert(context == active); observation(); return active->applyDeferredReposition(); };
  h.status = [](void* context) { stable(); assert(context == active); active->renderStatusBar(); };
  h.bookmark = [](void* context) { stable(); assert(context == active); active->updateBookmarkFlag(); };
  h.contents = [](void* context, void* page, const int32_t* margins) {
    stable(); assert(context == active && page && page == livePage && metadataSucceeded && !contentsCalled);
    contentsCalled = true;
    const auto observed = observation();
    assert(observed.values.visible_offset == livePage->visibleTextOffset && observed.values.has_visible_offset);
    assert(observed.values.left == margins[3] && observed.values.top == margins[0]);
    std::printf("contents %d %d %d %d\n", margins[0], margins[1], margins[2], margins[3]);
  };
  h.save = [](void* context, int32_t spine, int32_t page, int32_t count) -> uint8_t {
    stable(); assert(context == active && !livePage); return active->saveProgress(spine, page, count);
  };
  h.toolbar = [](void* context) -> uint8_t { stable(); assert(context == active); return active->usesToolbarMenu(); };
  h.overlay = [](void* context) { stable(); assert(context == active && !livePage); active->renderOverlay(); };
  h.push = [](void* context) { stable(); assert(context == active); active->pushOverlayRefresh(); };
  // Observers follow the actual mutations; they never supply their results.
  h.clear = [](void* context) {
    stable(); assert(context == active); const auto o = observation();
    assert(o.values.cached_count == 0 && !o.values.has_cached_offset); event("clear-deferred");
  };
  h.discard = [](void* context) {
    stable(); assert(context == active); assert(!observation().values.stored); event("discard-overlay");
  };
  h.mark = [](void* context) {
    stable(); assert(context == active && !livePage); assert(observation().values.rendered); event("mark-rendered");
  };
  return h;
}
struct Entry {
  EpubReaderActivity& fixture;
  explicit Entry(EpubReaderActivity& value) : fixture(value) {
    assert(!active && modelDepth == 0 && lockDepth == 0 && !publishing && !livePage && !loan);
    active = &fixture; lockDepth = 1;
    if (!fixture.rustOwner) {
      fixture.nativeSection = fixture.section.release();
      const XpEpubTestBookSeed seed{&fixture, fixture.epub.get(), fixture.nativeSection, &fixture.overlayPopup,
                                    wire(fixture.pendingAnchor.c_str()), seedValues(fixture), hooks(),
                                    [](void* context) {
                                      stable(); assert(context == active); active->requestUpdate();
                                    }};
      publicationCount = 0;
      if (fixture.nativeSection) publicationStart();
      fixture.rustOwner = xp_epub_test_book_create_legacy_book(&seed, &leaves.physical, &runtime, &resources, &leaves.book);
      assert(fixture.rustOwner && !publishing && publicationCount == (fixture.nativeSection ? 1u : 0u));
      observation();
    }
  }
  ~Entry() {
    assert(modelDepth == 0 && lockDepth == 1 && !publishing && !livePage && !loan);
    lockDepth = 0; active = nullptr;
  }
};
}  // namespace book_render_adapter

EpubReaderActivity::~EpubReaderActivity() {
  if (rustOwner) {
    book_render_adapter::Entry entry(*this);
    xp_epub_test_book_destroy(rustOwner); rustOwner = nullptr;
    assert(!nativeSection);
  }
}
void EpubReaderActivity::renderBook() {
  book_render_adapter::Entry entry(*this);
  xp_epub_test_book_render(rustOwner);
  book_render_adapter::observation();
}
void EpubReaderActivity::observeOwner() {
  book_render_adapter::Entry entry(*this);
  const auto observed = book_render_adapter::observation();
  const auto& v = observed.values;
  currentSpineIndex = v.spine;
  // Only the dump mirror uses native currentPage; production state lives in Rust.
  if (nativeSection) nativeSection->currentPage = v.page;
  nextPageNumber = v.next_page; cachedSpineIndex = v.cached_spine; cachedChapterTotalPageCount = v.cached_count;
  lastSavedSpineIndex = v.last_spine; lastSavedPage = v.last_page; lastSavedPageCount = v.last_count;
  pendingPageJump = v.has_pending_page ? std::optional<uint16_t>(v.pending_page) : std::nullopt;
  pendingOffsetJump = v.has_pending_offset ? std::optional<uint32_t>(v.pending_offset) : std::nullopt;
  cachedVisibleTextOffset = v.has_cached_offset ? std::optional<uint32_t>(v.cached_offset) : std::nullopt;
  currentPageVisibleOffset = v.has_visible_offset ? std::optional<uint32_t>(v.visible_offset) : std::nullopt;
  partialRebuildStartFailed = v.partial_failed; pendingPercentJump = v.percent; buildPopupPending = v.popup;
  pageBufferStale = v.stale; automaticPageTurnActive = v.automatic; pendingSyncSaveError = v.sync_error;
  pendingScreenshot = v.screenshot; showBookmarkMessage = v.bookmark_notice; bookmarkRemoved = v.bookmark_removed;
  showDictionaryMessage = v.dictionary_notice; showFontDamagedMessage = v.font_notice; showClippingMessage = v.clipping_notice;
  overlayPageStored = v.stored; overlay = static_cast<Overlay>(v.overlay);
  pageLoadRetryCount = v.retry; pagesUntilFullRefresh = v.refresh;
  buildViewportWidth = v.width; buildViewportHeight = v.height;
  currentPageLinkMarginLeft = v.left; currentPageLinkMarginTop = v.top;
  lastRenderCompleteMs = v.last_render; fontDamagedMessageTime = v.font_since;
  pendingSpineProgress = v.spine_progress;
  pendingAnchor = book_render_adapter::nativeText(observed.anchor);
}
