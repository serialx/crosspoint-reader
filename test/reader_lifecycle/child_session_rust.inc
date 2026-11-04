#include "child_session_fixture.h"

static XpAppText childText(const std::string& value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
static std::string childString(XpAppText value) {
  assert(value.data || value.len == 0);
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string{};
}
static const std::string& childNativeText(XpAppNativeText value) {
  assert(value.native_string);
  const auto& text = *static_cast<const std::string*>(value.native_string);
  assert(value.bytes.data == reinterpret_cast<const uint8_t*>(text.data()) && value.bytes.len == text.size());
  return text;
}

struct ChildSessionBridge;
static ChildSessionBridge* activeChild;
static LegacyEpubLeaves childLeaves;
static XpAppRuntime childRuntime;
static XpAppResources childResources;

struct ChildSessionBridge {
  EpubReaderActivity& owner;
  void* rust = nullptr;
  std::optional<GfxRenderer::FrameBufferLoan> loan;
  explicit ChildSessionBridge(EpubReaderActivity&);
  ~ChildSessionBridge() {
    assert(!loan && activeChild == this && !lockDepth && !modelDepth);
    xp_epub_test_child_destroy(rust);
    activeChild = nullptr;
  }
  void observe() {
    XpEpubTestChildObservation out{};
    xp_epub_test_child_observe(rust, &out);
    assert(bool(out.has_book) == bool(owner.epub));
    assert(bool(out.has_section) == bool(owner.section));
    if (owner.section) owner.section->currentPage = out.page;
    owner.nextPageNumber = out.next_page;
    owner.currentSpineIndex = out.spine;
    owner.pendingSyncSaveError = out.sync_error;
    owner.showDictionaryMessage = out.dictionary;
    owner.showBookmarkMessage = out.bookmark;
    owner.showClippingMessage = out.clipping;
    owner.clippingMessage = static_cast<StrId>(out.clipping_message);
    owner.dictionaryMessageTime = out.dictionary_time;
    owner.bookmarkMessageTime = out.bookmark_time;
    owner.clippingMessageTime = out.clipping_time;
    owner.currentPageVisibleOffset = out.has_offset ? std::optional<uint32_t>(out.visible_offset) : std::nullopt;
  }
  void deliver(const ActivityResult& result) {
    // The immutable oracle changes the spine before delivering a retained result.
    xp_epub_test_child_spine(rust, owner.currentSpineIndex);
    XpAppResultView view{};
    view.original = &result;
    view.cancelled = result.isCancelled;
    if (const auto* clipping = std::get_if<ClippingResult>(&result.data)) {
      view.kind = XP_APP_RESULT_CLIPPING;
      view.value.clipping = {
          {childText(clipping->text), &clipping->text}, clipping->startPageOffset, clipping->endPageOffset,
          clipping->startWordIndex, clipping->endWordIndex, clipping->wordCount, clipping->startOffset,
          clipping->endOffset, static_cast<uint8_t>(clipping->action)};
    } else if (const auto* footnote = std::get_if<FootnoteResult>(&result.data)) {
      view.kind = XP_APP_RESULT_FOOTNOTE;
      view.value.text = childText(footnote->href);
    } else if (const auto* progress = std::get_if<ProgressChangeResult>(&result.data)) {
      view.kind = XP_APP_RESULT_PROGRESS;
      view.value.progress.spine = progress->marker;
    }
    xp_epub_test_child_result(rust, &view);
    observe();
  }
  void push(void* raw) {
    owner.startActivityForResult(std::unique_ptr<Child>(static_cast<Child*>(raw)),
                                [this](const ActivityResult& result) { deliver(result); });
  }
};

static void configureChildLeaves() {
  childLeaves = overlayLegacyLeaves();
  childLeaves.physical.book_set_extractor = [](void* handle) {
    if (handle) childLeaves.book.book_install_extractor(handle);
    else childLeaves.book.book_clear_extractor();
  };
  childRuntime = overlayUnexpectedRuntime();
  childResources = overlayUnexpectedResources();
  childRuntime.model_lock = [] { assert(!modelDepth); ++modelDepth; };
  childRuntime.model_unlock = [] { assert(modelDepth == 1); --modelDepth; };
  childRuntime.render_lock = []() -> void* { return new RenderLock; };
  childRuntime.render_unlock = [](void* lock) { delete static_cast<RenderLock*>(lock); };
  childRuntime.now_ms = millis;
  childLeaves.physical.settings_snapshot = [](uint8_t font, XpEpubSettings* out) {
    assert(!modelDepth && !font);
    *out = {};
    out->screen_margin = SETTINGS.screenMargin;
    out->dictionary_available = SETTINGS.dictionaryName[0] != '\0';
  };
  childLeaves.book.section.section_page_count = [](void* raw) -> uint16_t {
    assert(!modelDepth && raw == activeChild->owner.section.get());
    return static_cast<Section*>(raw)->pageCount;
  };
  // Section facts publish silently; EstimateObserver records each policy read.
  childLeaves.book.section.section_estimated_pages = [](void* raw) -> int32_t {
    assert(!modelDepth && raw == activeChild->owner.section.get());
    return 9;
  };
  childLeaves.book.section.section_building = [](void*) -> uint8_t { assert(!modelDepth); return 0; };
  childLeaves.book.section.section_complete = [](void*) -> uint8_t { assert(!modelDepth); return 1; };
  childLeaves.book.section.section_partial = [](void*) -> uint8_t { assert(!modelDepth); return 0; };
  childLeaves.book.section.section_load_page = [](void* raw, int32_t page) -> void* {
    assert(lockDepth == 1 && !modelDepth && raw == activeChild->owner.section.get());
    return static_cast<Section*>(raw)->loadPage(page).release();
  };
  childLeaves.book.section.section_offset_for_page = [](void* raw, uint16_t page, uint32_t* offset) -> uint8_t {
    assert(lockDepth == 1 && !modelDepth && raw == activeChild->owner.section.get());
    const auto value = static_cast<Section*>(raw)->getVisibleTextOffsetForPage(page);
    if (value) *offset = *value;
    return value.has_value();
  };
  childLeaves.book.section.section_paragraph_for_page = [](void* raw, uint16_t page, uint16_t* paragraph) -> uint8_t {
    assert(lockDepth == 1 && !modelDepth && raw == activeChild->owner.section.get());
    const auto value = static_cast<Section*>(raw)->getParagraphIndexForPage(page);
    if (value) *paragraph = *value;
    return value.has_value();
  };
  childLeaves.book.section.section_close = [](void* raw) {
    assert(lockDepth == 1 && !modelDepth && raw == activeChild->owner.section.get());
    activeChild->owner.section.reset();
  };
  childLeaves.book.book_close = [](void* raw) {
    assert(lockDepth == 1 && !modelDepth && raw == activeChild->owner.epub.get());
    activeChild->owner.epub.reset();
  };
  childLeaves.physical.page_close = [](void* raw) { assert(!modelDepth); delete static_cast<Page*>(raw); };
  childLeaves.physical.render_margins = [](int32_t* margins) {
    activeChild->owner.renderer.getOrientedViewableTRBL(&margins[0], &margins[1], &margins[2], &margins[3]);
  };
  childLeaves.book.book_toc_for_spine = [](void* raw, int32_t spine) -> int32_t {
    assert(raw == activeChild->owner.epub.get());
    return static_cast<Epub*>(raw)->getTocIndexForSpineIndex(spine);
  };
  childLeaves.book.book_toc = [](void* raw, int32_t index, XpEpubTocSink sink, void* context) -> uint8_t {
    assert(raw == activeChild->owner.epub.get());
    const auto toc = static_cast<Epub*>(raw)->getTocItem(index);
    XpEpubToc row{};
    row.title = childText(toc.title);
    return sink(context, &row);
  };
  childLeaves.book.book_title = [](void* raw, XpAppTextSink sink, void* context) -> uint8_t {
    assert(raw == activeChild->owner.epub.get());
    const auto text = static_cast<Epub*>(raw)->getTitle();
    return sink(context, childText(text));
  };
  childLeaves.book.book_author = [](void* raw, XpAppTextSink sink, void* context) -> uint8_t {
    assert(raw == activeChild->owner.epub.get());
    const auto text = static_cast<Epub*>(raw)->getAuthor();
    return sink(context, childText(text));
  };
  childLeaves.book.book_path = [](void* raw, XpAppTextSink sink, void* context) -> uint8_t {
    assert(raw == activeChild->owner.epub.get());
    const auto text = static_cast<Epub*>(raw)->getPath();
    return sink(context, childText(text));
  };
  childLeaves.physical.metadata_footnotes = [](void* raw) -> uint16_t {
    assert(raw == activeChild && !modelDepth);
    return activeChild->owner.currentPageMetadata.footnotes().size();
  };
  childLeaves.physical.metadata_footnote = [](void* raw, uint16_t index, XpEpubFootnoteSink sink, void* context) -> uint8_t {
    assert(raw == activeChild && !modelDepth);
    const auto& notes = activeChild->owner.currentPageMetadata.footnotes();
    assert(index < notes.size());
    XpEpubFootnote row{};
    row.href = childText(notes[index].href);
    return sink(context, &row);
  };
  childLeaves.physical.clippings_count = []() -> uint32_t { return CLIPPINGS.hasClippings(); };
  childLeaves.physical.clippings_remove = [](uint32_t index) -> uint8_t {
    assert(lockDepth == 1);
    return CLIPPINGS.removeClippingAt(index);
  };
  childLeaves.physical.clippings_insert = [](const XpEpubClipInsert* row) -> uint8_t {
    assert(lockDepth == 1);
    const auto chapter = childString(row->chapter);
    const auto& r = row->range;
    return static_cast<uint8_t>(CLIPPINGS.addClipping(
        r.spine, r.start_page, r.end_page, r.page_count, r.start_word, r.end_word, row->word_count,
        chapter.c_str(), row->paragraph, childNativeText(row->text), r.signature, r.start_offset, r.end_offset));
  };
  childLeaves.physical.clippings_export = [](XpAppText title, XpAppText author, XpAppText chapter, int32_t page,
                                    XpAppNativeText text) -> uint8_t {
    assert(!lockDepth);
    return ClippingsManager::saveClipping(childString(title), childString(author), childString(chapter), page,
                                          childNativeText(text));
  };
  childLeaves.physical.child_selection = [](void** raw, uint8_t count, const XpEpubSelectionArgs* args) -> void* {
    assert(!lockDepth && !modelDepth && count > 0 && count <= 3);
    std::vector<std::unique_ptr<Page>> pages;
    pages.reserve(3);
    for (uint8_t i = 0; i < count; ++i) pages.emplace_back(static_cast<Page*>(raw[i]));
    auto child = makeUniqueNoThrow<ClipSelectionActivity>(
        activeChild->owner.renderer, activeChild->owner.mappedInput, std::move(pages), args->left, args->top,
        args->x, args->y, static_cast<ClipSelectionActivity::Mode>(args->mode));
    if (child) {
      for (uint8_t i = 0; i < count; ++i) raw[i] = nullptr;
    } else {
      for (auto& page : pages) page.release();
    }
    return child.release();
  };
  childLeaves.physical.child_dictionary = [](void* raw, int32_t left, int32_t top, XpAppText initial) -> void* {
    assert(!lockDepth && !modelDepth);
    std::unique_ptr<Page> page(static_cast<Page*>(raw));
    auto child = makeUniqueNoThrow<DictionaryWordSelectActivity>(
        activeChild->owner.renderer, activeChild->owner.mappedInput, std::move(page), left, top, childString(initial));
    if (!child) page.release();
    return child.release();
  };
  childLeaves.physical.child_footnote = [](void* raw, int32_t left, int32_t top) -> void* {
    assert(!lockDepth && !modelDepth);
    std::unique_ptr<Page> page(static_cast<Page*>(raw));
    auto child = makeUniqueNoThrow<EpubReaderFootnoteSelectActivity>(
        activeChild->owner.renderer, activeChild->owner.mappedInput, std::move(page), left, top);
    if (!child) page.release();
    return child.release();
  };
  childLeaves.physical.child_clippings = []() -> void* {
    assert(!lockDepth && !modelDepth);
    return makeUniqueNoThrow<EpubReaderClippingListActivity>(activeChild->owner.renderer,
                                                          activeChild->owner.mappedInput).release();
  };
  childLeaves.physical.child_sync = [](XpAppText path, const XpEpubPosition* position, const XpEpubSavedProgress* saved,
                            XpAppText chapter, uint8_t events) -> void* {
    assert(!modelDepth);
    // The historical sync path was infallible; its allocation failures have a separate test.
    return std::make_unique<KOReaderSyncActivity>(
        activeChild->owner.renderer, activeChild->owner.mappedInput, childString(path),
        CrossPointPosition{position->spine}, SavedProgressPosition{childString(saved->xpath), saved->percentage},
        childString(chapter), events != 0).release();
  };
  childLeaves.physical.has_sync_credentials = []() -> uint8_t { return KOREADER_STORE.hasCredentials(); };
  childLeaves.physical.strip_release_idle = [](void* raw) -> uint8_t {
    assert(raw == activeChild);
    activeChild->owner.stripBuffers.releaseIdle();
    return 1;
  };
  childLeaves.physical.image_release_cache = [] { ImageBlock::releaseRenderCache(); };
  childLeaves.book.book_clear_extractor = [] { ImageBlock::setExtractor(nullptr, nullptr); };
  childLeaves.physical.fonts_release_caches = [] {
    if (auto* cache = activeChild->owner.renderer.getFontCacheManager()) cache->releaseSdFontCaches();
  };
  childLeaves.physical.framebuffer_lend = []() -> uint8_t {
    assert(!activeChild->loan);
    activeChild->loan.emplace(activeChild->owner.renderer);
    return 1;
  };
  childLeaves.physical.framebuffer_restore = [](uint8_t owned) {
    assert(owned && activeChild->loan);
    activeChild->loan.reset();
  };  childLeaves.physical.progress_to_saved = [](void* raw, const XpEpubPosition* position, XpEpubSavedProgressSink sink,
                                    void* context) -> uint8_t {
    assert(raw == activeChild->owner.epub.get());
    const auto saved = ProgressMapper::toSavedProgress(activeChild->owner.epub, CrossPointPosition{position->spine});
    const XpEpubSavedProgress row{childText(saved.xpath), saved.percentage};
    return sink(context, &row);
  };
  childLeaves.physical.render_services = [](void* raw) -> const XpUiReaderRenderServices* {
    assert(raw == activeChild);
    static const XpUiReaderRenderServices services{
        nullptr,
        [](void*, uint8_t query) -> uint32_t {
          assert(query == XP_UI_READER_RENDER_FREE_HEAP && lockDepth == 1);
          return ESP.getFreeHeap();
        },
        &OverlayUnexpected<decltype(XpUiReaderRenderServices::execute)>::call};
    return &services;
  };
  childLeaves.physical.log_enabled = [](uint8_t severity) -> uint8_t {
#ifdef CHILD_RELEASE_LOG
    return severity != 0;
#else
    return 1;
#endif
  };
  childLeaves.physical.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    assert(!modelDepth);
#ifdef CHILD_RELEASE_LOG
    if (severity == 0) return;
#endif
    std::printf("%s %s\n", childString(origin).c_str(), childString(message).c_str());
  };
}

ChildSessionBridge::ChildSessionBridge(EpubReaderActivity& value) : owner(value) {
  assert(!activeChild && !lockDepth && !modelDepth);
  activeChild = this;
  configureChildLeaves();
  const XpEpubTestChildSeed seed{
      this, owner.epub.get(), owner.section.get(), owner.currentSpineIndex,
      owner.section ? owner.section->currentPage : 0, owner.nextPageNumber, owner.cachedChapterTotalPageCount,
      static_cast<uint16_t>(owner.buildViewportWidth), static_cast<uint16_t>(owner.buildViewportHeight),
      static_cast<uint8_t>(owner.footnoteDepth), this,
      [](void* p) { static_cast<ChildSessionBridge*>(p)->owner.addBookmark(); },
      [](void* p) { static_cast<ChildSessionBridge*>(p)->owner.openReaderMenu(); },
      [](void* p, const XpAppProgressResult* result) {
        static_cast<ChildSessionBridge*>(p)->owner.applyProgressChange(ProgressChangeResult{result->spine});
      },
      [](void* p) { static_cast<ChildSessionBridge*>(p)->owner.flushReaderSession(); },
      [](void* p, int32_t spine, int32_t page, int32_t count) -> uint8_t {
        return static_cast<ChildSessionBridge*>(p)->owner.saveProgress(spine, page, count);
      },
      [](void* p, XpAppText href, uint8_t save) {
        static_cast<ChildSessionBridge*>(p)->owner.navigateToHref(childString(href), save != 0);
      },
      [](void* p, void* page, int32_t x, int32_t y) -> int32_t {
        return static_cast<ChildSessionBridge*>(p)->owner.clippingAtPoint(*static_cast<Page*>(page), x, y);
      },
      [](void*, void* child) { activityManager.replaceActivity(std::unique_ptr<Child>(static_cast<Child*>(child))); },
      [](void* p, XpEpubPosition* out) {
        const auto position = static_cast<ChildSessionBridge*>(p)->owner.getCurrentPosition();
        *out = {};
        out->spine = position.marker;
      },
      [](void*, uint16_t width, uint16_t height) -> uint32_t {
        return readerRenderSpecSignature(SETTINGS.readerRenderSpec(width, height));
      },
      [](void* p, int32_t estimate) {
        auto& owner = static_cast<ChildSessionBridge*>(p)->owner;
        assert(owner.section && owner.section->estimatedTotalPages() == estimate);
      },
      [](void* p) { static_cast<ChildSessionBridge*>(p)->owner.discardOverlayPage(); },
      [](void* p) { static_cast<ChildSessionBridge*>(p)->owner.saveLinkStack(); },
      [](void* p) { static_cast<ChildSessionBridge*>(p)->owner.requestUpdate(); },
      [](void* p, void* child) { static_cast<ChildSessionBridge*>(p)->push(child); },
      [](void*) { assert(false && "historical child fixture cannot finish"); }};
  rust = xp_epub_test_child_create_legacy_book(&seed,&childLeaves.physical, &childRuntime, &childResources, &childLeaves.book);
  assert(rust);
}

void EpubReaderActivity::openDictionaryWordSelect() {
  assert(activeChild && &activeChild->owner == this);
  xp_epub_test_child_dictionary(activeChild->rust);
  activeChild->observe();
}
void EpubReaderActivity::startClipSelection(int x, int y, ClipSelectionActivity::Mode mode) {
  assert(activeChild && &activeChild->owner == this);
  xp_epub_test_child_selection(activeChild->rust, x, y, static_cast<uint8_t>(mode));
  activeChild->observe();
}
void EpubReaderActivity::openFootnoteSelect(bool reopen) {
  assert(activeChild && &activeChild->owner == this);
  xp_epub_test_child_footnotes(activeChild->rust, reopen);
  activeChild->observe();
}
void EpubReaderActivity::openClippings() {
  assert(activeChild && &activeChild->owner == this);
  xp_epub_test_child_clippings(activeChild->rust);
  activeChild->observe();
}
bool EpubReaderActivity::launchKOReaderSync() {
  assert(activeChild && &activeChild->owner == this);
  const bool result = xp_epub_test_child_sync(activeChild->rust);
  activeChild->observe();
  return result;
}
