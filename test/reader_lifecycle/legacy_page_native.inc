// Frozen deafbe10 Page/frame oracle fragments. Extraction input, not a translation unit.
// Only named historical synthetic-owner tests may use this file.

struct NativeRenderFrame {
  GfxRenderer& renderer;
  XpEpubLegacyHighlight highlight;
  void* highlightContext;
  Page& page;
  int font, left, top;
  bool images = false;
  struct ImageGuard {
    ~ImageGuard() { ImageBlock::releaseRenderCache(); }
  } imageGuard;
  std::optional<FontCacheManager::PrewarmScope> prewarm;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  struct StripFrame {
    GfxRenderer& renderer;
    ReaderStripBuffers& buffers;
    ReaderStripPainter painter;
    XpPagePlan plan{};
    XpDisplaySource source{};
    bool active = false;
    ~StripFrame() {
      if (active) display.setFrameSource(nullptr);
      buffers.setSourceLoanActive(false);
      renderer.usePagePlan(nullptr, nullptr, 0);
      ImageBlock::setRenderReadWindow(nullptr, 0);
    }
  } strips;
#endif
  std::unique_ptr<uint8_t[]> lsb, msb, scratch;
  uint8_t* borrowedScratch = nullptr;
  unsigned long times[10]{};
  NativeRenderFrame(GfxRenderer& renderer, Page& page, int font, int left, int top, int bottom,
                    XpEpubLegacyHighlight highlight, void* context
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
                    ,
                    ReaderStripBuffers& buffers
#endif
                    )
      : renderer(renderer),
        highlight(highlight),
        highlightContext(context),
        page(page),
        font(font),
        left(left),
        top(top)
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        ,
        strips{renderer,
               buffers,
               {renderer, page, font, left, top, renderer.getScreenHeight() - bottom - 8, display.isInverted(), this,
                [](void* raw, const Page& page, int font, int left, int top) {
                  auto& frame = *static_cast<NativeRenderFrame*>(raw);
                  frame.highlight(frame.highlightContext, const_cast<Page*>(&page), font, left, top);
                }}}
#endif
  {
    (void)bottom;
  }
  uint8_t* buffer(uint8_t id) {
    switch (static_cast<XpUiReaderRenderBuffer>(id)) {
      case XP_UI_READER_BUFFER_LSB:
        return lsb.get();
      case XP_UI_READER_BUFFER_MSB:
        return msb.get();
      case XP_UI_READER_BUFFER_SCRATCH:
        return borrowedScratch ? borrowedScratch : scratch.get();
    }
    return nullptr;
  }
  uint32_t read(uint8_t query) {
    switch (static_cast<XpUiReaderRenderQuery>(query)) {
      case XP_UI_READER_RENDER_NOW:
        return millis();
      case XP_UI_READER_RENDER_HAS_IMAGES:
        images = page.hasImages();
        return images;
      case XP_UI_READER_RENDER_IMAGES_NEED_DECODE:
        return page.hasImagesNeedingDecode();
      case XP_UI_READER_RENDER_TAKE_FORCED:
      case XP_UI_READER_RENDER_COUNTER:
        assert(false);  // Intercepted by the Rust session.
        return 0;
      case XP_UI_READER_RENDER_FREQUENCY:
        return SETTINGS.getRefreshFrequency();
      case XP_UI_READER_RENDER_TEXT_AA:
        return SETTINGS.textAntiAliasing;
      case XP_UI_READER_RENDER_IS_A4:
        return BoardConfig::isEegoA4();
      case XP_UI_READER_RENDER_IS_BW:
        return renderer.getRenderMode() == GfxRenderer::BW;
      case XP_UI_READER_RENDER_IS_X3:
        return gpio.deviceIsX3();
      case XP_UI_READER_RENDER_IS_UC8279:
        return display.getController() == HalDisplay::Controller::UC8279;
      case XP_UI_READER_RENDER_ABSOLUTE_SUPPORTED:
        return renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
      case XP_UI_READER_RENDER_OVERLAY_CAPS:
      case XP_UI_READER_RENDER_ABSOLUTE_CAPS: {
        const auto caps = renderer.grayscaleCapabilities(query == XP_UI_READER_RENDER_ABSOLUTE_CAPS
                                                             ? HalDisplay::GrayscaleMode::Absolute
                                                             : HalDisplay::GrayscaleMode::Overlay);
        return (caps.stripUploads ? 1u : 0u) | (caps.base == HalDisplay::GrayscaleBase::Combined ? 2u : 0u) |
               (caps.asyncBase ? 4u : 0u);
      }
      case XP_UI_READER_RENDER_STRIP_BOARD:
        return display.getController() == HalDisplay::Controller::UC8253 ||
               (BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4 &&
                display.getController() == HalDisplay::Controller::SSD1677);
      case XP_UI_READER_RENDER_ELEMENTS:
        return page.elementCount();
      case XP_UI_READER_RENDER_WIDTH_BYTES:
        return renderer.getDisplayWidthBytes();
      case XP_UI_READER_RENDER_HEIGHT:
        return renderer.getDisplayHeight();
      case XP_UI_READER_RENDER_FREE_HEAP:
        return ESP.getFreeHeap();
      case XP_UI_READER_RENDER_MAX_BLOCK:
        return ESP.getMaxAllocHeap();
    }
    return 0;
  }
  [[gnu::noinline]] void log(uint32_t id) const {  // Keep log locals off the nested page-drawing stack.
    const auto* t = times;
    switch (static_cast<XpUiReaderRenderLog>(id)) {
      case XP_UI_READER_RENDER_STRIP_OOM:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        LOG_ERR(
            "ERS", "OOM: reader strips (%u bytes); using framebuffer",
            static_cast<unsigned>(ReaderStripBuffers::SCRATCH_BYTES + ReaderStripBuffers::COMMAND_BYTES +
                                  sizeof(XpPageRowBounds) * ReaderStripBuffers::MAX_ELEMENTS + (images ? 4096 : 0)));
#endif
        break;
      case XP_UI_READER_RENDER_ABSOLUTE_FAILED:
        LOG_ERR("ERS", "Could not start absolute image page; displaying B/W");
        break;
      case XP_UI_READER_RENDER_ABSOLUTE_QUALITY:
        LOG_DBG("ERS", "UC8279 image page: absolute quality waveform");
        break;
      case XP_UI_READER_RENDER_SCRATCH_OOM:
        LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); skipping AA this page",
                renderer.getDisplayWidthBytes() * 80);
        break;
      case XP_UI_READER_RENDER_STORE_FAILED:
        LOG_ERR("ERS", "Failed to store BW buffer for grayscale render; skipping grayscale this page");
        break;
      case XP_UI_READER_RENDER_TIMING_ASYNC:
        LOG_DBG("ERS",
                "Page render (tiled async): prewarm=%lums bw_render=%lums display=%lums gray_render=%lums "
                "wait=%lums gray_write=%lums gray_display=%lums cleanup=%lums total=%lums (planes buffered: %d)",
                t[1] - t[0], t[2] - t[1], t[3] - t[2], t[4] - t[3], t[5] - t[4], t[6] - t[5], t[7] - t[6], t[8] - t[7],
                t[8] - t[0], msb ? 2 : 1);
        break;
      case XP_UI_READER_RENDER_TIMING_TILED:
        LOG_DBG("ERS",
                "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums gray_lsb=%lums "
                "gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums",
                t[1] - t[0], t[2] - t[1], t[3] - t[2], t[4] - t[3], t[5] - t[4], t[6] - t[5], t[7] - t[6], t[8] - t[0]);
        break;
      case XP_UI_READER_RENDER_TIMING_GRAY:
        LOG_DBG("ERS",
                "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
                "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
                t[1] - t[0], t[2] - t[1], t[3] - t[2], t[4] - t[3], t[5] - t[4], t[6] - t[5], t[7] - t[6], t[8] - t[7],
                t[9] - t[0]);
        break;
      case XP_UI_READER_RENDER_TIMING_BW:
        LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", t[1] - t[0], t[2] - t[1],
                t[3] - t[2], t[4] - t[0]);
        break;
    }
  }
  uint32_t execute(const XpUiReaderRenderOp& op) {
    const auto refresh = op.mode == XP_UI_HALF ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
    switch (static_cast<XpUiReaderRenderOperation>(op.kind)) {
      case XP_UI_READER_RENDER_IMAGE_RESET:
        ImageBlock::clearRenderFailures();
        break;
      case XP_UI_READER_RENDER_PREWARM_BEGIN:
        prewarm.emplace(*renderer.getFontCacheManager());
        break;
      case XP_UI_READER_RENDER_PAGE:
        page.render(renderer, font, left, top);
        break;
      case XP_UI_READER_RENDER_STATUS:
        assert(false);  // Intercepted by the Rust session.
        break;
      case XP_UI_READER_RENDER_PREWARM_END:
        prewarm->endScanAndPrewarm();
        break;
      case XP_UI_READER_RENDER_HIGHLIGHTS:
        highlight(highlightContext, &page, font, left, top);
        break;
      case XP_UI_READER_RENDER_IMAGES:
        page.renderImages(renderer, font, left, top);
        break;
      case XP_UI_READER_RENDER_PLACEHOLDERS:
        page.renderWithImagePlaceholders(renderer, font, left, top);
        break;
      case XP_UI_READER_RENDER_CLEAR:
        renderer.clearScreen(op.value);
        break;
      case XP_UI_READER_RENDER_DISPLAY:
        renderer.displayBuffer(refresh);
        break;
      case XP_UI_READER_RENDER_DISPLAY_ASYNC:
        renderer.displayBufferAsync(refresh);
        break;
      case XP_UI_READER_RENDER_GRAY_BASE:
        renderer.displayGrayscaleBase(refresh);
        break;
      case XP_UI_READER_RENDER_ABSOLUTE_BASE:
        return renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, refresh);
      case XP_UI_READER_RENDER_PRECONDITION:
        renderer.preconditionGrayscale();
        break;
      case XP_UI_READER_RENDER_SET_COUNTER:
        assert(false);  // Intercepted by the Rust session.
        break;
      case XP_UI_READER_RENDER_PREPARE_STRIPS:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        return strips.buffers.prepare(renderer.getDisplayWidthBytes(), op.value != 0);
#else
        return false;
#endif
      case XP_UI_READER_RENDER_IMAGE_WINDOW:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        ImageBlock::setRenderReadWindow(strips.buffers.imageWindow.get(), strips.buffers.imageWindow ? 4096 : 0);
#endif
        break;
      case XP_UI_READER_RENDER_RECORD_BEGIN:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        strips.plan = {strips.buffers.commands.get(), ReaderStripBuffers::COMMAND_BYTES, 0, 0};
        renderer.usePagePlan(&strips.plan, strips.buffers.bounds.get(), page.elementCount(), true);
#endif
        break;
      case XP_UI_READER_RENDER_RECORD_END:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        renderer.usePagePlan(strips.plan.valid ? &strips.plan : nullptr, strips.buffers.bounds.get(),
                             page.elementCount());
        return strips.plan.valid;
#else
        return false;
#endif
      case XP_UI_READER_RENDER_SET_SOURCE:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        strips.source = {sizeof(XpDisplaySource),      &strips.painter,
                         strips.buffers.scratch.get(), op.value,
                         ReaderStripBuffers::ROWS,     renderer.getDisplayWidth(),
                         &ReaderStripPainter::paint};
        strips.active = display.setFrameSource(&strips.source);
        strips.buffers.setSourceLoanActive(strips.active);
        return strips.active;
#else
        return false;
#endif
      case XP_UI_READER_RENDER_CLEAR_PLAN:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        renderer.usePagePlan(nullptr, nullptr, 0);
#endif
        break;
      case XP_UI_READER_RENDER_CLEAR_WINDOW:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        ImageBlock::setRenderReadWindow(nullptr, 0);
#endif
        break;
      case XP_UI_READER_RENDER_RELEASE_STRIPS:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        strips.buffers.releaseIdle();
#endif
        break;
      case XP_UI_READER_RENDER_ALLOCATE:
        switch (static_cast<XpUiReaderRenderBuffer>(op.buffer)) {
          case XP_UI_READER_BUFFER_LSB:
            lsb = makeUniqueNoThrow<uint8_t[]>(op.value);
            return lsb != nullptr;
          case XP_UI_READER_BUFFER_MSB:
            msb = makeUniqueNoThrow<uint8_t[]>(op.value);
            return msb != nullptr;
          case XP_UI_READER_BUFFER_SCRATCH:
            scratch = makeUniqueNoThrow<uint8_t[]>(op.value);
            return scratch != nullptr;
        }
        return false;
      case XP_UI_READER_RENDER_BORROW_SCRATCH:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
        borrowedScratch = strips.buffers.scratch.get();
        return borrowedScratch != nullptr;
#else
        return false;
#endif
      case XP_UI_READER_RENDER_MODE:
        switch (op.mode) {
          case 0:
            renderer.setRenderMode(GfxRenderer::BW);
            break;
          case 1:
            renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
            break;
          case 2:
            renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
            break;
        }
        break;
      case XP_UI_READER_RENDER_STRIP_BEGIN:
        renderer.beginStripTarget(buffer(op.buffer) + op.offset, op.y, op.rows);
        break;
      case XP_UI_READER_RENDER_STRIP_END:
        renderer.endStripTarget();
        break;
      case XP_UI_READER_RENDER_WAIT:
        renderer.waitRefreshComplete();
        break;
      case XP_UI_READER_RENDER_WRITE_PLANE:
        renderer.writeGrayscalePlaneStrip(op.mode == 1, buffer(op.buffer), op.y, op.rows);
        break;
      case XP_UI_READER_RENDER_GRAY_DISPLAY:
        renderer.displayGrayBuffer();
        break;
      case XP_UI_READER_RENDER_CLEANUP:
        renderer.cleanupGrayscaleWithFrameBuffer();
        break;
      case XP_UI_READER_RENDER_STORE_BW:
        return renderer.storeBwBuffer();
      case XP_UI_READER_RENDER_RESTORE_BW:
        renderer.restoreBwBuffer();
        break;
      case XP_UI_READER_RENDER_COPY_LSB:
        renderer.copyGrayscaleLsbBuffers();
        break;
      case XP_UI_READER_RENDER_COPY_MSB:
        renderer.copyGrayscaleMsbBuffers();
        break;
      case XP_UI_READER_RENDER_LOG:
        log(op.value);
        break;
      case XP_UI_READER_RENDER_MARK:
        times[op.value] = millis();
        break;
    }
    return 0;
  }
};

struct NativeResources {
  GfxRenderer& renderer;
  XpUiReaderRenderServices renderServices;
  std::optional<FontCacheManager::PrewarmScope> prewarm;
  PageMetadata metadata;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  ReaderStripBuffers strips;
#endif
  // Page-bound physical scratch replaces the former renderContents stack context.
  std::optional<NativeRenderFrame> frame;
  explicit NativeResources(GfxRenderer& renderer)
      : renderer(renderer),
        renderServices{
            this, [](void* raw, uint8_t query) -> uint32_t { return static_cast<NativeResources*>(raw)->read(query); },
            [](void* raw, const XpUiReaderRenderOp* op) -> uint32_t {
              return static_cast<NativeResources*>(raw)->execute(*op);
            }} {}
  uint32_t read(uint8_t query) {
    switch (static_cast<XpUiReaderRenderQuery>(query)) {
      case XP_UI_READER_RENDER_NOW:
        return millis();
      case XP_UI_READER_RENDER_IS_A4:
        return BoardConfig::isEegoA4();
      case XP_UI_READER_RENDER_IS_BW:
        return renderer.getRenderMode() == GfxRenderer::BW;
      case XP_UI_READER_RENDER_FREQUENCY:
        return SETTINGS.getRefreshFrequency();
      case XP_UI_READER_RENDER_FREE_HEAP:
        return ESP.getFreeHeap();
      case XP_UI_READER_RENDER_MAX_BLOCK:
        return ESP.getMaxAllocHeap();
      default:
        assert(frame);  // Remaining queries belong to a bound Page render.
        return frame->read(query);
    }
  }
  uint32_t execute(const XpUiReaderRenderOp& op) {
    const auto refresh = op.mode == XP_UI_HALF ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
    switch (static_cast<XpUiReaderRenderOperation>(op.kind)) {
      case XP_UI_READER_RENDER_CLEAR:
        renderer.clearScreen(op.value);
        return 0;
      case XP_UI_READER_RENDER_DISPLAY:
        renderer.displayBuffer(refresh);
        return 0;
      case XP_UI_READER_RENDER_DISPLAY_ASYNC:
        renderer.displayBufferAsync(refresh);
        return 0;
      case XP_UI_READER_RENDER_CLEANUP:
        renderer.cleanupGrayscaleWithFrameBuffer();
        return 0;
      case XP_UI_READER_RENDER_STORE_BW:
        return renderer.storeBwBuffer();
      default:
        assert(frame);  // Remaining operations borrow the bound Page and scratch.
        return frame->execute(op);
    }
  }
};

NativeResources& resources(void* handle) { return *static_cast<NativeResources*>(handle); }

.page_close = [](void* handle) { delete static_cast<Page*>(handle); },
.page_info = [](void* handle, XpEpubPageInfo* out) {
            const auto& page = *static_cast<Page*>(handle);
            *out = {page.visibleTextOffset,
                    static_cast<uint16_t>(page.elementCount()),
                    static_cast<uint16_t>(page.footnoteCount()),
                    static_cast<uint16_t>(page.linkCount()),
                    page.hasImages(),
                    page.hasImagesNeedingDecode()};
          },
.page_element = [](void* handle, uint16_t index, XpEpubElement* out) {
            const auto line = static_cast<Page*>(handle)->element(index);
            *out = {};
            out->x = line.xPos;
            out->y = line.yPos;
            out->tag = line.getTag();
            if (line.getTag() == TAG_PageLine) {
              const auto block = line.textBlock();
              out->valid = block.valid();
              if (out->valid) out->words = block.wordCount();
            }
          },
.page_word = [](void* handle, uint16_t element, uint16_t index, XpEpubWord* out) {
            const auto block = static_cast<Page*>(handle)->element(element).textBlock();
            const auto range = block.wordSourceRange(index);
            *out = {text(block.wordText(index)),
                    block.wordXpos(index),
                    range.start,
                    range.end,
                    static_cast<uint8_t>(block.wordStyle(index)),
                    block.getBlockStyle().characterSpacing};
          },
.metadata_assign = [](void* handle, void* page) -> uint8_t {
        return resources(handle).metadata.assign(*static_cast<Page*>(page));
      },
.metadata_clear = [](void* handle) { resources(handle).metadata.clear(); },
.metadata_footnotes = [](void* handle) -> uint16_t { return resources(handle).metadata.footnotes().size(); },
.metadata_links = [](void* handle) -> uint16_t { return resources(handle).metadata.links().size(); },
.metadata_footnote = [](void* handle, uint16_t index, XpEpubFootnoteSink sink, void* context) -> uint8_t {
        const auto rows = resources(handle).metadata.footnotes();
        if (index >= rows.size()) return 0;
        const XpEpubFootnote view{text(rows[index].number), text(rows[index].href)};
        return sink(context, &view);
      },
.metadata_link = [](void* handle, uint16_t index, XpEpubLinkSink sink, void* context) -> uint8_t {
        const auto rows = resources(handle).metadata.links();
        if (index >= rows.size()) return 0;
        const auto& row = rows[index];
        const XpEpubLink view{text(row.href), row.x, row.y, row.width, row.height};
        return sink(context, &view);
      },
.page_word_font = [](void* handle, uint16_t element, uint16_t word, int32_t font) -> int32_t {
        const auto block = static_cast<Page*>(handle)->element(element).textBlock();
        return TextBlock::renderFontId(*target, block.wordFontId(word, font), font);
      },
.page_line_font = [](void* handle, uint16_t element, int32_t font) -> int32_t {
        const auto block = static_cast<Page*>(handle)->element(element).textBlock();
        return TextBlock::renderFontId(*target, block.blockFontId(font), font);
      },
.page_word_y_offset = [](void* handle, uint16_t element, uint16_t word, int32_t font) -> int32_t {
        const auto block = static_cast<Page*>(handle)->element(element).textBlock();
        return block.wordYOffset(*target, font, word);
      },
.page_line_ruby_shift = [](void* handle, uint16_t element, int32_t font) -> int32_t {
        const auto block = static_cast<Page*>(handle)->element(element).textBlock();
        return block.getRubyShift(target->getFontAscenderSize(font));
      },
.page_render = [](void* handle, int32_t font, int32_t left,
                        int32_t top) { static_cast<Page*>(handle)->render(*target, font, left, top); },
.page_load = [](void* file, uint32_t bytes, uint32_t visible) -> void* {
        return Page::load(*static_cast<HalFile*>(file), bytes, visible).release();
      },
.render_bind_page = [](void* handle, void* page, int32_t font, int32_t left, int32_t top, int32_t bottom,
             XpEpubLegacyHighlight highlight, void* context) {
            auto& root = resources(handle);
            assert(!root.frame && page && highlight);
            root.frame.emplace(*target, *static_cast<Page*>(page), font, left, top, bottom, highlight, context
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
                               ,
                               root.strips
#endif
            );
          },
.child_footnote = [](void* handle, int32_t left, int32_t top) -> void* {
        std::unique_ptr<Page> page(static_cast<Page*>(handle));
        auto child = makeLegacyActivity<EpubReaderFootnoteSelectActivity>(*target, *mapped, std::move(page), left, top);
        // The Rust caller retains the Page on allocation failure.
        if (!child) page.release();
        return child.release();
      },
.child_dictionary = [](void* handle, int32_t left, int32_t top, XpAppText initial) -> void* {
        std::unique_ptr<Page> page(static_cast<Page*>(handle));
        auto child = makeLegacyActivity<DictionaryWordSelectActivity>(*target, *mapped, std::move(page), left, top,
                                                                      owned(initial));
        if (!child) page.release();
        return child.release();
      },
.child_selection = [](void** handles, uint8_t count, const XpEpubSelectionArgs* args) -> void* {
        constexpr size_t pageWindow = 3;
        assert(count > 0 && count <= pageWindow);
        // The native child still owns the existing three-slot Page vector.
        std::vector<std::unique_ptr<Page>> pages;
        pages.reserve(pageWindow);
        for (uint8_t i = 0; i < count; ++i) pages.emplace_back(static_cast<Page*>(handles[i]));
        auto child = makeLegacyActivity<ClipSelectionActivity>(*target, *mapped, std::move(pages), args->left,
                                                               args->top, args->x, args->y,
                                                               static_cast<ClipSelectionActivity::Mode>(args->mode));
        if (child) {
          for (uint8_t i = 0; i < count; ++i) handles[i] = nullptr;
        } else {
          for (auto& page : pages) page.release();
        }
        return child.release();
      },
.resources_open = []() -> void* { return makeUniqueNoThrow<NativeResources>(*target).release(); },
.resources_close = [](void* handle) { delete static_cast<NativeResources*>(handle); },
.render_services = [](void* handle) -> const XpUiReaderRenderServices* {
        return &resources(handle).renderServices;
      },
.render_finish_page = [](void* handle) {
            auto& root = resources(handle);
            assert(root.frame);
            root.frame.reset();
          },
