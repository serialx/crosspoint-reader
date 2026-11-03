// Physical facts match scene_physical_bindings.inc; real owner renders/builds its Section.
static const XpUiReaderRenderServices resultRenderServices{
  nullptr,
  [](void*, uint8_t query) -> uint32_t {
    physical("renderer-query", 1);
    switch (query) {
      case XP_UI_READER_RENDER_NOW: return 100;
      case XP_UI_READER_RENDER_FREQUENCY: return 3;
      case XP_UI_READER_RENDER_FREE_HEAP:
      case XP_UI_READER_RENDER_MAX_BLOCK: return 100000;
      case XP_UI_READER_RENDER_HEIGHT: return 792;
      case XP_UI_READER_RENDER_WIDTH_BYTES: return 66;
      case XP_UI_READER_RENDER_IS_BW: return 1;
      case XP_UI_READER_RENDER_HAS_IMAGES:
      case XP_UI_READER_RENDER_IMAGES_NEED_DECODE:
      case XP_UI_READER_RENDER_TEXT_AA:
      case XP_UI_READER_RENDER_IS_A4:
      case XP_UI_READER_RENDER_IS_X3:
      case XP_UI_READER_RENDER_IS_UC8279:
      case XP_UI_READER_RENDER_ABSOLUTE_SUPPORTED:
      case XP_UI_READER_RENDER_OVERLAY_CAPS:
      case XP_UI_READER_RENDER_ABSOLUTE_CAPS:
      case XP_UI_READER_RENDER_STRIP_BOARD:
      case XP_UI_READER_RENDER_ELEMENTS: return 0;
      default: assert(false && "unexpected renderer query"); return 0;
    }
  },
  [](void*, const XpUiReaderRenderOp* op) -> uint32_t {
    physical("renderer-operation", 1); assert(op);
    switch (op->kind) {
      case XP_UI_READER_RENDER_PAGE: assert(results.bound); ++results.rendered; return 1;
      case XP_UI_READER_RENDER_STORE_BW:
      case XP_UI_READER_RENDER_CLEANUP:
      case XP_UI_READER_RENDER_DISPLAY_ASYNC:
      case XP_UI_READER_RENDER_DISPLAY: return 1;
      case XP_UI_READER_RENDER_CLEAR:
      case XP_UI_READER_RENDER_IMAGE_RESET:
      case XP_UI_READER_RENDER_PREWARM_BEGIN:
      case XP_UI_READER_RENDER_PREWARM_END:
      case XP_UI_READER_RENDER_MODE:
      case XP_UI_READER_RENDER_MARK:
      case XP_UI_READER_RENDER_LOG:
      case XP_UI_READER_RENDER_RELEASE_STRIPS: return 0;
      default: assert(false && "unexpected renderer operation"); return 0;
    }
  }
};
void bindResultsPhysical() {
  runtime.now_ms = []() -> uint32_t { physical("clock"); return 100; };
  runtime.delay_ms = [](uint32_t ms) { borrowedUse("wifi-delay"); assert(ms == 30); };
  leaves.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    physical("settings"); assert(resolve <= 1); *out = {}; out->embedded_style = 1;
    out->font = resolve ? 1 : 0; out->font_size = 14; out->font_family = literal("fixture");
    out->dictionary_available = 1; out->menu_style = results.toolbarMode ? 1 : 0;
  };
  leaves.render_geometry = [](XpEpubGeometry* out) {
    physical("geometry"); *out = {}; out->width = 528; out->height = 792;
    out->has_framebuffer = 1; out->async_refresh = 1; out->title_mode = 2; out->ui_font_12 = 1;
  };
  leaves.render_margins = [](int32_t* out) { physical("margins"); for (int i = 0; i < 4; ++i) out[i] = 0; };
  leaves.settings_render_spec = [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
    physical("render-spec"); *out = {}; out->width = width; out->height = height;
    out->font = 1; out->compression = 1; out->word_spacing_percent = 100;
  };
  leaves.render_services = [](void*) -> const XpUiReaderRenderServices* { physical("render-services", 1); return &resultRenderServices; };
  leaves.render_bind_page = [](void*, void* page, int32_t, int32_t, int32_t, int32_t, XpEpubHighlight, void*) {
    physical("bind-page", 1); assert(results.pages.count(static_cast<ResultPage*>(page)) && !results.bound); results.bound = page;
  };
  leaves.render_finish_page = [](void*) { physical("finish-page", 1); assert(results.bound); results.bound = nullptr; };
  leaves.render_busy = []() -> uint8_t { physical("display-busy"); return 0; };
  leaves.strip_release_idle = [](void*) -> uint8_t { physical("strip-release", 1); return 1; };
  legacyBook.book_spine_count = [](void*) -> int32_t { physical("spine-count"); return 7; };
  legacyBook.book_size = [](void*) -> uint32_t { physical("book-size"); return 1000; };
  legacyBook.book_progress = [](void*, int32_t, float) -> float { physical("book-progress"); return .1f; };
  legacyBook.book_spine_for_size = [](void*, uint32_t size) -> int32_t { borrowedUse("spine-for-size"); assert(size == 420); return 2; };
  legacyBook.book_cumulative_size = [](void*, int32_t spine) -> uint32_t { physical("cumulative-size"); assert(spine == 1 || spine == 2); return spine == 1 ? 300 : 500; };
  legacyBook.book_toc_count = [](void*) -> int32_t { physical("toc-count"); return 5; };
  legacyBook.book_toc_for_spine = [](void*, int32_t) -> int32_t { physical("toc-for-spine"); return 0; };
  legacyBook.book_spine = [](void*, int32_t, XpEpubSpineSink sink, void* context) -> uint8_t {
    physical("book-spine", 1); const XpEpubSpine value{literal("chapter.xhtml"), 100}; return sink(context, &value);
  };
  legacyBook.book_toc = [](void*, int32_t, XpEpubTocSink sink, void* context) -> uint8_t {
    physical("book-toc"); const XpEpubToc value{literal("Chapter"), {nullptr, 0}, 1, 0}; return sink(context, &value);
  };
  legacyBook.book_language = [](void*, XpAppTextSink sink, void* context) -> uint8_t { physical("language"); return deliver(sink, context, "en"); };
  legacyBook.book_title = [](void*, XpAppTextSink sink, void* context) -> uint8_t { physical("title"); return deliver(sink, context, "Fixture"); };
  legacyBook.book_author = [](void*, XpAppTextSink sink, void* context) -> uint8_t { physical("author"); return deliver(sink, context, "Author"); };
  legacyBook.book_path = [](void*, XpAppTextSink sink, void* context) -> uint8_t { physical("book-path"); return deliver(sink, context, f().path.c_str()); };
  legacyBook.book_resolve_href = [](void*, XpAppText href) -> int32_t {
    borrowedUse("resolve-href"); assert(text(href) == "target.xhtml#anchor");
    assert(href.data == reinterpret_cast<const uint8_t*>(std::get<FootnoteResult>(results.borrowed->data).href.data()));
    return 3;
  };
  leaves.section_open = [](void*, int32_t spine) -> void* {
    physical("section-open", 1); assert(!results.section); ++results.sectionOpened;
    return results.section = new ResultSection{spine};
  };
  leaves.section_close = [](void* raw) {
    physical("section-close", 1); assert(raw == results.section && !results.bound);
    delete results.section; results.section = nullptr; ++results.sectionClosed;
  };
  leaves.section_page_count = [](void* raw) -> uint16_t { physical("section-pages", 1); assert(raw == results.section); return 10; };
  leaves.section_estimated_pages = [](void* raw) -> int32_t { physical("section-estimate", 1); assert(raw == results.section); return 10; };
  leaves.section_building = [](void* raw) -> uint8_t { physical("section-building", 1); assert(raw == results.section); return 0; };
  leaves.section_complete = [](void* raw) -> uint8_t { physical("section-complete", 1); assert(raw == results.section); return 1; };
  leaves.section_partial = [](void* raw) -> uint8_t { physical("section-partial", 1); assert(raw == results.section); return 0; };
  leaves.section_load_cache = [](void* raw, const XpEpubRenderSpec*) -> uint8_t { physical("section-cache", 1); assert(raw == results.section); return 1; };
  leaves.section_toc_for_page = [](void* raw, void*, int32_t, int32_t) -> int32_t { physical("section-toc", 1); assert(!raw || raw == results.section); return 0; };
  leaves.section_offset_for_page = [](void* raw, uint16_t page, uint32_t* offset) -> uint8_t { physical("section-offset", 1); assert(raw == results.section); *offset = page * 100; return 1; };
  leaves.section_page_for_offset = [](void* raw, uint32_t, uint8_t, uint16_t* page) -> uint8_t { physical("offset-page", 1); assert(raw == results.section); *page = 3; return 1; };
  leaves.section_find_anchor = [](void* raw, XpAppText anchor, uint16_t* page) -> uint8_t { physical("find-anchor", 1); assert(raw == results.section && anchor.len); *page = 2; return 1; };
  leaves.section_cached_anchor = leaves.section_find_anchor;
  leaves.section_reached_offset = [](void* raw, uint32_t) -> uint8_t { physical("reached-offset", 1); assert(raw == results.section); return 1; };
  leaves.section_paragraph_for_page = [](void* raw, uint16_t, uint16_t* paragraph) -> uint8_t { physical("paragraph", 1); assert(raw == results.section); *paragraph = 7; return 1; };
  leaves.section_load_page = [](void* raw, int32_t page) -> void* {
    physical("page-open", 1); assert(raw == results.section);
    auto* value = new ResultPage{page}; assert(results.pages.insert(value).second); return value;
  };
  leaves.section_page_text = [](void* raw, int32_t, XpAppTextSink sink, void* context) -> uint8_t { physical("page-text", 1); assert(raw == results.section); return deliver(sink, context, "page words"); };
  leaves.page_close = [](void* raw) { releasePage(static_cast<ResultPage*>(raw)); };
  leaves.page_info = [](void* raw, XpEpubPageInfo* out) { physical("page-info", 1); assert(results.pages.count(static_cast<ResultPage*>(raw))); *out = {}; };
  leaves.metadata_assign = [](void*, void* raw) -> uint8_t { physical("metadata-copy", 1); assert(results.pages.count(static_cast<ResultPage*>(raw))); return 1; };
  leaves.metadata_clear = [](void*) { physical("metadata-clear", 1); };
  leaves.metadata_footnotes = [](void*) -> uint16_t { physical("footnote-count", 1); return 2; };
  leaves.bookmarks_count = [](void*) -> uint32_t { physical("bookmark-count"); return 0; };
  leaves.bookmarks_clear = [](void* raw) { auto& h = f().handle(raw, Kind::Bookmarks); f().physical("bookmarks-clear", h.generation, h.id); };
  legacyBook.bookmarks_load = [](void* raw, void* book) -> uint8_t {
    auto& h = f().handle(raw, Kind::Bookmarks); f().handle(book, Kind::Book); ++f().bookmarkLoads; f().physical("bookmarks-load", h.generation, h.id); return 1;
  };
  leaves.bookmarks_retain = [](void*, uint8_t (*)(void*, const XpEpubBookmark*), void*) -> uint32_t { borrowedUse("bookmark-retain"); return 0; };
  leaves.bookmarks_insert_front = [](void*, const XpEpubBookmark* row) -> uint8_t { borrowedUse("bookmark-insert"); assert(row && row->has_offset); ++results.bookmarkWrites; return 1; };
  legacyBook.bookmarks_save = [](void*, void*) -> uint8_t { borrowedUse("bookmark-save"); return 1; };
  leaves.clippings_count = []() -> uint32_t { physical("clip-count"); return 1; };
  leaves.clippings_row = [](uint32_t index, XpEpubClip* out) { physical("clip-row", 1); assert(index == 0); *out = {}; out->spine = 6; };
  leaves.clippings_insert = [](const XpEpubClipInsert* row) -> uint8_t {
    borrowedUse("clip-insert"); assert(f().renderDepth == 1 && row);
    assert(row->text.bytes.data == results.borrowedText && row->text.bytes.len == 4096);
    assert(row->text.native_string == &std::get<ClippingResult>(results.borrowed->data).text);
    assert(row->word_count == 8 && row->paragraph == 7 && text(row->chapter) == "Chapter");
    ++results.inserts; return 0;
  };
  leaves.clippings_export = [](XpAppText title, XpAppText author, XpAppText chapter, int32_t, XpAppNativeText value) -> uint8_t {
    borrowedUse("clip-export"); assert(!f().renderDepth && value.bytes.data == results.borrowedText);
    assert(text(title) == "Fixture" && text(author) == "Author" && text(chapter) == "Chapter");
    ++results.exports; return 1;
  };
  leaves.progress_to_saved = [](void*, const XpEpubPosition*, XpEpubSavedProgressSink sink, void* context) -> uint8_t {
    physical("progress-to-saved"); const XpEpubSavedProgress saved{literal("saved-xpath"), .25f}; return sink(context, &saved);
  };
  leaves.file_open_write = [](XpAppText path, XpAppText origin) -> void* {
    assert(origin.len == 3 && std::memcmp(origin.data, "ERS", 3) == 0);
    physical("links-open", 1); assert(text(path) == "/cache/links.bin" && !f().extractor);
    assert(std::count_if(f().handles.begin(), f().handles.end(), [](const auto& entry) { return entry.second->kind == Kind::Book; }) == 1);
    return f().acquire(Kind::File, f().generation);
  };
  leaves.file_write = [](void* raw, const uint8_t* bytes, uint32_t count) -> uint32_t {
    physical("links-write", 1); f().handle(raw, Kind::File);
    assert(bytes && count == 9 && results.savedLinks.empty());
    results.savedLinks.assign(bytes, bytes + count); return count;
  };
  leaves.progress_save = [](void*, const XpEpubPosition*) -> uint8_t { physical("progress-save", 1); return 1; };
  leaves.paint_status = [](const XpEpubStatus* out) { physical("status", 1); assert(out); };
  leaves.radio_status = [](XpEpubRadio* out, XpEpubPeer* peer) { physical("radio-status"); assert(!peer); *out = {}; };
  leaves.radio_settle_start = [] { physical("radio-settle", 1); };
  leaves.radio_after_paint = [] { physical("radio-after-paint", 1); };
  leaves.fonts_take_corrupt_notice = []() -> uint8_t { physical("font-notice", 1); return 0; };
  leaves.trusted_now = []() -> int64_t { physical("trusted-time"); return 1000; };
  leaves.state_set_open_path = [](XpAppText path) { physical("set-path", 0); assert(text(path) == f().path); };
  static auto baseStateSave = leaves.state_save;
  leaves.state_save = [] { if (f().renderDepth == 1) baseStateSave(); else physical("state-save"); };
  legacyBook.recents_add = [](void* book) { physical("recent-add", 0); assert(book == f().extractor); };
  leaves.emit_open = [](XpAppText path) { physical("emit-open", 0); assert(text(path) == f().path); };
  static auto entryFonts = leaves.fonts_ensure_loaded;
  leaves.fonts_ensure_loaded = [] { if (!f().renderDepth) entryFonts(); else { borrowedUse("fonts-reload"); assert(f().renderDepth == 1); } };
  leaves.settings_save = [] { borrowedUse("settings-save"); ++results.settingsSaved; };
  leaves.toolbar_open = []() -> void* { physical("toolbar-open", 0); assert(!results.toolbar); return results.toolbar = new ResultToolbar; };
  leaves.toolbar_close = [](void* raw) { physical("toolbar-close", 1); assert(raw == results.toolbar); delete results.toolbar; results.toolbar = nullptr; };
  leaves.toolbar_begin = [](void* raw) { physical("toolbar-begin", 0); assert(raw == results.toolbar); ++results.toolbar->begins; ++results.toolbarBegins; };
  leaves.popup_active = [](void* raw) -> uint8_t { physical("popup-active"); auto it = f().handles.find(raw); assert(it != f().handles.end());
    assert(it->second->kind == Kind::OverlayPopup || it->second->kind == Kind::FailurePopup); return 0; };
  leaves.popup_dismiss = [](void* raw) { physical("popup-dismiss", 0); f().handle(raw, Kind::OverlayPopup); };
  leaves.render_discard_bw = [] { physical("discard-backup", 1); };
  leaves.input_services = []() -> const XpUiReaderInputServices* {
    physical("input-services", 0);
    static const XpUiReaderInputServices input{
      nullptr,
      &AppFixtureUnexpected<uint8_t (*)(void*, uint32_t, uint8_t, uint32_t), 9001>::call,
      [](void*, uint8_t query) -> uint32_t { physical("has-touch", 0); assert(query == XP_UI_READER_INPUT_HAS_TOUCH); return 1; },
      &AppFixtureUnexpected<uint8_t (*)(void*, int32_t*, int32_t*), 9002>::call
    };
    return &input;
  };
  leaves.child_menu = [](const XpEpubMenuArgs* args) -> void* { assert(args && text(args->title) == "Fixture"); return child("menu"); };
  leaves.child_chapter = [](void* book, int32_t toc) -> void* { assert(book == f().extractor && toc == 0); return child("chapter"); };
  leaves.child_percent = [](int32_t) -> void* { return child("percent"); };
  leaves.child_text_settings = []() -> void* { return child("text"); };
  leaves.child_bookmarks = [](void* book, XpAppText path) -> void* { assert(book == f().extractor && text(path) == f().path); return child("bookmarks"); };
  leaves.child_clippings = []() -> void* { return child("clippings"); };
  leaves.child_qr = [](XpAppText value) -> void* { assert(text(value) == "page words"); return child("qr"); };
  leaves.child_footnote = [](void* page, int32_t, int32_t) -> void* { return child("footnote", {static_cast<ResultPage*>(page)}); };
  leaves.child_selection = [](void** pages, uint8_t count, const XpEpubSelectionArgs*) -> void* {
    assert(count > 0 && count <= 3); std::vector<ResultPage*> owned;
    for (unsigned i = 0; i < count; ++i) { auto* page = static_cast<ResultPage*>(pages[i]); assert(results.pages.count(page)); owned.push_back(page); }
    return child("selection", std::move(owned));
  };
  leaves.child_dictionary = [](void* page, int32_t, int32_t, XpAppText initial) -> void* {
    borrowedUse("dictionary-create"); assert(initial.data == results.borrowedText && initial.len == 4096); ++results.lookups;
    return child("dictionary", {static_cast<ResultPage*>(page)});
  };
  leaves.child_wifi = []() -> void* { return child("wifi"); };
  leaves.wifi_connected = []() -> uint8_t { borrowedUse("wifi-connected"); ++results.wifiChecks; return results.wifiConnected; };
  leaves.trusted_sync = [](uint32_t timeout) -> uint8_t { borrowedUse("trusted-sync"); assert(timeout == 5000); ++results.syncs; return results.syncSucceeded; };
  leaves.wifi_disconnect = [](uint8_t off) { borrowedUse("wifi-disconnect"); assert(!off); };
  resources.popup_draw = [](XpAppText message) -> XpUiRect { borrowedUse("sync-popup-draw"); assert(message.len); return {}; };
  resources.translate = [](uint16_t) -> XpAppText { physical("translate"); return literal("Fixture"); };
  leaves.popup_show = [](void* popup, XpAppText, XpAppText message, uint32_t count, int32_t selected,
                         uint8_t (*label)(void*, uint32_t, XpAppTextSink, void*), void* labelContext,
                         void (*choice)(void*, int32_t), void* context) {
    borrowedUse("retry-popup"); assert(f().renderDepth == 1 && count == 2 && selected == 0 && message.len);
    f().handle(popup, Kind::FailurePopup);
    for (uint32_t i = 0; i < count; ++i) {
      std::string copied;
      assert(label(labelContext, i, [](void* raw, XpAppText value) -> uint8_t {
        *static_cast<std::string*>(raw) = text(value); return 1;
      }, &copied) == 1 && !copied.empty());
    }
    results.popupChoice = choice; results.popupContext = context; results.popupHandle = popup;
  };
  static auto basePopupClose = leaves.popup_close;
  leaves.popup_close = [](void* popup) {
    if (popup == results.popupHandle) { results.popupChoice = nullptr; results.popupContext = nullptr; results.popupHandle = nullptr; }
    basePopupClose(popup);
  };
  // Preserve inherited entry/exit assertions; additional actual reader logs are checked by source tag.
  static auto entryLog = leaves.log;
  leaves.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    if (text(origin) == "ERS" && text(message) != "Loaded cache: 1, 2") { physical("reader-log"); return; }
    entryLog(severity, origin, message);
  };
  platform = {&legacy, &resources, &runtime, &leaves};
}
