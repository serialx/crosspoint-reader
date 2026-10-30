#include "ReaderActivity.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <KOReaderDocumentId.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <cstdio>
#include <optional>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "XtcReaderActivity.h"
#include "util/PluginEvents.h"

static_assert(static_cast<int>(EndOfBookOptions::Action::Redraw) == XP_UI_READER_VALUE_END_REDRAW);
static_assert(static_cast<int>(EndOfBookOptions::Action::OpenBook) == XP_UI_READER_VALUE_END_OPEN_BOOK);
static_assert(static_cast<int>(EndOfBookOptions::Action::GoHome) == XP_UI_READER_VALUE_END_GO_HOME);
static_assert(static_cast<int>(EndOfBookOptions::Action::LastPage) == XP_UI_READER_VALUE_END_LAST_PAGE);
static_assert(static_cast<int>(CrossPointSettings::CHAPTER_SKIP) == XP_UI_READER_VALUE_LONG_BUTTON_SKIP);
static_assert(static_cast<int>(CrossPointSettings::ORIENTATION_CHANGE) == XP_UI_READER_VALUE_LONG_BUTTON_ORIENTATION);

ReaderActivity::ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::string bookPath, const bool allowFastInitialRefresh)
    : RustActivityAdapter(name, renderer, mappedInput), bookPath(std::move(bookPath)) {
  xp_ui_reader_base_init(&baseModel, allowFastInitialRefresh,
                         allowFastInitialRefresh ? SETTINGS.getRefreshFrequency() : 0);
}

std::unique_ptr<ReaderActivity> ReaderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string path, const bool allowFastInitialRefresh) {
  // ActivityManager requires heap ownership; each branch allocates exactly one screen-lifetime object.
  std::unique_ptr<ReaderActivity> activity;
  if (FsHelpers::hasXtcExtension(path)) {
    activity = makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else {
    activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  }

  if (!activity) {
    LOG_ERR("READER", "OOM: reader activity");
  }
  return activity;
}

void ReaderActivity::applyInitialOrientation() { ReaderUtils::applyOrientation(renderer, SETTINGS.orientation); }

void ReaderActivity::disableFastInitialRefresh() { setRefreshCounter(0); }

void ReaderActivity::notePageTurn(const bool forward, const bool succeeded) {
  ModelLock lock(*this);
  xp_ui_reader_session_note(&baseModel.session, forward, succeeded);
}

void ReaderActivity::prepareForSleep() { flushReaderSession(); }

bool ReaderActivity::handleBackNavigation() {
  return ReaderUtils::handleBackNavigation(mappedInput, activityManager, bookPath.c_str(),
                                           {this, [](void* ctx) { static_cast<ReaderActivity*>(ctx)->onGoHome(); }});
}

uint8_t ReaderActivity::runBase(uint8_t operation, uint8_t a, uint8_t b) {
  struct Context {
    ReaderActivity& o;
    uint8_t operation;
    struct Resources {
      std::optional<RenderLock> lock;
      std::optional<std::string> openPath, document;
      ReaderSession session;
    };
    Resources* resources = nullptr;
    Resources& nonRenderResources() {
      assert(resources);
      return *resources;
    }
    bool exists() {
      if (Storage.exists(o.bookPath.c_str())) return true;
      LOG_ERR("READER", "File does not exist: %s", o.bookPath.c_str());
      return false;
    }
    uint32_t read(uint8_t field) { return o.readBaseField(field); }
    void write(uint8_t field, uint32_t value) {
      o.writeBaseField(field, value, operation == XP_UI_READER_BASE_OPERATION_RENDER);
    }
    uint32_t query(uint8_t query) {
      switch (static_cast<XpUiReaderBaseQuery>(query)) {
        case XP_UI_READER_BASE_QUERY_AT_END:
          return o.isAtEndOfBook();
        case XP_UI_READER_BASE_QUERY_HAS_END:
          return bool(o.endOfBookOptions);
        case XP_UI_READER_BASE_QUERY_END_MENU:
          return o.endOfBookOptions->menuActive();
        case XP_UI_READER_BASE_QUERY_EXISTS:
          return exists();
        case XP_UI_READER_BASE_QUERY_OPEN_PATH_EMPTY:
          return APP_STATE.openEpubPath.empty();
        case XP_UI_READER_BASE_QUERY_SUBSCRIBER_EXIT:
          return pluginevents::anySubscriber(pluginevents::Event::ReaderExit);
        case XP_UI_READER_BASE_QUERY_SESSION_WORTHY:
          return (nonRenderResources().session = o.snapshotReaderSession(),
                  nonRenderResources().session.isEmitWorthy());
        case XP_UI_READER_BASE_QUERY_SUBSCRIBER_SESSION:
          return pluginevents::anySubscriber(pluginevents::Event::ReaderSession);
        case XP_UI_READER_BASE_QUERY_HELD:
          return o.mappedInput.getHeldTime();
        case XP_UI_READER_BASE_QUERY_LONG_BUTTON:
          return SETTINGS.longPressButtonBehavior;
      }
      return 0;
    }
    // Keep event formatting buffers off the page-render call chain.
    __attribute__((noinline)) void emitSession() {
      auto& resources = nonRenderResources();
      auto& session = resources.session;
      char startTime[24], endTime[24], duration[16], startProgress[8], endProgress[8];
      snprintf(startTime, sizeof(startTime), "%lld", static_cast<long long>(session.startTime()));
      snprintf(endTime, sizeof(endTime), "%lld", static_cast<long long>(session.endTime()));
      snprintf(duration, sizeof(duration), "%lu", static_cast<unsigned long>(session.durationSeconds()));
      snprintf(startProgress, sizeof(startProgress), "%u", session.startProgressBp());
      snprintf(endProgress, sizeof(endProgress), "%u", session.endProgressBp());
      const pluginevents::Var vars[] = {{"book", o.bookPath.c_str()},     {"document", resources.document->c_str()},
                                        {"start_time", startTime},        {"end_time", endTime},
                                        {"duration_seconds", duration},   {"start_progress_bp", startProgress},
                                        {"end_progress_bp", endProgress}, {"progress_scale", "10000"}};
      pluginevents::emit(pluginevents::Event::ReaderSession, vars, 8);
    }
    __attribute__((noinline)) void emitExit() {
      char percent[8];
      snprintf(percent, sizeof(percent), "%d", o.getScreenshotInfo().progressPercent);
      const pluginevents::Var vars[] = {{"book", o.bookPath.c_str()}, {"percent", percent}};
      pluginevents::emit(pluginevents::Event::ReaderExit, vars, 2);
    }
    // Metadata temporaries must not enlarge the nested page-render dispatcher.
    [[gnu::noinline]] void addRecentBook() {
      RECENT_BOOKS.addBook(o.bookPath, o.getBookTitle(), o.getBookAuthor(), o.getBookThumbBmpPath());
    }
    [[gnu::noinline]] uint32_t calculateDocument() {
      nonRenderResources().document.emplace(KOReaderDocumentId::calculate(o.bookPath));
      return nonRenderResources().document->size();
    }
    [[gnu::noinline]] void saveState() { APP_STATE.saveToFile(); }
    [[gnu::noinline]] void rememberPath() { APP_STATE.openEpubPath = o.bookPath; }
    [[gnu::noinline]] void openNext() { activityManager.goToReader(*nonRenderResources().openPath); }
    uint32_t command(uint8_t cmd, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderBaseCommand>(cmd)) {
        case XP_UI_READER_BASE_COMMAND_FORCE_REFRESH:
          o.queueForcedRefresh();
          break;
        case XP_UI_READER_BASE_COMMAND_UPDATE:
          o.requestUpdate();
          break;
        case XP_UI_READER_BASE_COMMAND_FINISH:
          o.finish();
          break;
        case XP_UI_READER_BASE_COMMAND_BASE_ENTER:
          o.Activity::onEnter();
          break;
        case XP_UI_READER_BASE_COMMAND_BASE_EXIT:
          o.Activity::onExit();
          break;
        case XP_UI_READER_BASE_COMMAND_LOG_ENTER:
          LOG_INF("MEM", "reader enter: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
          break;
        case XP_UI_READER_BASE_COMMAND_LOG_EXIT:
          LOG_INF("MEM", "reader exit: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
          break;
        case XP_UI_READER_BASE_COMMAND_CLEAR_PATH:
          APP_STATE.openEpubPath.clear();
          break;
        case XP_UI_READER_BASE_COMMAND_SAVE_STATE:
          saveState();
          break;
        case XP_UI_READER_BASE_COMMAND_ENSURE_FONTS:
          sdFontSystem.ensureLoaded(o.renderer);
          break;
        case XP_UI_READER_BASE_COMMAND_ORIENTATION_INITIAL:
          o.applyInitialOrientation();
          break;
        case XP_UI_READER_BASE_COMMAND_LOAD_BOOK:
          return o.loadBook();
          break;
        case XP_UI_READER_BASE_COMMAND_LOAD_FAILURE:
          return o.handleLoadFailure();
          break;
        case XP_UI_READER_BASE_COMMAND_REMEMBER_PATH:
          rememberPath();
          break;
        case XP_UI_READER_BASE_COMMAND_RECENT_ADD:
          addRecentBook();
          break;
        case XP_UI_READER_BASE_COMMAND_EMIT_OPEN: {
          const pluginevents::Var vars[] = {{"book", o.bookPath.c_str()}};
          pluginevents::emit(pluginevents::Event::ReaderOpen, vars, 1);
        } break;
        case XP_UI_READER_BASE_COMMAND_RELEASE_FONTS:
          if (auto* cache = o.renderer.getFontCacheManager()) cache->releaseSdFontCaches();
          break;
        case XP_UI_READER_BASE_COMMAND_SESSION_RESET:
          o.resetReaderSession();
          break;
        case XP_UI_READER_BASE_COMMAND_DOCUMENT:
          return calculateDocument();
          break;
        case XP_UI_READER_BASE_COMMAND_EMIT_SESSION:
          emitSession();
          break;
        case XP_UI_READER_BASE_COMMAND_EMIT_EXIT:
          emitExit();
          break;
        case XP_UI_READER_BASE_COMMAND_ORIENTATION_PORTRAIT:
          o.renderer.setOrientation(GfxRenderer::Orientation::Portrait);
          break;
        case XP_UI_READER_BASE_COMMAND_RESET_LOAD_COUNT:
          APP_STATE.readerActivityLoadCount = 0;
          break;
        case XP_UI_READER_BASE_COMMAND_END_DROP:
          o.endOfBookOptions.reset();
          break;
        case XP_UI_READER_BASE_COMMAND_END_ALLOCATE:
          o.endOfBookOptions = makeUniqueNoThrow<EndOfBookOptions>(o.renderer);
          break;
        case XP_UI_READER_BASE_COMMAND_END_OOM:
          LOG_ERR("READER", "OOM: EndOfBookOptions");
          break;
        case XP_UI_READER_BASE_COMMAND_END_LOAD:
          o.endOfBookOptions->loadOnce(o.bookPath);
          break;
        case XP_UI_READER_BASE_COMMAND_END_RENDER:
          o.endOfBookOptions->render(o.renderer, o.mappedInput);
          break;
        case XP_UI_READER_BASE_COMMAND_END_INPUT:
          nonRenderResources().openPath.emplace();
          return static_cast<uint32_t>(
              o.endOfBookOptions->handleMenuInput(o.mappedInput, &*nonRenderResources().openPath));
          break;
        case XP_UI_READER_BASE_COMMAND_OPEN_NEXT:
          openNext();
          break;
        case XP_UI_READER_BASE_COMMAND_HOME:
          o.onGoHome();
          break;
        case XP_UI_READER_BASE_COMMAND_RETURN_END:
          o.onReturnFromEndOfBook();
          break;
        case XP_UI_READER_BASE_COMMAND_FORMAT_INPUT:
          return o.handleFormatInput();
          break;
        case XP_UI_READER_BASE_COMMAND_BACK:
          return o.handleBackNavigation();
          break;
        case XP_UI_READER_BASE_COMMAND_TURN:
          return o.pageTurn(a != 0);
          break;
        case XP_UI_READER_BASE_COMMAND_SKIP:
          return o.skipPages(a);
          break;
        case XP_UI_READER_BASE_COMMAND_NOTE_TURN:
          o.notePageTurn(a != 0, b != 0);
          break;
        case XP_UI_READER_BASE_COMMAND_HAPTIC:
          haptic_feedback::touchAction(a != 0);
          break;
        case XP_UI_READER_BASE_COMMAND_CLEAR_SCREEN:
          o.renderer.clearScreen();
          break;
        case XP_UI_READER_BASE_COMMAND_DISPLAY:
          o.renderer.displayBuffer();
          break;
        case XP_UI_READER_BASE_COMMAND_END_RENDERED:
          o.onEndOfBookRendered();
          break;
        case XP_UI_READER_BASE_COMMAND_MARK_RENDERED:
          o.markPageRendered();
          break;
        case XP_UI_READER_BASE_COMMAND_SESSION_RENDER:
          o.completeReaderSession();
          break;
        case XP_UI_READER_BASE_COMMAND_RENDER_BOOK:
          o.renderBook();
          break;
        case XP_UI_READER_BASE_COMMAND_LOCK:
          assert(!nonRenderResources().lock);
          nonRenderResources().lock.emplace(o);
          break;
        case XP_UI_READER_BASE_COMMAND_UNLOCK:
          assert(nonRenderResources().lock);
          nonRenderResources().lock.reset();
          break;
        case XP_UI_READER_BASE_COMMAND_PATH_DROP:
          nonRenderResources().openPath.reset();
          break;
        case XP_UI_READER_BASE_COMMAND_DOCUMENT_DROP:
          nonRenderResources().document.reset();
          break;
        case XP_UI_READER_BASE_COMMAND_DOCUMENT_BYTE:
          return static_cast<unsigned char>((*nonRenderResources().document)[a]);
          break;
      }
      return 0;
    }
    [[gnu::always_inline]] uint8_t run(uint8_t a, uint8_t b) {
      const XpUiReaderBaseServices services{
          this,
          [](void* p, uint8_t f) { return static_cast<Context*>(p)->read(f); },
          [](void* p, uint8_t f, uint32_t v) { static_cast<Context*>(p)->write(f, v); },
          [](void* p, uint8_t q) { return static_cast<Context*>(p)->query(q); },
          [](void* p, uint8_t c, int32_t a, int32_t b) { return static_cast<Context*>(p)->command(c, a, b); },
          [](void* p) -> XpUiReaderTurnInput {
            auto& o = static_cast<Context*>(p)->o;
            auto t = ReaderUtils::detectTouchPageTurn(o.renderer, o.mappedInput);
            return {static_cast<uint32_t>(t.heldMs), t.prev, t.next, 0, 0};
          },
          [](void* p) -> XpUiReaderTurnInput {
            auto& o = static_cast<Context*>(p)->o;
            auto [prev, next, tilt] = ReaderUtils::detectPageTurn(o.mappedInput);
            return {0, prev, next, tilt, 0};
          }};
      return xp_ui_reader_base_run(&services, operation, a, b);
    }
    // Render uses none of these lifecycle/input resources. Keep their storage
    // in a separate frame so it is absent throughout nested page drawing.
    [[gnu::noinline]] uint8_t runWithResources(uint8_t a, uint8_t b) {
      Resources storage;
      resources = &storage;
      const auto result = run(a, b);
      assert(!storage.lock);
      resources = nullptr;
      return result;
    }
  } context{*this, operation};
  return operation == XP_UI_READER_BASE_OPERATION_RENDER ? context.run(a, b) : context.runWithResources(a, b);
}
void ReaderActivity::onEnter() { runBase(XP_UI_READER_BASE_OPERATION_ENTER, 0, 0); }
void ReaderActivity::onExit() { runBase(XP_UI_READER_BASE_OPERATION_EXIT, 0, 0); }
void ReaderActivity::rememberBookOnceRendered() { runBase(XP_UI_READER_BASE_OPERATION_REMEMBER, 0, 0); }
void ReaderActivity::clearEndOfBookOptionsIfNeeded() { runBase(XP_UI_READER_BASE_OPERATION_CLEAR_END, 0, 0); }
bool ReaderActivity::handleEndOfBookMenu(const bool suppress) {
  return runBase(XP_UI_READER_BASE_OPERATION_MENU, suppress, 0);
}
bool ReaderActivity::handleEndOfBookPageTurn(const bool prev, const bool next) {
  return runBase(XP_UI_READER_BASE_OPERATION_END_TURN, prev, next);
}
void ReaderActivity::loop() { runBase(XP_UI_READER_BASE_OPERATION_LOOP, 0, 0); }
void ReaderActivity::render(RenderLock&&) {
  assert(!renderFrame);
  XpUiReaderBaseModel frame{};
  {
    ModelLock lock(*this);
    xp_ui_reader_base_begin_render(&baseModel, &frame);
  }
  renderFrame = &frame;
  runBase(XP_UI_READER_BASE_OPERATION_RENDER, 0, 0);
  {
    ModelLock lock(*this);
    xp_ui_reader_base_publish(&baseModel, &frame);
  }
  renderFrame = nullptr;
}
bool ReaderActivity::handleForcedRefresh() { return runBase(XP_UI_READER_BASE_OPERATION_FORCE_REFRESH, 0, 0); }
void ReaderActivity::flushReaderSession() { runBase(XP_UI_READER_BASE_OPERATION_FLUSH, 0, 0); }
bool ReaderActivity::endOfBookMenuActive() const {
  return const_cast<ReaderActivity*>(this)->runBase(XP_UI_READER_BASE_OPERATION_MENU_ACTIVE, 0, 0);
}

uint32_t ReaderActivity::readBaseField(const uint8_t field) const {
  ModelLock lock(const_cast<ReaderActivity&>(*this));
  return xp_ui_reader_base_model_read(&baseModel, field);
}

void ReaderActivity::writeBaseField(const uint8_t field, const uint32_t value, const bool fromRender) {
  ModelLock lock(*this);
  xp_ui_reader_base_model_write(&baseModel, field, value);
  if (fromRender) {
    assert(renderFrame);
    xp_ui_reader_base_model_write(renderFrame, field, value);
  }
}

ReaderSession ReaderActivity::snapshotReaderSession() const {
  ModelLock lock(const_cast<ReaderActivity&>(*this));
  return ReaderSession(baseModel.session);
}

void ReaderActivity::resetReaderSession() {
  // Exit and prepareForSleep already run under ActivityManager's resource lock.
  ModelLock lock(*this);
  baseModel.session = {};
}

void ReaderActivity::completeReaderSession() {
  assert(renderFrame);
  xp_ui_reader_session_render(&renderFrame->session, millis(), trustedtime::trustedNow(), getProgressBasisPoints());
}

void ReaderActivity::queueForcedRefresh() {
  ModelLock lock(*this);
  xp_ui_reader_base_force_refresh(&baseModel);
}

void ReaderActivity::setRefreshCounter(const int32_t value) {
  ModelLock lock(*this);
  xp_ui_reader_base_model_write(&baseModel, XP_UI_READER_BASE_FIELD_COUNTER, static_cast<uint32_t>(value));
}

int32_t& ReaderActivity::renderRefreshCounter() {
  assert(renderFrame);
  return renderFrame->pages_until_full_refresh;
}

bool ReaderActivity::takeForcedRefresh() {
  assert(renderFrame);
  const bool forced = xp_ui_reader_base_model_read(renderFrame, XP_UI_READER_BASE_FIELD_FORCED);
  xp_ui_reader_base_model_write(renderFrame, XP_UI_READER_BASE_FIELD_FORCED, 0);
  return forced;
}

void ReaderActivity::markPageRendered() { writeBaseField(XP_UI_READER_BASE_FIELD_RENDERED, 1, true); }

bool ReaderActivity::acceptsRemoteInput() const { return readBaseField(XP_UI_READER_BASE_FIELD_RENDERED) != 0; }
