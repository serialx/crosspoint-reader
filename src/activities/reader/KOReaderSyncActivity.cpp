#include "KOReaderSyncActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cassert>

#include "Epub/Section.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "MappedInputManager.h"
#include "ProgressComparison.h"
#include "ReaderUtils.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "clippings/ClippingSync.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/WifiPowerSaveGuard.h"
#include "util/PluginEvents.h"
#include "util/PluginHttp.h"

namespace {
std::string calculateDocumentHashForMethod(const std::string& path, const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? KOReaderDocumentId::calculateFromFilename(path)
                                                 : KOReaderDocumentId::calculate(path);
}

DocumentMatchMethod alternateMatchMethod(const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
}

const char* matchMethodName(const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? "filename" : "binary";
}

}  // namespace

KOReaderSyncActivity::KOReaderSyncActivity(GfxRenderer& renderer, MappedInputManager& input, const std::string& path,
                                           CrossPointPosition position, SavedProgressPosition koPosition,
                                           std::string chapter, bool plugin)
    : KOReaderUiActivity("KOReaderSync", renderer, input, XP_KO_SYNC),
      epubPath(path),
      localChapterName(std::move(chapter)),
      localPosition(position),
      localProgress(std::move(koPosition)) {
  model.plugin = plugin;
}

void KOReaderSyncActivity::ensureEpubLoaded() {
  if (epub) return;
  LOG_DBG("KOSync", "Loading epub for progress mapping (heap: %u)", (unsigned)ESP.getFreeHeap());
  auto loaded = std::make_shared<Epub>(epubPath, "/.crosspoint");
  loaded->setupCacheDir();
  if (!loaded->load(false, true)) {
    LOG_ERR("KOSync", "Failed to load epub for progress mapping");
    return;
  }
  {
    ModelLock lock(*this);
    epub.swap(loaded);
  }
  LOG_DBG("KOSync", "Epub loaded (heap: %u)", (unsigned)ESP.getFreeHeap());
}
void KOReaderSyncActivity::releaseEpub() {
  std::shared_ptr<Epub> released;
  {
    ModelLock lock(*this);
    released.swap(epub);
  }
}
XpKoOptions KOReaderSyncActivity::options() const {
  return {localProgress.percentage,
          static_cast<uint8_t>(KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART),
          KOREADER_STORE.getSyncClippings(),
          KOREADER_STORE.supportsRichProgress(),
          KOREADER_STORE.getSendMetadata(),
          KOREADER_STORE.supportsExtendedMetadata(),
          0,
          {0, 0}};
}
void KOReaderSyncActivity::returnToReader() { activityManager.goToReader(epubPath); }
void KOReaderSyncActivity::performSync() {
  RunGuard guard(runActive);
  if (!guard) return;
  WifiPowerSaveGuard psGuard;
  const auto config = options();
  const XpKoServices services{this, &KOReaderSyncActivity::service};
  xp_ui_koreader_sync(&config, &services);
  KOReaderProgress released = std::move(scratch);
}
void KOReaderSyncActivity::performUpload() {
  RunGuard guard(runActive);
  if (!guard) return;
  const auto config = options();
  const XpKoServices services{this, &KOReaderSyncActivity::service};
  xp_ui_koreader_upload(&config, &services);
  KOReaderProgress released = std::move(scratch);
}
void KOReaderSyncActivity::startUpload() {
  if (documentHash.empty()) documentHash = calculateDocumentHashForMethod(epubPath, KOREADER_STORE.getMatchMethod());
  performUpload();
}
void KOReaderSyncActivity::onWifiSelectionComplete(bool success) {
  if (!success) {
    LOG_DBG("KOSync", "WiFi connection failed, exiting");
    returnToReader();
    return;
  }
  LOG_DBG("KOSync", "WiFi connected, starting sync");
  WiFi.setSleep(false);
  LOG_DBG("KOSync", "WiFi sleep disabled for sync");
  if (model.plugin) {
    publish(XP_KO_SYNCING, XP_KO_SERVICE_ERROR, 0, tr(STR_SYNC_PROGRESS));
    requestUpdate(true);
    pluginevents::drain(&renderer);
    publish(XP_KO_SYNCHRONIZED);
    markReturn();
    requestUpdate(true);
    return;
  }
  publish(XP_KO_SYNCING, XP_KO_HASH);
  requestUpdate(true);
  performSync();
}
void KOReaderSyncActivity::onEnter() {
  Activity::onEnter();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  if (!initializeUi()) {
    returnToReader();
    return;
  }
#ifdef CROSSPOINT_UI_ORACLE
  if (oracleFreeze) {
    requestUpdate();
    return;
  }
#endif
  model.smart = KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART;
  if (!model.plugin && !KOREADER_STORE.hasCredentials()) {
    publish(XP_KO_NO_CREDENTIALS);
    requestUpdate();
    return;
  }
  model.wifi_activated = 1;
  if (WiFi.status() == WL_CONNECTED) {
    LOG_DBG("KOSync", "Already connected to WiFi");
    onWifiSelectionComplete(true);
    return;
  }
  LOG_DBG("KOSync", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}
void KOReaderSyncActivity::onExit() {
  KOReaderUiActivity::onExit();
  if (model.wifi_activated) {
    WiFi.disconnect(false);
    delay(30);
    silentRestartToReader();
  }
}
void KOReaderSyncActivity::performEffect(const XpUiEffect& effect) {
  switch (effect.kind) {
    case XP_UI_CANCEL:
      returnToReader();
      break;
    case XP_UI_ACTIVATE:
      switch (effect.value) {
        case 0: {
          RunGuard guard(runActive);
          if (!guard) return;
          const XpKoServices services{this, &KOReaderSyncActivity::service};
          xp_ui_koreader_save(&services);
          break;
        }
        case 1:
          performUpload();
          break;
        case 2:
          startUpload();
          break;
      }
      break;
    default:
      break;
  }
}
uint32_t KOReaderSyncActivity::service(void* context, uint32_t operation, uint32_t a, uint32_t b) {
  return static_cast<KOReaderSyncActivity*>(context)->service(static_cast<XpKoOperation>(operation), a, b);
}
uint32_t KOReaderSyncActivity::service(XpKoOperation operation, uint32_t a, uint32_t b) {
  switch (operation) {
    case XP_KO_PUBLISH:
      publish(static_cast<XpKoPhase>(a), static_cast<XpKoMessage>(b & 255), b >> 16,
              KOReaderSyncClient::errorString(static_cast<KOReaderSyncClient::Error>((b >> 8) & 255)));
      return 0;
    case XP_KO_REPAINT:
      if (a == 2)
        requestUpdateAndWait();
      else
        requestUpdate(a != 0);
      return 0;
    case XP_KO_MARK_RETURN:
      markReturn();
      return 0;
    case XP_KO_RETURN_READER:
      returnToReader();
      return 0;
    case XP_KO_HASH_PRIMARY:
      documentHash = calculateDocumentHashForMethod(epubPath, KOREADER_STORE.getMatchMethod());
      if (documentHash.empty()) return 0;
      LOG_DBG("KOSync", "Document hash (%s): %s", matchMethodName(KOREADER_STORE.getMatchMethod()),
              documentHash.c_str());
      return 1;
    case XP_KO_HASH_ALTERNATE:
      alternateHash = calculateDocumentHashForMethod(epubPath, alternateMatchMethod(KOREADER_STORE.getMatchMethod()));
      return !alternateHash.empty() && alternateHash != documentHash;
    case XP_KO_FETCH_PRIMARY: {
      const auto result = KOReaderSyncClient::getProgress(documentHash, remoteProgress);
      LOG_DBG("KOSync", "Primary remote (%s): result=%d http=%d doc=%s local=%.6f remote=%.6f xpath=%s",
              matchMethodName(KOREADER_STORE.getMatchMethod()), result, KOReaderSyncClient::lastHttpCode,
              documentHash.c_str(), localProgress.percentage, remoteProgress.percentage,
              remoteProgress.progress.c_str());
      return result;
    }
    case XP_KO_FETCH_ALTERNATE: {
      const auto result = KOReaderSyncClient::getProgress(alternateHash, scratch);
      LOG_DBG("KOSync", "Alternate remote (%s): result=%d http=%d doc=%s local=%.6f remote=%.6f xpath=%s",
              matchMethodName(alternateMatchMethod(KOREADER_STORE.getMatchMethod())), result,
              KOReaderSyncClient::lastHttpCode, alternateHash.c_str(), localProgress.percentage, scratch.percentage,
              scratch.progress.c_str());
      std::string released = std::move(alternateHash);
      return result;
    }
    case XP_KO_ADOPT_ALTERNATE:
      remoteProgress = std::move(scratch);
      return 0;
    case XP_KO_LOAD_BOOK:
      ensureEpubLoaded();
      return epub != nullptr;
    case XP_KO_MAP_PRIMARY: {
      RenderLock lock;
      GfxRenderer::FrameBufferLoan loan(renderer);
      const auto map = [&](const KOReaderProgress& progress) {
        const SavedProgressPosition koPos = {progress.progress, progress.percentage};
        auto mapped =
            ProgressMapper::toCrossPoint(epub, koPos, renderer, localPosition.spineIndex, localPosition.totalPages);
        if (!mapped.hasVisibleTextOffset && progress.position.has_value()) {
          if (const auto richMapped = ProgressMapper::fromRichPosition(epub, *progress.position, renderer,
                                                                       progress.position->xpath == progress.progress))
            mapped = *richMapped;
        }
        return mapped;
      };
      remotePosition = map(remoteProgress);
      if (a) {
        const auto alternate = map(scratch);
        if (selectRemoteRecord(remotePosition, remoteProgress.percentage, alternate, scratch.percentage) ==
            RemoteRecordChoice::Alternate) {
          remoteProgress = std::move(scratch);
          remotePosition = alternate;
          LOG_DBG("KOSync", "Selected alternate remote record after mapped-position comparison");
        } else {
          LOG_DBG("KOSync", "Kept primary remote record after mapped-position comparison");
        }
      }
      return 0;
    }
    case XP_KO_COMPARE:
      return static_cast<uint32_t>(
          compareProgress(localPosition, localProgress.percentage, remotePosition, remoteProgress.percentage));
    case XP_KO_SAVE_PROGRESS: {
      assert(epub);
      std::optional<uint32_t> offset;
      if (remotePosition.hasVisibleTextOffset) offset = remotePosition.visibleTextOffset;
      return EpubReaderUtils::saveProgress(*epub, remotePosition.spineIndex, remotePosition.pageNumber, 0, offset);
    }
    case XP_KO_CLIP_SYNC: {
#if defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3
      RenderLock lock;
      GfxRenderer::FrameBufferLoan loan(renderer);
#endif
      return clippingSync::run(epubPath, documentHash);
    }
    case XP_KO_UPLOAD_BUILD: {
      scratch = {};
      scratch.document = documentHash;
      scratch.progress = localProgress.xpath;
      scratch.percentage = localProgress.percentage;
      if (a) {
        KOReaderRichPosition position;
        position.pctQ = b;
        position.spineIndex = static_cast<uint16_t>(localPosition.spineIndex);
        position.pageNumber = static_cast<uint16_t>(localPosition.pageNumber);
        position.totalPages = static_cast<uint16_t>(localPosition.totalPages > 0 ? localPosition.totalPages : 1);
        if (localPosition.hasParagraphIndex) position.paragraphIndex = localPosition.paragraphIndex;
        position.xpath = localProgress.xpath;
        scratch.position = std::move(position);
      }
      return 0;
    }
    case XP_KO_UPLOAD_METADATA: {
      ensureEpubLoaded();
      KOReaderMetadata meta;
      const auto slash = epubPath.rfind('/');
      meta.filename = slash == std::string::npos ? epubPath : epubPath.substr(slash + 1);
      if (epub) {
        meta.title = epub->getTitle();
        meta.authors = epub->getAuthor();
        if (a) {
          Epub::SyncMetadata syncMetadata;
          if (epub->loadSyncMetadata(syncMetadata)) {
            meta.isbn = std::move(syncMetadata.isbn);
            meta.asin = std::move(syncMetadata.asin);
            meta.series = std::move(syncMetadata.series);
            meta.seriesIndex = syncMetadata.seriesIndex;
          } else {
            LOG_DBG("KOSync", "Could not read extended EPUB metadata; sending core metadata only");
          }
        }
      } else {
        LOG_ERR("KOSync", "Epub unavailable for metadata; sending filename only");
      }
      Storage.readFileToString("KOSync", epubPath + ".meta.json", 2 * 1024, meta.extraJson);
      scratch.metadata = std::move(meta);
      return 0;
    }
    case XP_KO_RELEASE_BOOK:
      releaseEpub();
      return 0;
    case XP_KO_UPLOAD_SEND: {
      WifiPowerSaveGuard psGuard;
      return KOReaderSyncClient::updateProgress(scratch);
    }
    case XP_KO_RADIO_STOP:
      esp_wifi_stop();
      return 0;
    case XP_KO_DIAGNOSTIC:
      switch (static_cast<XpKoDiagnostic>(a)) {
        case XP_KO_DIAG_SMART_MISSING:
          LOG_DBG("KOSync", "Smart sync: no remote progress found for known document hashes; uploading local %.6f",
                  localProgress.percentage);
          break;
        case XP_KO_DIAG_SMART_DECISION:
          LOG_DBG("KOSync", "Smart decision: doc=%s result=%d local=%.6f remote=%.6f remoteXpath=%s mapped=%d/%d",
                  documentHash.c_str(), static_cast<int>(b), localProgress.percentage, remoteProgress.percentage,
                  remoteProgress.progress.c_str(), remotePosition.spineIndex, remotePosition.pageNumber);
          break;
        case XP_KO_DIAG_SMART_UNKNOWN:
          LOG_DBG("KOSync", "Smart sync comparison unknown; opening manual selection");
          break;
      }
      return 0;
    case XP_KO_AUTH_REQUEST:
      return 0;
  }
  return 0;
}
void KOReaderSyncActivity::snapshotResources() {
  // Result alone borrows the book. Upload publishes Uploading and waits for
  // rendering before release; loading precedes Result and teardown holds RenderLock.
  if (painting.phase == XP_KO_RESULT) paintingEpub = epub.get();
}
void KOReaderSyncActivity::prepareText() {
  auto* book = paintingEpub;
  paintingEpub = nullptr;
  {
    std::string released;
    released.swap(remoteChapter);
  }
  if (painting.phase != XP_KO_RESULT || !book) return;
  const int toc = book->getTocIndexForSpineIndex(remotePosition.spineIndex);
  remoteChapter = toc >= 0 ? book->getTocItem(toc).title
                           : std::string(tr(STR_SECTION_PREFIX)) + std::to_string(remotePosition.spineIndex + 1);
  snprintf(localChapterFallback, sizeof(localChapterFallback), "%s%d", tr(STR_SECTION_PREFIX),
           localPosition.spineIndex + 1);
  snprintf(remoteValue, sizeof(remoteValue), tr(STR_PAGE_OVERALL_FORMAT), remotePosition.pageNumber + 1,
           remoteProgress.percentage * 100);
  snprintf(localValue, sizeof(localValue), tr(STR_PAGE_TOTAL_OVERALL_FORMAT), localPosition.pageNumber + 1,
           localPosition.totalPages, localProgress.percentage * 100);
  deviceValue[0] = '\0';
  if (!remoteProgress.device.empty())
    snprintf(deviceValue, sizeof(deviceValue), tr(STR_DEVICE_FROM_FORMAT), remoteProgress.device.c_str());
}
const char* KOReaderSyncActivity::resourceText(uint32_t handle) const {
  switch (handle) {
    case 4:
      return remoteChapter.c_str();
    case 5:
      return remoteValue;
    case 6:
      return deviceValue;
    case 7:
      return localChapterName.empty() ? localChapterFallback : localChapterName.c_str();
    case 8:
      return localValue;
    default:
      return KOReaderUiActivity::resourceText(handle);
  }
}
