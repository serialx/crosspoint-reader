namespace render_adapter {
static EpubReaderActivity* active;
static Page* activePage;
static int modelDepth, lockDepth;
static bool bound, finishing;
static unsigned finishCount, closeCount, replayCount;
static void leaf() { assert(active && modelDepth == 0 && lockDepth == 1); }
static void root(void* value) { leaf(); assert(value && active->native.get() == value); }
static const auto actualServices = /* ACTUAL_SERVICES_LEAF */;
static const auto actualBind = /* ACTUAL_BIND_LEAF */;
static const auto actualFinish = /* ACTUAL_FINISH_LEAF */;
static uint32_t physicalRead(void* context, uint8_t query) {
  root(context);
  const auto& services = static_cast<NativeResources*>(context)->renderServices;
  return services.read(services.context, query);
}
static uint32_t physicalExecute(void* context, const XpUiReaderRenderOp* operation) {
  root(context); assert(operation);
  const auto& services = static_cast<NativeResources*>(context)->renderServices;
  return services.execute(services.context, operation);
}
static XpUiReaderRenderServices checkedServices;

template <typename Signature> struct Unexpected;
template <typename Result, typename... Args> struct Unexpected<Result (*)(Args...)> {
  static Result call(Args...) { leaf(); assert(false && "unexpected render leaf"); std::abort(); }
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
  table.render_services = [](void* context) -> const XpUiReaderRenderServices* {
    root(context);
    const auto* native = actualServices(context);
    assert(native == &active->native->renderServices && native->context == context && native->read && native->execute);
    checkedServices = {context, physicalRead, physicalExecute};
    return &checkedServices;
  };
  table.render_bind_page = [](void* context, void* page, int32_t font, int32_t left, int32_t top, int32_t bottom,
                             XpEpubHighlight highlights, void* highlightContext) {
    root(context);
    assert(!bound && !finishing && livePages == 1 && page == activePage && highlights && highlightContext);
    assert(font == 12 && left == 4 && top == 1 && bottom == 3 && "native Page binding must preserve coordinates");
    bound = true;
    actualBind(context, page, font, left, top, bottom, highlights, highlightContext);
    assert(active->native->frame && &active->native->frame->page == activePage);
  };
  table.render_finish_page = [](void* context) {
    root(context);
    assert(bound && !finishing && livePages == 1 && activePage && finishCount == 0);
    finishing = true;
    actualFinish(context);
    finishing = false;
    assert(!active->native->frame && !boundSource);
    bound = false;
    ++finishCount;
  };
  table.page_close = [](void* page) {
    leaf();
    assert(!bound && !finishing && finishCount == 1 && "native frame must finish before Page closes");
    assert(page == activePage && livePages == 1 && closeCount == 0 && !active->native->frame && !boundSource);
    delete static_cast<Page*>(page);
    activePage = nullptr;
    ++closeCount;
  };
  return table;
}();
#undef UNUSED_LEAF

static void enter(EpubReaderActivity& owner) {
  assert(!active && modelDepth == 0 && lockDepth == 0);
  active = &owner; lockDepth = 1; target = &owner.renderer;
}
static void leave() {
  assert(modelDepth == 0 && lockDepth == 1);
  lockDepth = 0; active = nullptr;
}
static void create(EpubReaderActivity& owner) {
  leaf();
  if (!owner.native) owner.native = std::make_unique<NativeResources>(owner.renderer);
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  owner.native->strips.prepareOk = owner.stripBuffers.prepareOk;
#endif
  assert(!owner.rustOwner && !owner.native->frame);
  const XpEpubTestRenderSeed seed{
    owner.native.get(), &owner, owner.pagesUntilFullRefresh, static_cast<uint8_t>(owner.forcedRefreshPending),
    [](void* context) {
      leaf(); assert(context == active && bound && activePage && livePages == 1 && !finishing);
      active->renderStatusBar();
    },
    [](void* context, void* page, int32_t font, int32_t top, int32_t left) {
      leaf(); assert(context == active && bound && page == activePage && livePages == 1);
      assert(font == 12 && top == 1 && left == 4 && "highlight callback must preserve font/top/left");
      if (finishing) ++replayCount;
      active->drawClippingHighlights(*static_cast<Page*>(page), font, top, left);
    },
  };
  owner.rustOwner = xp_epub_test_render_create(&seed, &leaves, &runtime, &resources);
  assert(owner.rustOwner);
}
static void lifetimeFacts() {
  EpubReaderActivity owner;
  enter(owner);
  owner.native = std::make_unique<NativeResources>(owner.renderer);
  assert(!owner.native->frame && livePages == 0);
  const auto* services = leaves.render_services(owner.native.get());
  assert(services->read(services->context, XP_UI_READER_RENDER_FREQUENCY) == 3);
  ESP.free = 123456;
  assert(services->read(services->context, XP_UI_READER_RENDER_FREE_HEAP) == 123456);
  ESP.block = 65432;
  assert(services->read(services->context, XP_UI_READER_RENDER_MAX_BLOCK) == 65432);
  XpUiReaderRenderOp store{};
  store.kind = XP_UI_READER_RENDER_STORE_BW;
  owner.renderer.storeOk = false;
  assert(services->execute(services->context, &store) == 0);
  owner.renderer.storeOk = true;
  assert(services->execute(services->context, &store) == 1);
  assert(!owner.native->frame && livePages == 0 && tick == 0);
  leave();
  BoardConfig::a4 = false; gpio.x3 = true;
  BoardConfig::ACTIVE.board = BoardConfig::Board::Other;
  display.controller = HalDisplay::Controller::UC8253;
  display.sourceOk = true; SETTINGS.textAntiAliasing = false;
  ESP.free = ESP.block = 100000;
  owner.forcedRefreshPending = true;
  replayOnDetach = true;
  auto page = std::make_unique<Page>();
  owner.renderContents(std::move(page), 1, 2, 3, 4);
  assert(!livePages && finishCount == 1 && closeCount == 1 && !boundSource);
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  assert(replayCount == 1 && "bound strip painter must call Rust while finish/access/Page remain live");
#else
  assert(replayCount == 0);
#endif
  assert(!owner.forcedRefreshPending);
  replayOnDetach = false;
  std::puts("render lifetime: unbound Frequency and StoreBw; finished native frame before Page close; bound callback lifetime checked");
}
}  // namespace render_adapter

static void replayBeforeDetach() {
  using namespace render_adapter;
  leaf();
  assert(finishing && bound && boundSource && activePage && livePages == 1);
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  auto& painter = *static_cast<ReaderStripPainter*>(boundSource->context);
  assert(&painter.page == activePage && painter.context && painter.highlights);
  // Invoke the actual NativeRenderFrame painter continuation, not a test-owned replacement.
  painter.highlights(painter.context, painter.page, painter.font, painter.left, painter.top);
#else
  assert(false && "strip source cannot exist in the off build");
#endif
}
EpubReaderActivity::~EpubReaderActivity() {
  if (!native && !rustOwner) return;
  render_adapter::enter(*this);
  assert(!render_adapter::bound && !render_adapter::finishing && !livePages && !boundSource);
  if (rustOwner) xp_epub_test_render_destroy(rustOwner);
  rustOwner = nullptr;
  // The old Activity's persistent strip arrays also freed after its `end` line.
  native.reset();
  render_adapter::leave();
}
void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, int top, int right, int bottom, int left) {
  using namespace render_adapter;
  enter(*this);
  assert(page && livePages == 1 && !bound && !finishing);
  activePage = page.get(); finishCount = closeCount = replayCount = 0;
  create(*this);
  const int32_t margins[] = {top, right, bottom, left};
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  constexpr uint8_t stripEnabled = 1;
#else
  constexpr uint8_t stripEnabled = 0;
#endif
  xp_epub_test_render_contents(rustOwner, page.release(), SETTINGS.getReaderFontId(), margins, stripEnabled);
  assert(!livePages && !activePage && finishCount == 1 && closeCount == 1 && !bound && !finishing);
  XpEpubTestRenderObservation state{};
  xp_epub_test_render_observe(rustOwner, &state);
  pagesUntilFullRefresh = state.counter;
  forcedRefreshPending = state.forced;
  leave();
}
