#include "ClearCacheActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

void ClearCacheActivity::onEnter() {
  UpdateUiActivity::onEnter();
  requestUpdate();
}
void ClearCacheActivity::beginClear() {
  LOG_DBG("CLEAR_CACHE", "User confirmed, starting cache clear");
  {
    event(XP_UI_UPDATE_CACHE_BEGIN);
  }
  requestUpdateAndWait();
  clearCache();
}
void ClearCacheActivity::clearCache() {
  LOG_DBG("CLEAR_CACHE", "Clearing cache...");

  // Open .crosspoint directory
  auto root = Storage.open("/.crosspoint");
  if (!root || !root.isDirectory()) {
    LOG_DBG("CLEAR_CACHE", "Failed to open cache directory");
    if (root) root.close();
    event(XP_UI_UPDATE_CACHE_RESULT, 0);
    requestUpdate();
    return;
  }

  clearedCount = 0;
  failedCount = 0;
  char name[128];

  // Iterate through all entries in the directory
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    String itemName(name);

    // Only delete directories matching known book cache names.
    if (file.isDirectory() && isBookCacheDirectoryName(itemName.c_str())) {
      String fullPath = "/.crosspoint/" + itemName;
      LOG_DBG("CLEAR_CACHE", "Removing cache: %s", fullPath.c_str());

      file.close();  // Close before attempting to delete

      if (Storage.removeDir(fullPath.c_str())) {
        clearedCount++;
      } else {
        LOG_ERR("CLEAR_CACHE", "Failed to remove: %s", fullPath.c_str());
        failedCount++;
      }
    } else {
      file.close();
    }
  }
  root.close();

  LOG_DBG("CLEAR_CACHE", "Cache cleared: %d removed, %d failed", clearedCount, failedCount);

  event(XP_UI_UPDATE_CACHE_RESULT, 1);
  requestUpdate();
}
void ClearCacheActivity::performAction(XpUiUpdateAction action, int32_t) {
  if (action == XP_UI_UPDATE_CLEAR)
    beginClear();
  else if (action == XP_UI_UPDATE_FINISH)
    finish();
}
void ClearCacheActivity::prepareText() {
  if (painting.phase != 2) return;
  resultText = std::to_string(clearedCount) + " " + tr(STR_ITEMS_REMOVED);
  if (failedCount > 0) resultText += ", " + std::to_string(failedCount) + " " + tr(STR_FAILED_LOWER);
}
const char* ClearCacheActivity::dynamicText(uint32_t handle) const {
  return handle == 0x40000018 ? resultText.c_str() : "";
}
