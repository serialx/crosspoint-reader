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
    publication(section, 0); return static_cast<uint16_t>(active->nativeSection->count);
  };
  table.section_estimated_pages = [](void* section) -> int32_t {
    leaf(); assert(section && section == active->nativeSection);
    if (publishing) publication(section, 1);
    return active->nativeSection->estimatedTotalPages();
  };
  table.section_building = [](void* section) -> uint8_t { publication(section, 2); return 0; };
  table.section_complete = [](void* section) -> uint8_t { publication(section, 3); return 0; };
  table.section_partial = [](void* section) -> uint8_t { publication(section, 4); return 0; };
  table.section_close = [](void* section) {
    leaf(); assert(!publishing && section && section == active->nativeSection);
    delete active->nativeSection; active->nativeSection = nullptr;
  };
  table.book_progress = [](void* book, int32_t spine, float fraction) -> float {
    leaf(); assert(!publishing && book && book == active->epub.get());
    return active->epub->calculateProgress(spine, fraction);
  };
  table.book_toc_count = [](void* book) -> int32_t {
    leaf(); assert(book && book == active->epub.get());
    return active->epub->getTocItemsCount();
  };
  table.book_toc_for_spine = [](void* book, int32_t spine) -> int32_t {
    leaf(); assert(active->realValues && book == active->epub.get() && spine == active->currentSpineIndex);
    return 0;
  };
  table.book_toc = [](void* book, int32_t index, XpEpubTocSink sink, void* context) -> uint8_t {
    leaf(); assert(book && book == active->epub.get() && !borrowedToc && tocDepth == 0);
    const auto item = active->epub->getTocItem(index);
    const XpEpubToc toc{wire(item.title), wire(""), active->currentSpineIndex, item.level};
    borrowedToc = toc.title.data; borrowedTocSize = toc.title.len; ++tocDepth;
    const auto result = sink(context, &toc);
    assert(tocDepth == 1 && borrowedToc == toc.title.data);
    --tocDepth; borrowedToc = nullptr; borrowedTocSize = 0;
    return result;
  };
  table.input_facts = [](XpEpubInputFacts* out) {
    leaf(); assert(!publishing && out); *out = {};
    out->touch = active->mappedInput.hasTouch();
  };
  table.settings_snapshot = [](uint8_t resolveFont, XpEpubSettings* out) {
    leaf(); assert(!publishing && !resolveFont && out); *out = {};
    out->focus_reading = SETTINGS.focusReadingEnabled;
    out->screen_inverted = SETTINGS.screenInverted;
    out->ble_enabled = SETTINGS.ble.enabled;
  };
  table.toolbar_paint = [](void* toolbar, const XpEpubToolbarModel* model,
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
      fixture.rustOwner = xp_epub_test_overlay_view_create(&seed, &leaves, &runtime, &resources);
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
