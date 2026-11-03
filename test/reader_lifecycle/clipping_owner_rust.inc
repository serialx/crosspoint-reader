#include <cstdlib>

namespace clipping_owner_adapter {
static EpubReaderActivity* active;
static const Page* activePage;
static Section* liveSection;
static unsigned resourceDepth, publicationStep, countReads, specReads;
static bool publishing, realMode, a4Read;
static ReaderRenderSpec realSpec;
struct Access {
  Access() { assert(!resourceDepth && !modelDepth); ++resourceDepth; }
  ~Access() { assert(resourceDepth == 1 && !modelDepth); --resourceDepth; }
};
static void leaf() { assert(active && resourceDepth == 1 && modelDepth == 0); }
static void context(void* value) { leaf(); assert(value == active); }
static void publication(void* section, unsigned step) {
  leaf(); assert(publishing && section == liveSection && section && publicationStep == step); ++publicationStep;
}
static const Page& page(void* handle) { leaf(); assert(handle && handle == activePage); return *activePage; }
static const PageElementView& line(void* handle, uint16_t element) {
  const auto& value = page(handle); assert(element < value.lines.size()); return value.lines[element];
}
static TextBlock block(void* handle, uint16_t element) {
  const auto& value = line(handle, element); assert(value.tag == TAG_PageLine && value.block.validValue);
  return TextBlock{value.block.words, value.block.validValue, value.block.font};
}
static const Word& word(void* handle, uint16_t element, uint16_t index) {
  const auto& value = line(handle, element); assert(value.tag == TAG_PageLine && value.block.validValue);
  assert(index < value.block.words->size()); return (*value.block.words)[index];
}
static XpAppText wire(const char* text) { return {reinterpret_cast<const uint8_t*>(text), std::strlen(text)}; }
template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) { leaf(); assert(false && "unexpected clipping owner leaf"); std::abort(); }
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
  table.line_height = [](int32_t font) -> int32_t { leaf(); return active->renderer.getLineHeight(font); };
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
  table.now_ms = []() -> uint32_t { leaf(); return millis(); };
  return table;
}();

static XpUiReaderRenderServices renderServices{
  nullptr,
  [](void* value, uint8_t query) -> uint32_t {
    context(value);
    if (query == XP_UI_READER_RENDER_IS_A4) {
      a4Read = true; return BoardConfig::isEegoA4();
    }
    assert(query == XP_UI_READER_RENDER_IS_BW && a4Read && BoardConfig::a4);
    a4Read = false;
    return active->renderer.getRenderMode() == GfxRenderer::BW;
  },
  &Unexpected<uint32_t (*)(void*, const XpUiReaderRenderOp*)>::call,
};

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
  table.section_page_count = [](void* section) -> uint16_t { publication(section, 0); return liveSection->pageCount; };
  table.section_estimated_pages = [](void* section) -> int32_t { publication(section, 1); return liveSection->pageCount; };
  table.section_building = [](void* section) -> uint8_t { publication(section, 2); return 0; };
  table.section_complete = [](void* section) -> uint8_t { publication(section, 3); return 1; };
  table.section_partial = [](void* section) -> uint8_t { publication(section, 4); return 0; };
  table.section_close = [](void* section) {
    leaf(); assert(section == liveSection && section && !activePage && !publishing);
    delete liveSection; liveSection = nullptr;
  };
  table.render_margins = [](int32_t* margins) {
    leaf(); assert(margins); active->renderer.getOrientedViewableTRBL(&margins[0], &margins[1], &margins[2], &margins[3]);
  };
  table.settings_snapshot = [](uint8_t resolveFont, XpEpubSettings* out) {
    leaf(); assert(out && resolveFont <= 1); *out = {};
    out->font = resolveFont ? SETTINGS.getReaderFontId() : 0;
    out->screen_margin = SETTINGS.screenMargin;
  };
  table.settings_render_spec = [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
    leaf(); assert(realMode && out && width == active->buildViewportWidth && height == active->buildViewportHeight);
    ++specReads;
    *out = {realSpec.fontId, realSpec.lineCompression, width, height,
      realSpec.extraParagraphSpacing, realSpec.paragraphIndentSpaces, realSpec.characterSpacing,
      realSpec.wordSpacingPercent, realSpec.paragraphAlignment, realSpec.hyphenationEnabled,
      realSpec.embeddedStyle, realSpec.imageRendering, realSpec.focusReadingEnabled};
  };
  table.page_info = [](void* handle, XpEpubPageInfo* out) {
    const auto& value = page(handle); assert(out); *out = {}; out->elements = value.elementCount();
  };
  table.page_element = [](void* handle, uint16_t index, XpEpubElement* out) {
    const auto& value = page(handle); assert(out && index < value.lines.size());
    const auto element = value.element(index);
    *out = {}; out->x = element.xPos; out->y = element.yPos; out->tag = element.getTag();
    if (out->tag == TAG_PageLine) {
      const auto text = element.textBlock(); out->valid = text.valid(); if (out->valid) out->words = text.wordCount();
    }
  };
  table.page_word = [](void* handle, uint16_t element, uint16_t index, XpEpubWord* out) {
    const auto& value = word(handle, element, index); assert(out);
    // Reconstructed physical views borrow the Page; only standalone element
    // enumeration emits the historical element event.
    *out = {wire(value.text), value.x, value.start, value.end, static_cast<uint8_t>(value.style), 1};
  };
  table.page_word_font = [](void* handle, uint16_t element, uint16_t index, int32_t font) -> int32_t {
    assert(clippingText::hasVisibleText(word(handle, element, index).text));
    const auto text = block(handle, element);
    return TextBlock::renderFontId(active->renderer, text.wordFontId(index, font), font);
  };
  table.page_line_font = [](void* handle, uint16_t element, int32_t font) -> int32_t {
    const auto text = block(handle, element);
    return TextBlock::renderFontId(active->renderer, text.blockFontId(font), font);
  };
  table.page_word_y_offset = [](void* handle, uint16_t element, uint16_t index, int32_t font) -> int32_t {
    const auto text = block(handle, element); return text.wordYOffset(active->renderer, font, index);
  };
  table.page_line_ruby_shift = [](void* handle, uint16_t element, int32_t font) -> int32_t {
    const auto text = block(handle, element); return text.getRubyShift(active->renderer.getFontAscenderSize(font));
  };
  table.font_text_advance = [](XpAppText value, int32_t font, uint8_t style, int8_t spacing) -> int32_t {
    leaf(); assert(value.data && value.data[value.len] == 0);
    return active->renderer.getTextAdvanceX(font, reinterpret_cast<const char*>(value.data),
      static_cast<EpdFontFamily::Style>(style), spacing);
  };
  table.clippings_count = []() -> uint32_t { leaf(); ++countReads; return CLIPPINGS.clippingCount(); };
  table.clippings_row = [](uint32_t index, XpEpubClip* out) {
    leaf(); assert(out && index < CLIPPINGS.entries.size()); const auto& value = *CLIPPINGS.clippingAt(index);
    *out = {value.spineIndex, value.startPage, value.endPage, value.pageCount,
      value.startWordIndex, value.endWordIndex, value.layoutSignature, value.startOffset, value.endOffset};
  };
  table.render_services = [](void* root) -> const XpUiReaderRenderServices* {
    context(root); renderServices.context = root; return &renderServices;
  };
  table.paint_pixel = [](int32_t x, int32_t y) { leaf(); a4Read = false; active->renderer.drawPixel(x, y, false); };
  table.paint_dither = [](int32_t x, int32_t y, int32_t width, int32_t height) {
    leaf(); a4Read = false; active->renderer.fillRectDither(x, y, width, height, Color::LightGray);
  };
  table.log_enabled = [](uint8_t severity) -> uint8_t {
    leaf(); assert(severity == 0);
#ifdef CLIPPING_RELEASE_LOG
    return 0;
#else
    return 1;
#endif
  };
  table.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    leaf(); assert(severity == 0 && origin.len == 4 && std::memcmp(origin.data, "CLIP", 4) == 0);
    LOG_DBG("CLIP", "%.*s", int(message.len), reinterpret_cast<const char*>(message.data));
  };
  return table;
}();
#undef UNUSED_LEAF
}  // namespace clipping_owner_adapter

void EpubReaderActivity::initializeOwner(bool realFacts) {
  using namespace clipping_owner_adapter;
  assert(!active && !owner && !activePage && !resourceDepth && !liveSection);
  active = this; realMode = realFacts; liveSection = section.release();
  assert(buildViewportWidth >= 0 && buildViewportWidth <= 65535 && buildViewportHeight >= 0 && buildViewportHeight <= 65535);
  XpEpubTestClippingSeed seed{};
  seed.native = this; seed.section = liveSection; seed.spine = currentSpineIndex;
  seed.page = liveSection ? liveSection->currentPage : 0;
  seed.width = buildViewportWidth; seed.height = buildViewportHeight; seed.historical = !realFacts;
  seed.context = this;
  seed.signature = [](void* value, uint16_t width, uint16_t height) -> uint32_t {
    context(value); assert(!realMode);
    return readerRenderSpecSignature(SETTINGS.readerRenderSpec(width, height));
  };
  seed.nonempty = [](void* value, uint8_t actual) {
    context(value); assert(!realMode && actual == uint8_t(!CLIPPINGS.entries.empty()));
    assert(actual == uint8_t(CLIPPINGS.hasClippings()));
  };
  Access access;
  publishing = true; publicationStep = 0;
  owner = xp_epub_test_clipping_create(&seed, &leaves, &runtime, &resources);
  assert(owner && publicationStep == (liveSection ? 5u : 0u)); publishing = false;
}
EpubReaderActivity::~EpubReaderActivity() {
  using namespace clipping_owner_adapter;
  if (!owner) return;
  assert(active == this && !activePage); Access access;
  xp_epub_test_clipping_destroy(owner); owner = nullptr;
  assert(!liveSection && !modelDepth); active = nullptr;
}
int EpubReaderActivity::clippingAtPoint(const Page& value, int x, int y) const {
  using namespace clipping_owner_adapter;
  assert(active == this && owner && !activePage); Access access;
  activePage = &value;
  const int result = xp_epub_test_clipping_hit(owner, const_cast<Page*>(&value), x, y);
  activePage = nullptr; return result;
}
void EpubReaderActivity::drawClippingHighlights(const Page& value, int font, int top, int left) const {
  using namespace clipping_owner_adapter;
  assert(active == this && owner && !activePage); Access access;
  activePage = &value;
  xp_epub_test_clipping_draw(owner, const_cast<Page*>(&value), font, top, left);
  activePage = nullptr;
}

namespace clipping_owner_adapter {
static int realFacts() {
  unsigned signatureCases = 0, countCases = 0;
  for (unsigned variant = 0; variant < 17; ++variant) {
    EpubReaderActivity fixture;
    realSpec = ReaderRenderSpec{};
    realSpec.fontId = 6; realSpec.viewportWidth = 80; realSpec.viewportHeight = 100;
    switch (variant) {
      case 1: realSpec.fontId = -7; break;
      case 2: realSpec.lineCompression = 0.85f; break;
      case 3: realSpec.extraParagraphSpacing = true; break;
      case 4: realSpec.characterSpacing = -3; break;
      case 5: realSpec.wordSpacingPercent = 117; break;
      case 6: realSpec.paragraphAlignment = 3; break;
      case 7: realSpec.viewportWidth = 567; break;
      case 8: realSpec.viewportHeight = 987; break;
      case 9: realSpec.hyphenationEnabled = true; break;
      case 10: realSpec.embeddedStyle = false; break;
      case 11: realSpec.imageRendering = 2; break;
      case 12: realSpec.focusReadingEnabled = true; break;
      case 13: realSpec.paragraphIndentSpaces = 9; break;
      case 14: realSpec.lineCompression = 1.999f; break;
      case 15: realSpec.viewportWidth = 65535; realSpec.viewportHeight = 0; break;
      case 16: realSpec.characterSpacing = 127; realSpec.fontId = 42; break;
    }
    fixture.buildViewportWidth = realSpec.viewportWidth; fixture.buildViewportHeight = realSpec.viewportHeight;
    fixture.initializeOwner(true);
    {
      Access access; specReads = 0;
      const uint32_t expected = readerRenderSpecSignature(realSpec);
      const uint32_t actual = xp_epub_test_clipping_signature(fixture.owner);
      assert(actual == expected && specReads == 1); ++signatureCases;
      for (unsigned count : {0u, 1u, 2u, 9u}) {
        CLIPPINGS.entries.resize(count); countReads = 0;
        assert(xp_epub_test_clipping_nonempty(fixture.owner) == uint8_t(count != 0));
        assert(countReads == 1); ++countCases;
      }
    }
  }
  assert(signatureCases == 17 && countCases == 68);
  std::printf("clipping real facts: %u signatures / %u counts; hooks disabled\n", signatureCases, countCases);
  return 0;
}
}  // namespace clipping_owner_adapter
