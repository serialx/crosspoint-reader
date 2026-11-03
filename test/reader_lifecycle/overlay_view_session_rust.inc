namespace overlay_view_adapter {
static EpubReaderActivity* active;
static int modelDepth, lockDepth;
static bool publishing;
static unsigned publicationStep;
static const uint8_t* borrowedToc;
static size_t borrowedTocSize;
static unsigned tocDepth, borrowedTitles, borrowedRows, paints;
static void leaf() { assert(active && modelDepth == 0 && lockDepth == 1); }
static XpAppText wire(const std::string& value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
static XpAppText wire(const char* value) {
  return {reinterpret_cast<const uint8_t*>(value), std::strlen(value)};
}
static std::string nativeText(XpAppText value) {
  assert(value.data || value.len == 0);
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}
static void publication(void* section, unsigned step) {
  leaf(); assert(section && section == active->nativeSection && publishing && publicationStep == step);
  ++publicationStep;
}
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) { leaf(); assert(false && "unexpected overlay-view leaf"); std::abort(); }
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
    switch (static_cast<StrId>(key)) {
      case StrId::STR_TOOL_CONTENTS: return wire(tr(STR_TOOL_CONTENTS));
      case StrId::STR_TOOL_TEXT: return wire(tr(STR_TOOL_TEXT));
      case StrId::STR_TOOL_MORE: return wire(tr(STR_TOOL_MORE));
      case StrId::STR_UNNAMED: return wire("Unnamed");
      default: assert(false && "unexpected overlay translation"); std::abort();
    }
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
  table.model_unlock = [] { assert(active && modelDepth == 1 && lockDepth == 1); --modelDepth; };
  table.frontlight_state = []() -> uint8_t { leaf(); return Frontlight.isOn(); };
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
  table.physical.section_page_count = [](void* section) -> uint16_t {
    publication(section, 0); return static_cast<uint16_t>(active->nativeSection->count);
  };
  table.physical.section_estimated_pages = [](void* section) -> int32_t {
    leaf(); assert(section && section == active->nativeSection);
    if (publishing) publication(section, 1);
    return active->nativeSection->estimatedTotalPages();
  };
  table.physical.section_building = [](void* section) -> uint8_t { publication(section, 2); return 0; };
  table.physical.section_complete = [](void* section) -> uint8_t { publication(section, 3); return 0; };
  table.physical.section_partial = [](void* section) -> uint8_t { publication(section, 4); return 0; };
  table.physical.section_close = [](void* section) {
    leaf(); assert(!publishing && section && section == active->nativeSection);
    delete active->nativeSection; active->nativeSection = nullptr;
  };
  table.book.book_progress = [](void* book, int32_t spine, float fraction) -> float {
    leaf(); assert(!publishing && book && book == active->epub.get());
    return active->epub->calculateProgress(spine, fraction);
  };
  table.book.book_toc_count = [](void* book) -> int32_t {
    leaf(); assert(book && book == active->epub.get());
    return active->epub->getTocItemsCount();
  };
  table.book.book_toc_for_spine = [](void* book, int32_t spine) -> int32_t {
    leaf(); assert(active->realValues && book == active->epub.get() && spine == active->currentSpineIndex);
    return 0;
  };
  table.book.book_toc = [](void* book, int32_t index, XpEpubTocSink sink, void* context) -> uint8_t {
    leaf(); assert(book && book == active->epub.get() && !borrowedToc && tocDepth == 0);
    const auto item = active->epub->getTocItem(index);
    const XpEpubToc toc{wire(item.title), wire(""), active->currentSpineIndex, item.level};
    borrowedToc = toc.title.data; borrowedTocSize = toc.title.len; ++tocDepth;
    const auto result = sink(context, &toc);
    assert(tocDepth == 1 && borrowedToc == toc.title.data);
    --tocDepth; borrowedToc = nullptr; borrowedTocSize = 0;
    return result;
  };
  table.physical.input_facts = [](XpEpubInputFacts* out) {
    leaf(); assert(!publishing && out); *out = {};
    out->touch = active->mappedInput.hasTouch();
  };
  table.physical.settings_snapshot = [](uint8_t resolveFont, XpEpubSettings* out) {
    leaf(); assert(!publishing && !resolveFont && out); *out = {};
    out->focus_reading = SETTINGS.focusReadingEnabled;
    out->screen_inverted = SETTINGS.screenInverted;
    out->ble_enabled = SETTINGS.ble.enabled;
  };
  table.physical.toolbar_paint = [](void* toolbar, const XpEpubToolbarModel* model,
                           XpEpubRowSource rows, void* context) -> uint8_t {
    leaf(); assert(!publishing && model && toolbar && toolbar == active->toolbarUi.get());
    ++paints;
    if (active->realValues && active->overlay == EpubReaderActivity::Overlay::Toolbar) {
      assert(tocDepth == 1 && borrowedTocSize == 4096 && model->chapter.len == borrowedTocSize);
      assert(model->chapter.data == borrowedToc && "chapter must remain borrowed through paint");
      ++borrowedTitles;
    }
    const auto chapter = nativeText(model->chapter);
    const auto page = nativeText(model->page_info);
    const auto title = nativeText(model->panel_title);
    assert(page.size() + 1 <= 40);
    if (model->chapter.data) assert(model->chapter.data[model->chapter.len] == 0);
    if (model->page_info.data) assert(model->page_info.data[model->page_info.len] == 0);
    if (model->panel_title.data) assert(model->panel_title.data[model->panel_title.len] == 0);
    std::printf("model %d %d %d %d %d %d [%s] [%s] [%s]\n", model->panel, model->active_tool,
                model->dense, model->selected, model->item_count, model->progress_permille,
                chapter.c_str(), page.c_str(), title.c_str());
    for (int i = 0; i < model->item_count; ++i) {
      struct Output { std::string label, value; uint8_t toggle = 0, checked = 0; unsigned calls = 0; } output;
      assert(rows && rows(context, i, [](void* raw, const XpEpubToolbarRow* row) -> uint8_t {
        leaf(); assert(row); auto& output = *static_cast<Output*>(raw); assert(output.calls++ == 0);
        if (active->realValues && active->overlay == EpubReaderActivity::Overlay::Contents) {
          assert(tocDepth == 1 && borrowedTocSize == 4096 && row->label.len == borrowedTocSize);
          assert(row->label.data == borrowedToc && "TOC label must remain borrowed through row sink");
          assert(row->label_spaces == 6 && "level four row carries its six-space prefix separately");
          ++borrowedRows;
        }
        output.label.assign(row->label_spaces, ' ');
        output.label += nativeText(row->label);
        output.value = nativeText(row->value);
        output.toggle = row->toggle; output.checked = row->checked;
        return 1;
      }, &output));
      assert(output.calls == 1);
      std::printf("row %d [%s] [%s] %d %d\n", i, output.label.c_str(), output.value.c_str(),
                  output.toggle, output.checked);
    }
    return 1;
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

template <std::string (EpubReaderActivity::*Method)(int) const>
static uint8_t rowHook(void* raw, int32_t row, XpAppTextSink sink, void* context) {
  leaf(); assert(raw == active && !active->realValues);
  const auto value = (active->*Method)(row);
  return sink(context, wire(value));
}
struct Entry {
  EpubReaderActivity& fixture;
  explicit Entry(EpubReaderActivity& value) : fixture(value) {
    assert(!active && modelDepth == 0 && lockDepth == 0);
    active = &fixture; lockDepth = 1;
    if (!fixture.rustOwner) {
      fixture.nativeSection = fixture.section.release();
      XpEpubTestOverlayViewSeed seed{};
      seed.native = &fixture; seed.book = fixture.epub.get(); seed.section = fixture.nativeSection;
      seed.toolbar = fixture.toolbarUi.get(); seed.context = &fixture;
      seed.spine = fixture.currentSpineIndex;
      seed.page = fixture.nativeSection ? fixture.nativeSection->currentPage : 0;
      seed.focus = fixture.focusedTool; seed.index = fixture.panelIndex;
      seed.overlay = static_cast<uint8_t>(fixture.overlay); seed.cursor = fixture.panelCursorShown;
      seed.real_values = fixture.realValues;
      assert(fixture.moreItems.size() <= std::size(seed.more_actions));
      seed.more_count = static_cast<uint8_t>(fixture.moreItems.size());
      for (size_t i = 0; i < fixture.moreItems.size(); ++i)
        seed.more_actions[i] = static_cast<uint8_t>(fixture.moreItems[i].action);
      seed.chapter = [](void* raw, XpAppTextSink sink, void* context) -> uint8_t {
        leaf(); assert(raw == active && !active->realValues);
        const auto value = active->currentChapterTitle();
        return sink(context, wire(value));
      };
      seed.text_name = rowHook<&EpubReaderActivity::textRowName>;
      seed.text_value = rowHook<&EpubReaderActivity::textRowValue>;
      seed.more_name = rowHook<&EpubReaderActivity::moreRowName>;
      seed.more_value = rowHook<&EpubReaderActivity::moreRowValue>;
      publishing = true; publicationStep = 0;
      fixture.rustOwner = xp_epub_test_overlay_view_create_legacy_book(&seed,&leaves.physical, &runtime, &resources, &leaves.book);
      assert(fixture.rustOwner && publicationStep == (fixture.nativeSection ? 5u : 0u));
      publishing = false;
    }
  }
  ~Entry() { assert(modelDepth == 0 && lockDepth == 1 && tocDepth == 0); lockDepth = 0; active = nullptr; }
};
static void realBorrows() {
  const unsigned firstPaint = paints;
  {
    EpubReaderActivity fixture;
    fixture.realValues = true;
    fixture.overlay = EpubReaderActivity::Overlay::Toolbar;
    fixture.epub->items[0].title.assign(4096, 't');
    fixture.epub->fixedProgress = true;
    fixture.renderOverlay();
  }
  {
    EpubReaderActivity fixture;
    fixture.realValues = true;
    fixture.overlay = EpubReaderActivity::Overlay::Contents;
    fixture.epub->items = {{4, std::string(4096, 'r')}};
    fixture.renderOverlay();
  }
  assert(paints == firstPaint + 2 && borrowedTitles == 1 && borrowedRows == 1);
  assert(!borrowedToc && tocDepth == 0 && modelDepth == 0 && lockDepth == 0);
  std::puts("real overlay borrows: 4096-byte chapter and TOC row retain native pointer identity through sinks");
}
}  // namespace overlay_view_adapter

EpubReaderActivity::~EpubReaderActivity() {
  if (rustOwner) {
    overlay_view_adapter::Entry entry(*this);
    xp_epub_test_overlay_view_destroy(rustOwner);
    rustOwner = nullptr;
    assert(!nativeSection);
  }
}
void EpubReaderActivity::renderOverlay() {
  overlay_view_adapter::Entry entry(*this);
  xp_epub_test_overlay_view_render(rustOwner);
}
