#include "FontDownloadActivity.h"

#include <ArduinoJson.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace fui = freeink::ui;

FontDownloadActivity::FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UpdateUiActivity("FontDownload", renderer, mappedInput, XP_UI_UPDATE_FONTS),
      fontInstaller_(sdFontSystem.registry()) {}

void FontDownloadActivity::activateIndex(const int index) {
  switch (phase()) {
    case GROUP_LIST:

      enterGroup(index);
      requestUpdate();
      return;
    case FAMILY_LIST:

      // Activation starts a download or opens the delete prompt; a lingering
      // flash would gray an unrelated row.

      activateSelected();  // ends with requestUpdateAndWait itself
      return;
    case WIFI_SELECTION:
    case LOADING_MANIFEST:
    case DOWNLOADING:
    case COMPLETE:
    case ERROR:
      return;
  }
}

// --- Lifecycle ---

void FontDownloadActivity::onEnter() {
  UpdateUiActivity::onEnter();
  requestUpdate();
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void FontDownloadActivity::onExit() {
  UpdateUiActivity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void FontDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    const auto action = serviceResult(XP_UI_UPDATE_WIFI_RESULT, 0);
    performAction(static_cast<XpUiUpdateAction>(action), 0);
    return;
  }

  {
    notify(XP_UI_UPDATE_WIFI_RESULT, 1);
  }
  requestUpdateAndWait();

  if (!fetchAndParseManifest()) {
    // Drop whatever was parsed before the failure: it would otherwise sit in
    // the heap behind the error screen, and leave the retry path pointing at a
    // half-built family table.
    clearManifest();
    {
      notify(XP_UI_UPDATE_FONT_ERROR);
    }
    return;
  }

  if (!hasGroupScreen()) buildFilteredIndices(0);

  {
    rowsDirty_ = true;  // families_ just loaded
    if (hasGroupScreen()) {
      notify(XP_UI_UPDATE_MANIFEST, 1, 1);
    } else {
      notify(XP_UI_UPDATE_MANIFEST, 1, 0);
    }
  }
}

// --- Manifest fetching ---

void FontDownloadActivity::clearManifest() {
  // Swap rather than clear: clear() keeps the capacity, and this runs to hand
  // the heap back while the error screen is up. Reverse allocation order.
  std::vector<int>().swap(filteredIndices_);
  std::vector<ManifestFamily>().swap(families_);
  std::vector<StrRef>().swap(scriptGroupLabels_);
  files_.reset();
  fileEntryCount_ = 0;
  stringArena_.reset();
  arenaUsed_ = 0;
  arenaCapacity_ = 0;
}

bool FontDownloadActivity::internString(const char* text, StrRef& outRef) {
  if (text == nullptr || *text == '\0') {
    outRef = 0;
    return true;
  }
  const size_t length = std::strlen(text) + 1;
  if (arenaUsed_ + length > arenaCapacity_) {
    LOG_ERR("FONT", "Manifest string arena overflow at %u/%u bytes", arenaUsed_, arenaCapacity_);
    return false;
  }
  outRef = arenaUsed_;
  std::memcpy(stringArena_.get() + arenaUsed_, text, length);
  arenaUsed_ = static_cast<uint32_t>(arenaUsed_ + length);
  return true;
}

bool FontDownloadActivity::fetchAndParseManifest() {
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
  }
  // Rescan installed families before any TLS or JSON buffers exist. The rescan
  // frees and reallocates every registry entry, which fragments the heap if it
  // runs while the manifest DOM and the catalog arena are live.
  clearManifest();
  fontInstaller_.refreshRegistry();

  // Download manifest to a temp file on SD card to avoid holding both
  // TLS buffers and the full JSON string in RAM simultaneously.
  static constexpr const char* MANIFEST_TMP = "/fonts_manifest.tmp";

  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR("FONT", "Low heap for manifest (%u free, %u max block)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    errorMessage_ = tr(STR_MEMORY_ERROR);
    return false;
  }

  // No downgradeRedirectsToHttp here, unlike the font transfers below: this
  // response carries the crc32 values that are the only integrity anchor for
  // those plain-HTTP downloads.
  auto result = HttpDownloader::downloadToFile(FONT_MANIFEST_URL, MANIFEST_TMP, nullptr);
  if (result != HttpDownloader::OK) {
    LOG_ERR("FONT", "Failed to fetch manifest from %s", FONT_MANIFEST_URL);
    errorMessage_ = "Failed to fetch font list";
    Storage.remove(MANIFEST_TMP);
    return false;
  }

  // HTTP client is now closed — TLS buffers freed. Parse JSON from file.
  HalFile manifestFile;
  if (!Storage.openFileForRead("FONT", MANIFEST_TMP, manifestFile)) {
    LOG_ERR("FONT", "Failed to open temp manifest");
    Storage.remove(MANIFEST_TMP);
    errorMessage_ = "Failed to read font list";
    return false;
  }

  JsonDocument doc;
  DeserializationError err;
  {
    // "styles" is the only key the catalog never reads. Dropping it keeps the
    // DOM about 1KB smaller while it coexists with the arena allocated below.
    JsonDocument filter;
    filter["version"] = true;
    filter["baseUrl"] = true;
    filter["scriptGroups"][0]["tag"] = true;
    filter["scriptGroups"][0]["label"] = true;
    filter["families"][0]["name"] = true;
    filter["families"][0]["description"] = true;
    filter["families"][0]["scripts"] = true;
    filter["families"][0]["files"][0]["name"] = true;
    filter["families"][0]["files"][0]["size"] = true;
    filter["families"][0]["files"][0]["crc32"] = true;
    err = deserializeJson(doc, manifestFile, DeserializationOption::Filter(filter));
  }
  manifestFile.close();
  Storage.remove(MANIFEST_TMP);

  if (err) {
    LOG_ERR("FONT", "Manifest parse error: %s", err.c_str());
    errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
    return false;
  }

  int version = doc["version"] | 0;
  if (version != FONTS_MANIFEST_VERSION) {
    LOG_ERR("FONT", "Unsupported manifest version: %d", version);
    errorMessage_ = "Unsupported manifest version";
    return false;
  }

  baseUrl_ = doc["baseUrl"] | "";
  downloadUrl_.reserve(baseUrl_.size() + 128);

  JsonArray groupsArr = doc["scriptGroups"].as<JsonArray>();
  JsonArray familiesArr = doc["families"].as<JsonArray>();

  // Size the arena and the file table in one pass so neither reallocates while
  // the catalog is built: a mid-build growth would both fragment the heap and
  // invalidate arena pointers already handed out below.
  const size_t groupCount = std::min(groupsArr.size(), MAX_SCRIPT_GROUPS);
  size_t arenaBytes = 1;  // leading terminator makes offset 0 the empty string
  size_t manifestFileCount = 0;
  for (size_t groupIndex = 0; groupIndex < groupCount; groupIndex++) {
    arenaBytes += std::strlen(groupsArr[groupIndex]["label"] | "") + 1;
  }
  for (JsonObject fObj : familiesArr) {
    arenaBytes += std::strlen(fObj["name"] | "") + 1;
    arenaBytes += std::strlen(fObj["description"] | "") + 1;
    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      arenaBytes += std::strlen(fileObj["name"] | "") + 1;
      manifestFileCount++;
    }
  }
  stringArena_ = makeUniqueNoThrow<char[]>(arenaBytes);
  if (!stringArena_) {
    LOG_ERR("FONT", "OOM: %zu byte string arena", arenaBytes);
    errorMessage_ = tr(STR_MEMORY_ERROR);
    return false;
  }
  stringArena_[0] = '\0';
  arenaUsed_ = 1;
  arenaCapacity_ = static_cast<uint32_t>(arenaBytes);
  files_ = makeUniqueNoThrow<ManifestFile[]>(manifestFileCount);
  if (!files_) {
    LOG_ERR("FONT", "OOM: %zu manifest file entries", manifestFileCount);
    errorMessage_ = tr(STR_MEMORY_ERROR);
    return false;
  }

  scriptGroupLabels_.reserve(groupCount);
  if (groupsArr.size() > MAX_SCRIPT_GROUPS) {
    LOG_ERR("FONT", "Manifest declares more than %zu script groups; extra groups ignored", MAX_SCRIPT_GROUPS);
  }
  for (size_t groupIndex = 0; groupIndex < groupCount; groupIndex++) {
    JsonObject groupObj = groupsArr[groupIndex].as<JsonObject>();
    const char* tag = groupObj["tag"] | "";
    const char* label = groupObj["label"] | "";
    if (*tag == '\0' || *label == '\0') {
      LOG_ERR("FONT", "Malformed script group at index %zu", groupIndex);
      errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
      return false;
    }
    StrRef labelRef = 0;
    if (!internString(label, labelRef)) {
      errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
      return false;
    }
    scriptGroupLabels_.push_back(labelRef);
  }

  families_.reserve(familiesArr.size());
  filteredIndices_.reserve(familiesArr.size());

  for (JsonObject fObj : familiesArr) {
    ManifestFamily family;
    if (!internString(fObj["name"] | "", family.name) || !internString(fObj["description"] | "", family.description)) {
      errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
      return false;
    }

    for (JsonVariant script : fObj["scripts"].as<JsonArray>()) {
      const char* familyTag = script.as<const char*>();
      if (!familyTag) continue;
      for (size_t groupIndex = 0; groupIndex < scriptGroupLabels_.size(); groupIndex++) {
        JsonObject groupObj = groupsArr[groupIndex].as<JsonObject>();
        const char* groupTag = groupObj["tag"] | "";
        if (std::strcmp(familyTag, groupTag) == 0) {
          family.scriptMask |= uint32_t{1} << groupIndex;
          break;
        }
      }
    }

    family.fileStart = fileEntryCount_;
    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      ManifestFile file;
      if (!internString(fileObj["name"] | "", file.name)) {
        errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
        return false;
      }
      file.size = fileObj["size"] | 0u;

      if (!fileObj["crc32"].is<uint32_t>()) {
        LOG_ERR("FONT", "Malformed manifest file entry: missing or invalid crc32 for %s", str(file.name));
        errorMessage_ = tr(STR_INVALID_FONT_MANIFEST);
        return false;
      }
      file.crc32 = fileObj["crc32"].as<uint32_t>();

      family.totalSize += file.size;
      files_[fileEntryCount_++] = file;
    }
    family.fileCount = fileEntryCount_ - family.fileStart;

    family.installed = fontInstaller_.isFamilyInstalled(str(family.name));

    // Detect updates by comparing manifest file sizes with files on disk.
    // Not a checksum, but a size mismatch reliably indicates a rebuild in practice.
    if (family.installed) {
      for (uint32_t i = 0; i < family.fileCount; i++) {
        const ManifestFile& file = files_[family.fileStart + i];
        char path[128];
        FontInstaller::buildFontPath(str(family.name), str(file.name), path, sizeof(path));
        HalFile f;
        if (Storage.openFileForRead("FONT", path, f)) {
          size_t actual = f.fileSize();
          f.close();
          if (actual != file.size) {
            family.hasUpdate = true;
            break;
          }
        } else {
          // File missing on disk but family dir exists — treat as update
          family.hasUpdate = true;
          break;
        }
      }
    }

    families_.push_back(family);
  }

  const size_t rowCapacity = std::max(families_.size() + 2, scriptGroupLabels_.size() + 1);
  rowLabels_.reserve(rowCapacity);
  rowItems_.reserve(rowCapacity);

  LOG_DBG("FONT", "Manifest loaded: %zu families, %zu script groups", families_.size(), scriptGroupLabels_.size());
  return true;
}

// --- Download ---

void FontDownloadActivity::downloadAll() {
  transferCancel = false;
  for (const int familyIndex : filteredIndices_) {
    if (families_[familyIndex].installed) continue;
    downloadFamily(families_[familyIndex]);
    if (phase() == ERROR || transferCancel) return;
  }

  {
    rowsDirty_ = true;
    notify(XP_UI_UPDATE_FONT_DONE);
  }
}

void FontDownloadActivity::updateAll() {
  transferCancel = false;
  for (const int familyIndex : filteredIndices_) {
    if (!families_[familyIndex].hasUpdate) continue;
    downloadFamily(families_[familyIndex]);
    if (phase() == ERROR || transferCancel) return;
  }

  {
    rowsDirty_ = true;
    notify(XP_UI_UPDATE_FONT_DONE);
  }
}

bool FontDownloadActivity::showDownloadAllRow() const {
  for (const int familyIndex : filteredIndices_) {
    if (!families_[familyIndex].installed) return true;
  }
  return false;
}

bool FontDownloadActivity::showUpdateAllRow() const {
  for (const int familyIndex : filteredIndices_) {
    if (families_[familyIndex].hasUpdate) return true;
  }
  return false;
}

int FontDownloadActivity::specialRowCount() const {
  return (showDownloadAllRow() ? 1 : 0) + (showUpdateAllRow() ? 1 : 0);
}

bool FontDownloadActivity::isDownloadAllRow(int index) const { return showDownloadAllRow() && index == 0; }

bool FontDownloadActivity::isUpdateAllRow(int index) const {
  return showUpdateAllRow() && index == (showDownloadAllRow() ? 1 : 0);
}

int FontDownloadActivity::listItemCount() const {
  return filteredIndices_.empty() ? 0 : static_cast<int>(filteredIndices_.size()) + specialRowCount();
}

int FontDownloadActivity::listCount() const {
  switch (phase()) {
    case GROUP_LIST:
      return groupListItemCount();
    case FAMILY_LIST:
      return listItemCount();
    case WIFI_SELECTION:
    case LOADING_MANIFEST:
    case DOWNLOADING:
    case COMPLETE:
    case ERROR:
      return 0;
  }
  return 0;
}

int FontDownloadActivity::familyIndexFromList(const int listIndex) const {
  const int filteredIndex = listIndex - specialRowCount();
  if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filteredIndices_.size())) return -1;
  return filteredIndices_[filteredIndex];
}

int FontDownloadActivity::groupMemberCount(const int scriptGroupIndex) const {
  if (scriptGroupIndex < 0 || scriptGroupIndex >= static_cast<int>(scriptGroupLabels_.size())) return 0;
  const uint32_t groupBit = uint32_t{1} << scriptGroupIndex;
  int count = 0;
  for (const auto& family : families_) {
    if (family.scriptMask & groupBit) count++;
  }
  return count;
}

void FontDownloadActivity::buildFilteredIndices(const int groupListIndex) {
  filteredIndices_.clear();
  filteredIndices_.reserve(families_.size());
  if (groupListIndex <= 0) {
    for (int familyIndex = 0; familyIndex < static_cast<int>(families_.size()); familyIndex++) {
      filteredIndices_.push_back(familyIndex);
    }
    return;
  }

  const uint32_t groupBit = uint32_t{1} << (groupListIndex - 1);
  for (int familyIndex = 0; familyIndex < static_cast<int>(families_.size()); familyIndex++) {
    if (families_[familyIndex].scriptMask & groupBit) filteredIndices_.push_back(familyIndex);
  }
}

void FontDownloadActivity::enterGroup(const int groupListIndex) {
  closeRouting();
  buildFilteredIndices(groupListIndex);
  {
    notify(XP_UI_UPDATE_GROUP);
    rowsDirty_ = true;
  }
}

size_t FontDownloadActivity::totalDownloadSize() const {
  size_t total = 0;
  for (const int familyIndex : filteredIndices_) {
    if (!families_[familyIndex].installed) total += families_[familyIndex].totalSize;
  }
  return total;
}

size_t FontDownloadActivity::totalUpdateSize() const {
  size_t total = 0;
  for (const int familyIndex : filteredIndices_) {
    if (families_[familyIndex].hasUpdate) total += families_[familyIndex].totalSize;
  }
  return total;
}

// Standard CRC32 matching zlib/Python zlib.crc32().
bool FontDownloadActivity::computeFileCrc32(const char* path, uint32_t& outCrc) {
  HalFile f;
  if (!Storage.openFileForRead("FONT", path, f)) {
    return false;
  }
  constexpr size_t BUF_SIZE = 128;
  uint8_t buf[BUF_SIZE];
  uint32_t crc = 0;
  while (f.available()) {
    const int n = f.read(buf, BUF_SIZE);
    if (n <= 0) break;
    crc = esp_rom_crc32_le(crc, buf, static_cast<uint32_t>(n));
  }
  outCrc = crc;
  return true;
}

void FontDownloadActivity::downloadFamily(ManifestFamily& family) {
  {
    notify(XP_UI_UPDATE_FONT_BEGIN, static_cast<uint32_t>(&family - families_.data()));
    transferCancel = false;
  }
  requestUpdateAndWait();

  // Rebuildable SD-font caches (glyph/kern arenas, CJK fallback tables) can
  // hold tens of KB the TLS session needs; release them up front rather than
  // starving the transfer. They repopulate on demand after the download.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
    LOG_DBG("FONT", "Free heap after SD font cache release: %d bytes", ESP.getFreeHeap());
  }

  // Check before touching the family directory so a failed update leaves the
  // installed family unchanged.
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR("FONT", "Low heap for download (%u free, %u max block)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    RenderLock lock(*this);
    notify(XP_UI_UPDATE_FONT_ERROR);
    errorMessage_ = tr(STR_MEMORY_ERROR);
    return;
  }

  if (!fontInstaller_.ensureFamilyDir(str(family.name))) {
    RenderLock lock(*this);
    notify(XP_UI_UPDATE_FONT_ERROR);
    errorMessage_ = "Failed to create font directory";
    return;
  }

  for (uint32_t i = 0; i < family.fileCount; i++) {
    const ManifestFile& file = files_[family.fileStart + i];

    {
      notify(XP_UI_UPDATE_FONT_FILE, file.size);
    }
    requestUpdateAndWait();

    char destPath[128];
    FontInstaller::buildFontPath(str(family.name), str(file.name), destPath, sizeof(destPath));

    downloadUrl_.assign(baseUrl_).append(str(file.name));

    auto result = HttpDownloader::downloadToFile(
        downloadUrl_, destPath,
        [this](size_t downloaded, size_t total) {
          notify(XP_UI_UPDATE_PROGRESS, downloaded, total);
          mappedInput.update(true);
          const bool back = mappedInput.isPressed(MappedInputManager::Button::Back) ||
                            mappedInput.wasPressed(MappedInputManager::Button::Back);
          const bool home = mappedInput.wasHomeGesture();
          notify(XP_UI_UPDATE_TRANSFER_INPUT, back, home);
          {
            ModelLock lock(*this);
            transferCancel = model.cancel_requested != 0;
          }
          requestUpdate(true);
        },
        // Bulk font transfers follow GitHub's release-asset redirect over plain
        // HTTP: the CRC check below (manifest fetched over TLS) covers
        // integrity, and skipping the second TLS session keeps the C3 heap out
        // of MEMORY_E territory.
        &transferCancel, "", "", /*headers=*/{}, /*downgradeRedirectsToHttp=*/true);

    if (result == HttpDownloader::ABORTED) {
      fontInstaller_.deleteFamily(str(family.name));
      family.installed = false;
      family.hasUpdate = false;
      if (serviceResult(XP_UI_UPDATE_FONT_ABORTED) == XP_UI_UPDATE_HOME) {
        onGoHome();
        return;
      }
      rowsDirty_ = true;  // installed/hasUpdate just changed above
      return;
    }

    if (result != HttpDownloader::OK) {
      LOG_ERR("FONT", "Download failed: %s (%d)", str(file.name), result);
      fontInstaller_.deleteFamily(str(family.name));
      family.installed = false;
      family.hasUpdate = false;
      RenderLock lock(*this);
      notify(XP_UI_UPDATE_FONT_ERROR);
      errorMessage_ = std::string("Download failed: ") + str(file.name);
      return;
    }

    uint32_t actualCrc = 0;
    if (!computeFileCrc32(destPath, actualCrc)) {
      LOG_ERR("FONT", "Failed to open file for CRC check: %s", destPath);
      fontInstaller_.deleteFamily(str(family.name));
      family.installed = false;
      family.hasUpdate = false;
      RenderLock lock(*this);
      notify(XP_UI_UPDATE_FONT_ERROR);
      errorMessage_ = std::string("Failed to compute checksum: ") + str(file.name);
      return;
    }
    if (actualCrc != file.crc32) {
      LOG_ERR("FONT", "CRC32 mismatch for %s: got %08x expected %08x", str(file.name), actualCrc, file.crc32);
      fontInstaller_.deleteFamily(str(family.name));
      family.installed = false;
      family.hasUpdate = false;
      RenderLock lock(*this);
      notify(XP_UI_UPDATE_FONT_ERROR);
      errorMessage_ = std::string("Checksum mismatch: ") + str(file.name);
      return;
    }
    LOG_DBG("FONT", "Downloaded %s (size=%u crc32=%08x)", str(file.name), file.size, actualCrc);

    if (!fontInstaller_.validateCpfontFile(destPath)) {
      LOG_ERR("FONT", "Invalid .cpfont: %s", destPath);
      fontInstaller_.deleteFamily(str(family.name));
      family.installed = false;
      family.hasUpdate = false;
      RenderLock lock(*this);
      notify(XP_UI_UPDATE_FONT_ERROR);
      errorMessage_ = std::string("Invalid font file: ") + str(file.name);
      return;
    }
    notify(XP_UI_UPDATE_FONT_FILE_DONE);
  }

  fontInstaller_.refreshRegistry();
  family.installed = true;
  family.hasUpdate = false;

  {
    rowsDirty_ = true;
    notify(XP_UI_UPDATE_FONT_DONE);
  }
}

void FontDownloadActivity::promptDeleteSelectedFamily() {
  const int pendingDeleteFamilyIndex = familyIndexFromList(selectedIndex());
  if (pendingDeleteFamilyIndex < 0 || pendingDeleteFamilyIndex >= static_cast<int>(families_.size())) {
    return;
  }

  std::string heading = tr(STR_DELETE);
  const auto& family = families_[pendingDeleteFamilyIndex];
  std::string body = str(family.name);
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, body),
                         [this](const ActivityResult& result) { onDeleteConfirmationResult(result); });
}

void FontDownloadActivity::onDeleteConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) {
    requestUpdate();
    return;
  }

  const int familyIndex = familyIndexFromList(selectedIndex());
  if (familyIndex < 0) {
    requestUpdate();
    return;
  }
  auto& family = families_[familyIndex];

  if (fontInstaller_.deleteFamily(str(family.name)) != FontInstaller::Error::OK) {
    RenderLock lock(*this);
    notify(XP_UI_UPDATE_FONT_ERROR);
    errorMessage_ = "Failed to delete font";
  } else {
    fontInstaller_.refreshRegistry();
    family.installed = false;
    family.hasUpdate = false;
    // Unlike the other family_ mutations, this one stays in FAMILY_LIST (no
    // phase() transition to hang the rebuild off), so it must set the flag
    // directly.
    rowsDirty_ = true;
  }

  requestUpdate();
}

void FontDownloadActivity::activateSelected() {
  if (filteredIndices_.empty()) return;
  if (isDownloadAllRow(selectedIndex())) {
    uint32_t count = 0;
    for (const int familyIndex : filteredIndices_) {
      if (!families_[familyIndex].installed) count += families_[familyIndex].fileCount;
    }
    notify(XP_UI_UPDATE_FONT_BATCH, count);
    downloadAll();
  } else if (isUpdateAllRow(selectedIndex())) {
    uint32_t count = 0;
    for (const int familyIndex : filteredIndices_) {
      if (families_[familyIndex].hasUpdate) count += families_[familyIndex].fileCount;
    }
    notify(XP_UI_UPDATE_FONT_BATCH, count);
    updateAll();
  } else {
    // The special rows disappear when a download starts, so a stale selection
    // can map past the family table.
    const int familyIndex = familyIndexFromList(selectedIndex());
    if (familyIndex < 0 || familyIndex >= static_cast<int>(families_.size())) return;
    auto& family = families_[familyIndex];
    if (!family.installed || family.hasUpdate) {
      notify(XP_UI_UPDATE_FONT_BATCH, family.fileCount);
      downloadFamily(family);
    } else {
      promptDeleteSelectedFamily();
      return;
    }
  }
  requestUpdateAndWait();
}

void FontDownloadActivity::rebuildRowItems() {
  switch (painting.phase) {
    case GROUP_LIST:
      rebuildGroupRowItems();
      return;
    case FAMILY_LIST:
      rebuildFamilyRowItems();
      return;
    case WIFI_SELECTION:
    case LOADING_MANIFEST:
    case DOWNLOADING:
    case COMPLETE:
    case ERROR:
      rowLabels_.clear();
      rowItems_.clear();
      return;
  }
}

void FontDownloadActivity::rebuildGroupRowItems() {
  const int listSize = groupListItemCount();
  rowLabels_.assign(listSize, std::string());
  rowItems_.clear();
  rowItems_.reserve(listSize);
  for (int rowIndex = 0; rowIndex < listSize; rowIndex++) {
    fui::ListItem item;
    item.label = rowIndex == 0 ? tr(STR_ALL_FONTS) : str(scriptGroupLabels_[rowIndex - 1]);
    const int memberCount = rowIndex == 0 ? static_cast<int>(families_.size()) : groupMemberCount(rowIndex - 1);
    rowLabels_[rowIndex] = std::to_string(memberCount);
    item.value = rowLabels_[rowIndex].c_str();
    item.actionValue = static_cast<int16_t>(rowIndex);
    rowItems_.push_back(item);
  }
}

void FontDownloadActivity::rebuildFamilyRowItems() {
  const int listSize = listItemCount();
  rowLabels_.assign(listSize, std::string());
  rowItems_.clear();
  rowItems_.reserve(listSize);
  for (int i = 0; i < listSize; i++) {
    fui::ListItem item;
    if (isDownloadAllRow(i)) {
      rowLabels_[i] = std::string(tr(STR_DOWNLOAD_ALL)) + " (" + formatSize(totalDownloadSize()) + ")";
      item.label = rowLabels_[i].c_str();
    } else if (isUpdateAllRow(i)) {
      rowLabels_[i] = std::string(tr(STR_UPDATE_ALL)) + " (" + formatSize(totalUpdateSize()) + ")";
      item.label = rowLabels_[i].c_str();
    } else {
      const auto& family = families_[familyIndexFromList(i)];
      item.label = str(family.name);
      if (family.description != 0) item.subtitle = str(family.description);
      if (family.hasUpdate) {
        item.value = tr(STR_UPDATE_AVAILABLE);
      } else if (family.installed) {
        item.value = tr(STR_INSTALLED);
      }
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

std::string FontDownloadActivity::formatSize(size_t bytes) {
  char buf[32];
  if (bytes >= 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%zu B", bytes);
  }
  return buf;
}

void FontDownloadActivity::performAction(XpUiUpdateAction action, int32_t value) {
  switch (action) {
    case XP_UI_UPDATE_FINISH:
      finish();
      break;
    case XP_UI_UPDATE_ENTER_GROUP:
      activateIndex(value);
      break;
    case XP_UI_UPDATE_FONT_ACTIVATE:
      activateIndex(value);
      break;
    case XP_UI_UPDATE_FONT_RETRY:
      if (value >= 0 && value < static_cast<int>(families_.size())) {
        downloadFamily(families_[value]);
        requestUpdateAndWait();
      }
      break;
    default:
      break;
  }
}
void FontDownloadActivity::prepareRows() {
  if (painting.phase != GROUP_LIST && painting.phase != FAMILY_LIST) {
    cachedPhase = painting.phase;
    return;
  }
  if (rowsDirty_.exchange(false) || cachedPhase != painting.phase) {
    rebuildRowItems();
    cachedPhase = painting.phase;
  }
}
bool FontDownloadActivity::row(uint16_t index, XpUiSettingsRow& out) const {
  if (index >= rowItems_.size()) return false;
  const auto& value = rowItems_[index];
  out = {value.label, value.subtitle, value.value, 0, 0, 0, 1, 0};
  return true;
}
const char* FontDownloadActivity::headerSubtitle() const {
  if (painting.phase != FAMILY_LIST || !hasGroupScreen()) return nullptr;
  const int group = painting.groups.selected - 1;
  return group >= 0 && group < static_cast<int>(scriptGroupLabels_.size()) ? str(scriptGroupLabels_[group])
                                                                           : tr(STR_ALL_FONTS);
}
const char* FontDownloadActivity::fontConfirmLabel() const {
  const int index = painting.list.selected;
  if (filteredIndices_.empty()) return "";
  if (isUpdateAllRow(index)) return tr(STR_UPDATE);
  if (!isDownloadAllRow(index)) {
    const int family = familyIndexFromList(index);
    if (family >= 0 && families_[family].installed && !families_[family].hasUpdate) return tr(STR_DELETE);
  }
  return tr(STR_DOWNLOAD);
}
void FontDownloadActivity::prepareText() {
  if (painting.phase != DOWNLOADING) return;
  if (painting.family_index < 0 || painting.family_index >= static_cast<int>(families_.size())) return;
  statusText = std::string(tr(STR_DOWNLOADING)) + " " + str(families_[painting.family_index].name) + " (" +
               std::to_string(painting.file_index + 1) + "/" + std::to_string(painting.file_count) + ")";
}
const char* FontDownloadActivity::dynamicText(uint32_t handle) const {
  switch (handle) {
    case 0x40000013:
      return errorMessage_.c_str();
    case 0x40000017:
      return statusText.c_str();
    default:
      return "";
  }
}
