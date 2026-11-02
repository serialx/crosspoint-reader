#include "ActivityManager.h"

#include <BlePageTurner.h>
#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalPowerManager.h>
#include <Memory.h>
#include <VectorFontSupport.h>

#include <cstring>

#include "Activity.h"
#include "ActivityResultView.h"
#include "CrossPointSettings.h"
#include "HapticFeedback.h"
#include "OpdsServerStore.h"
#include "RustAppResources.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "components/HeaderBackTapTarget.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "library/LibraryListActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "network/UsbDriveActivity.h"
#include "plugins/PluginCatalogActivity.h"
#include "reader/ReaderActivity.h"
#include "reader/RustEpubResources.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsListActivity.h"
#include "util/BmpViewerActivity.h"
#include "util/FrontlightPanelActivity.h"
#include "util/FullScreenMessageActivity.h"

static portMUX_TYPE activityManagerSpinlock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE appModelSpinlock = portMUX_INITIALIZER_UNLOCKED;

namespace {
struct OwnedLegacyActivity {
  std::unique_ptr<Activity> activity;
  uint8_t kind = UINT8_MAX;
  bool reader = false;
  bool identified = false;

  explicit OwnedLegacyActivity(std::unique_ptr<Activity> activity) : activity(std::move(activity)) {}
};

std::unique_ptr<OwnedLegacyActivity> ownLegacy(std::unique_ptr<Activity> activity) {
  if (!activity) return nullptr;
  auto owner = makeUniqueNoThrow<OwnedLegacyActivity>(std::move(activity));
  if (!owner) LOG_ERR("ACT", "Cannot allocate legacy activity owner");
  return owner;
}

Activity* nativeActivity(const void* object) { return static_cast<const OwnedLegacyActivity*>(object)->activity.get(); }

XpAppText text(const std::string& value) { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
std::string owned(XpAppText value) {
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string();
}
bool send(XpAppEvent event) { return xp_app_event(&event) == XP_APP_ACCEPTED; }
void navigate(XpAppLaunch launch) {
  XpAppEvent event{.kind = XP_APP_NAVIGATE};
  event.value.navigate = launch;
  if (!send(event)) LOG_ERR("ACT", "Cannot create activity kind=%u", launch.kind);
}
void screenshot(const ScreenshotInfo& info, XpAppScreenshotSink sink, void* context) {
  XpAppScreenshot view{.reader_type = static_cast<uint8_t>(info.readerType)};
  static_assert(sizeof(view.title) == sizeof(info.title));
  memcpy(view.title, info.title, sizeof(view.title));
  view.spine = info.spineIndex;
  view.current_page = info.currentPage;
  view.total_pages = info.totalPages;
  view.progress_percent = info.progressPercent;
  sink(context, &view);
}
}  // namespace

ActivityManager::ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : renderer(renderer), mappedInput(mappedInput), renderingMutex(xSemaphoreCreateMutex()) {
  assert(renderingMutex != nullptr && "Failed to create rendering mutex");
}
ActivityManager::~ActivityManager() { assert(false); }

void ActivityManager::begin(const XpAppRuntime& leaves) {
  static const XpAppLegacy legacy{
      .create = [](const XpAppLaunch* launch) -> void* {
        auto& renderer = activityManager.renderer;
        auto& input = activityManager.mappedInput;
        const auto create = [&]() -> std::unique_ptr<Activity> {
          switch (launch->kind) {
            case XP_APP_FILES:
              return makeUniqueNoThrow<FileBrowserActivity>(renderer, input, owned(launch->value.file_path));
            case XP_APP_LIBRARY:
              return makeUniqueNoThrow<LibraryListActivity>(renderer, input);
            case XP_APP_OPDS: {
              const auto& servers = OPDS_STORE.getServers();
              if (launch->value.opds_server >= servers.size()) return nullptr;
              return makeUniqueNoThrow<OpdsBookBrowserActivity>(renderer, input, servers[launch->value.opds_server]);
            }
            case XP_APP_OPDS_PICKER:
              return makeUniqueNoThrow<OpdsServerListActivity>(renderer, input, true);
            case XP_APP_PLUGINS:
              return makeUniqueNoThrow<PluginCatalogActivity>(renderer, input, launch->value.plugins_show_opds != 0,
                                                              true);
            case XP_APP_FILE_TRANSFER:
              return makeUniqueNoThrow<CrossPointWebServerActivity>(renderer, input,
                                                                    launch->value.transfer_join_network != 0);
            case XP_APP_USB_DRIVE:
#if FREEINK_CAP_USB_MSC
              return makeUniqueNoThrow<UsbDriveActivity>(renderer, input);
#else
              LOG_ERR("ACT", "USB Drive requested in a build without USB Drive capability");
              return nullptr;
#endif
            case XP_APP_SETTINGS:
              static_assert(static_cast<uint8_t>(SettingsSection::End) == 6);
              return makeUniqueNoThrow<SettingsListActivity>(
                  renderer, input, static_cast<SettingsSection>(launch->value.settings_section));
            case XP_APP_READER:
              return ReaderActivity::create(renderer, input, owned(launch->value.reader.path),
                                            launch->value.reader.fast_initial != 0);
            case XP_APP_BITMAP:
              return makeUniqueNoThrow<BmpViewerActivity>(renderer, input, owned(launch->value.bitmap_path));
            case XP_APP_BOOT:
              return makeUniqueNoThrow<BootActivity>(renderer, input);
            case XP_APP_SLEEP:
              return makeUniqueNoThrow<SleepActivity>(renderer, input, launch->value.sleep_from_timeout != 0);
            case XP_APP_MESSAGE:
              return makeUniqueNoThrow<FullScreenMessageActivity>(
                  renderer, input, owned(launch->value.message.text),
                  static_cast<EpdFontFamily::Style>(launch->value.message.style));
            case XP_APP_CRASH:
              return makeUniqueNoThrow<CrashActivity>(renderer, input);
            case XP_APP_FRONTLIGHT_PANEL:
              return makeUniqueNoThrow<FrontlightPanelActivity>(renderer, input);
            default:
              return nullptr;
          }
        };
        return ownLegacy(create()).release();
      },
      .destroy = [](void* activity) { delete static_cast<OwnedLegacyActivity*>(activity); },
      .enter = [](void* activity) { nativeActivity(activity)->onEnter(); },
      .loop = [](void* activity) { nativeActivity(activity)->loop(); },
      .exit = [](void* activity) { nativeActivity(activity)->onExit(); },
      .render =
          [](void* activity, XpAppRenderToken* token) {
            nativeActivity(activity)->render(std::move(*static_cast<RenderLock*>(token->lock)));
          },
      .facts = [](const void* object, uint8_t delay, uint8_t input) -> XpAppLegacyFacts {
        auto& owner = *const_cast<OwnedLegacyActivity*>(static_cast<const OwnedLegacyActivity*>(object));
        auto* activity = owner.activity.get();
        // Adoption reads facts before publication; identity then stays immutable.
        if (!owner.identified) {
          const auto& name = activity->name;
          if (name == "FileBrowser")
            owner.kind = XP_APP_FILES;
          else if (name == "Library")
            owner.kind = XP_APP_LIBRARY;
          else if (name == "OpdsBookBrowser")
            owner.kind = XP_APP_OPDS;
          else if (name == "PluginCatalog")
            owner.kind = XP_APP_PLUGINS;
          else if (name == "CrossPointWebServer")
            owner.kind = XP_APP_FILE_TRANSFER;
          else if (name == "Settings" || name == "SettingsList")
            owner.kind = XP_APP_SETTINGS;
          else if (name == "NetworkModeSelection")
            owner.kind = XP_APP_NETWORK_MODE;
          else if (name == "FrontlightPanel")
            owner.kind = XP_APP_FRONTLIGHT_PANEL;
          owner.reader = activity->isReaderActivity();

          owner.identified = true;
        }
        return {owner.kind,
                static_cast<uint8_t>(owner.reader),
                static_cast<uint8_t>(activity->acceptsRemoteInput()),
                static_cast<uint8_t>(activity->preventAutoSleep()),
                static_cast<uint8_t>(activity->requiresExclusiveStorageLoop()),
                static_cast<uint8_t>(delay && activity->skipLoopDelay()),
                static_cast<uint8_t>(input && owner.reader && static_cast<ReaderActivity*>(activity)->inputOverPage())};
      },
      .with_result =
          [](void* activity, XpAppResultSink sink, void* context) {
            static_assert(std::is_nothrow_move_constructible_v<ActivityResult>);
            // The continuation destroys the child before delivering this stack-owned result.
            auto result = std::move(nativeActivity(activity)->result);
            const auto view = activityResultView(result);
            sink(context, &view);
          },
      .deliver_result =
          [](void* parent, const void* result) {
            auto* activity = nativeActivity(parent);
            auto handler = std::move(activity->resultHandler);
            activity->resultHandler = nullptr;
            if (handler) {
              if (result) {
                handler(*static_cast<const ActivityResult*>(result));
              } else {
                handler(ActivityResult{});
              }
            }
          },
      .prepare_sleep = [](void* activity) { nativeActivity(activity)->prepareForSleep(); },
      .home_gesture = [](void* activity) -> uint8_t { return nativeActivity(activity)->handleHomeGesture(); },
      .forced_refresh = [](void* activity) -> uint8_t { return nativeActivity(activity)->handleForcedRefresh(); },
      .reader_turn = [](void* activity, uint8_t forward, uint8_t chapter) -> uint8_t {
        if (activityManager.mappedInput.hasRemoteClick()) return 0;
        auto* reader = static_cast<ReaderActivity*>(nativeActivity(activity));
        return chapter ? reader->skipPages(forward ? 1 : -1) : reader->pageTurn(forward != 0);
      },
      .screenshot_info =
          [](const void* activity, XpAppScreenshotSink sink, void* context) {
            screenshot(nativeActivity(activity)->getScreenshotInfo(), sink, context);
          },
  };
  static XpAppRuntime runtime = leaves;
  runtime.render_lock = []() -> void* {
    const auto mutex = activityManager.renderingMutex;
    const bool locked = xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE;
    assert(locked && "Blocking render lock acquisition failed");
    return mutex;
  };
  runtime.render_try_lock = []() -> void* {
    const auto mutex = activityManager.renderingMutex;
    return xSemaphoreTake(mutex, 0) == pdTRUE ? mutex : nullptr;
  };
  runtime.render_unlock = [](void* lock) { xSemaphoreGive(static_cast<SemaphoreHandle_t>(lock)); };
  runtime.model_lock = [] { taskENTER_CRITICAL(&appModelSpinlock); };
  runtime.model_unlock = [] { taskEXIT_CRITICAL(&appModelSpinlock); };
  runtime.notify_render = [] {
    if (activityManager.renderTaskHandle) xTaskNotify(activityManager.renderTaskHandle, 1, eIncrement);
  };
  const XpAppPlatform platform{&legacy, &rustAppResources(renderer, mappedInput), &runtime,
                               &rustEpubResources(renderer, mappedInput)};
  const auto status = xp_app_init(&platform, &compatibility);
  assert(status == XP_APP_OK && "Failed to initialize Rust application");
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif
#if CROSSPOINT_VECTOR_FONTS
  // FreeType rasterization runs on this task, and the deepest observed chain
  // is a glyph fault DURING LAYOUT: expat + parser + line-layout frames
  // (~3.5KB on Xtensa) with the scan converter's FT_RENDER_POOL_SIZE (4KB)
  // stack-resident band pool on top — a measured ~8KB peak that trips the
  // canary on an 8KB stack. Vector-font boards all have PSRAM-class RAM.
  constexpr uint32_t renderTaskStackBytes = 16384;
#else
  constexpr uint32_t renderTaskStackBytes = 8192;
#endif
  xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender",
                          renderTaskStackBytes,  // Stack size
                          this,                  // Parameters
                          1,                     // Priority
                          &renderTaskHandle,     // Task handle
                          renderTaskCore  // Keep long renders/cover decodes off CPU 0's idle watchdog when available
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) { static_cast<ActivityManager*>(param)->renderTaskLoop(); }
void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    RenderLock lock;
    HalPowerManager::Lock powerLock;
    display.setInverted(SETTINGS.screenInverted != 0);
    XpAppRenderToken token{&lock};
    xp_app_render(&token);
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&activityManagerSpinlock);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    taskEXIT_CRITICAL(&activityManagerSpinlock);
    if (waiter) xTaskNotify(waiter, 1, eIncrement);
  }
}

void ActivityManager::loop() { xp_app_loop(); }
void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& activity) {
  auto owner = ownLegacy(std::move(activity));
  if (!owner) return;
  XpAppEvent event{.kind = XP_APP_ADOPT_REPLACE};
  event.value.adopt_replace = owner.release();
  if (!send(event)) delete static_cast<OwnedLegacyActivity*>(event.value.adopt_replace);
}
void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  auto owner = ownLegacy(std::move(activity));
  if (!owner) return;
  XpAppEvent event{.kind = XP_APP_ADOPT_PUSH};
  event.value.adopt_push = owner.release();
  if (!send(event)) delete static_cast<OwnedLegacyActivity*>(event.value.adopt_push);
}
void ActivityManager::popActivity() { send({.kind = XP_APP_POP}); }
void ActivityManager::goToFileTransfer() { navigate({.kind = XP_APP_FILE_TRANSFER}); }
void ActivityManager::goToJoinNetwork() {
  XpAppLaunch launch{.kind = XP_APP_FILE_TRANSFER};
  launch.value.transfer_join_network = 1;
  navigate(launch);
}
void ActivityManager::goToUsbDrive() { navigate({.kind = XP_APP_USB_DRIVE}); }
void ActivityManager::goToSettings(SettingsSection section) {
  XpAppLaunch launch{.kind = XP_APP_SETTINGS};
  launch.value.settings_section = static_cast<uint8_t>(section);
  navigate(launch);
}
void ActivityManager::goToFileBrowser(std::string path) {
  XpAppLaunch launch{.kind = XP_APP_FILES};
  launch.value.file_path = text(path);
  navigate(launch);
}
void ActivityManager::goToLibrary() { navigate({.kind = XP_APP_LIBRARY}); }
void ActivityManager::goToBrowser() { navigate({.kind = XP_APP_OPDS_PICKER}); }
void ActivityManager::goToPlugins(bool showOpds) {
  XpAppLaunch launch{.kind = XP_APP_PLUGINS};
  launch.value.plugins_show_opds = showOpds;
  navigate(launch);
}
void ActivityManager::goToReader(std::string path, bool fastInitial) {
  XpAppLaunch launch{.kind = XP_APP_READER};
  launch.value.reader = {text(path), static_cast<uint8_t>(fastInitial)};
  navigate(launch);
}
void ActivityManager::goToSleep(bool fromTimeout) {
  XpAppEvent event{.kind = XP_APP_SLEEP_REQUESTED};
  event.value.sleep_from_timeout = fromTimeout;
  send(event);
}
void ActivityManager::goToBoot() { navigate({.kind = XP_APP_BOOT}); }
void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  XpAppLaunch launch{.kind = XP_APP_MESSAGE};
  launch.value.message = {text(message), static_cast<uint8_t>(style)};
  navigate(launch);
}
void ActivityManager::goToCrashReport() { navigate({.kind = XP_APP_CRASH}); }
void ActivityManager::goHome(HomeMenuItem menu, bool clean) {
  XpAppEvent event{.kind = XP_APP_HOME_REQUESTED};
  event.value.home = {static_cast<uint8_t>(menu), static_cast<uint8_t>(clean)};
  if (!send(event)) LOG_ERR("ACT", "OOM: Home activity");
}
bool ActivityManager::preventAutoSleep() const { return facts().prevent_sleep != 0; }
bool ActivityManager::requiresExclusiveStorageLoop() const { return facts().exclusive_storage != 0; }
bool ActivityManager::isReaderActivity() const { return facts().reader_in_stack != 0; }
bool ActivityManager::isForegroundReader() const { return facts().foreground_reader != 0; }
bool ActivityManager::isRemoteReaderContext() const { return facts().remote_context != 0; }
bool ActivityManager::isForegroundHomeShown() const { return facts().foreground_home_shown != 0; }
HomeButtonAction ActivityManager::homeButtonAction() const {
  return compatibility.home_action ? static_cast<HomeButtonAction>(compatibility.home_action())
                                   : HomeButtonAction::Ignore;
}
bool ActivityManager::remoteClick(RemoteButton button) {
  XpAppEvent event{.kind = XP_APP_REMOTE_CLICK};
  event.value.remote_button = static_cast<uint8_t>(button);
  return send(event);
}
bool ActivityManager::remoteTurn(bool forward, bool chapter) {
  XpAppEvent event{.kind = XP_APP_REMOTE_TURN};
  event.value.remote_turn = {static_cast<uint8_t>(forward), static_cast<uint8_t>(chapter)};
  return send(event);
}
bool ActivityManager::remoteShortcut(bool menu) {
  XpAppEvent event{.kind = XP_APP_REMOTE_SHORTCUT};
  event.value.reader_menu = menu;
  return send(event);
}
bool ActivityManager::handleForcedRefresh() { return send({.kind = XP_APP_FORCED_REFRESH}); }
ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  ScreenshotInfo info;
  if (compatibility.screenshot_info)
    compatibility.screenshot_info(
        [](void* context, const XpAppScreenshot* view) {
          auto& info = *static_cast<ScreenshotInfo*>(context);
          info.readerType = static_cast<ScreenshotInfo::ReaderType>(view->reader_type);
          memcpy(info.title, view->title, sizeof(info.title));
          info.spineIndex = view->spine;
          info.currentPage = view->current_page;
          info.totalPages = view->total_pages;
          info.progressPercent = view->progress_percent;
        },
        &info);
  return info;
}
void ActivityManager::prepareForSleep() { send({.kind = XP_APP_PREPARE_SLEEP}); }
void ActivityManager::requestUpdate(bool immediate) {
  XpAppEvent event{.kind = XP_APP_RENDER_REQUESTED};
  event.value.render_immediate = immediate;
  send(event);
}
void ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return;
  }

  // Atomic section to perform checks
  taskENTER_CRITICAL(&activityManagerSpinlock);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&activityManagerSpinlock);

  // Render task cannot call requestUpdateAndWait() or it will cause a deadlock
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");

  // There should never be the case where 2 tasks are waiting for a render at the same time
  assert(!alreadyWaiting && "Already waiting for a render to complete");

  // Cannot call while holding RenderLock or it will cause a deadlock
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");

  xTaskNotify(renderTaskHandle, 1, eIncrement);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

// RenderLock

RenderLock::RenderLock(Mode mode) {
  isLocked = xSemaphoreTake(activityManager.renderingMutex, mode == Mode::Try ? 0 : portMAX_DELAY) == pdTRUE;
  assert((mode == Mode::Try || isLocked) && "Blocking render lock acquisition failed");
}

RenderLock::RenderLock(Activity&) : RenderLock(Mode::Blocking) {}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

/**
 *
 * Checks if renderingMutex is busy.
 *
 * @return true if renderingMutex is busy, otherwise false.
 *
 */
bool RenderLock::peek() { return xQueuePeek(activityManager.renderingMutex, NULL, 0) != pdTRUE; };
