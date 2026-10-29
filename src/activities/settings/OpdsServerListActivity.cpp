#include "OpdsServerListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "OpdsSettingsActivity.h"
#include "activities/ActivityManager.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/OpdsFilename.h"

namespace fui = freeink::ui;

namespace {
// Normalizes a user-typed folder: trims spaces, "" => SD root, otherwise a
// single leading '/' and no trailing '/'. Cold path (runs once per edit).
std::string normalizeFolder(std::string v) {
  while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
  while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
  if (v.empty()) return "";
  if (v.front() != '/') v.insert(v.begin(), '/');
  while (v.size() > 1 && v.back() == '/') v.pop_back();
  if (v == "/") return "";  // a bare slash is SD root, same as empty
  return v;
}

// Label shown for the current OPDS filename format in the list subtitle.
StrId opdsFormatLabel(uint8_t format) {
  switch (format) {
    case static_cast<uint8_t>(OpdsFilenameFormat::TitleAuthor):
      return StrId::STR_FMT_TITLE_AUTHOR;
    case static_cast<uint8_t>(OpdsFilenameFormat::TitleOnly):
      return StrId::STR_FMT_TITLE;
    default:
      return StrId::STR_FMT_AUTHOR_TITLE;
  }
}
}  // namespace

int OpdsServerListActivity::getItemCount() const {
  int count = static_cast<int>(OPDS_STORE.getCount());
  // Picker mode appends "Add Server"; Settings also adds folder and format.
  count += ui.picker ? 1 : 3;
  return count;
}

OpdsServerListActivity::OpdsServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const bool pickerMode)
    : CatalogUiActivity("OpdsServerList", renderer, mappedInput, XP_UI_CATALOG_SERVERS, -1, pickerMode) {}

void OpdsServerListActivity::onEnter() {
  CatalogUiActivity::onEnter();
  if (!hasUiStorage()) return;

  // Reload from disk in case servers were added/removed by a subactivity or the web UI
  OPDS_STORE.loadFromFile();
  resetSelection();
  closeRouting();
}

bool OpdsServerListActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void OpdsServerListActivity::onBackButton() {
  if (ui.picker) {
    activityManager.goHome(HomeMenuItem::OPDS_BROWSER);
  } else {
    finish();
  }
}

const char* OpdsServerListActivity::headerTitle() const { return tr(STR_OPDS_SERVERS); }

void OpdsServerListActivity::activateIndex(const int index) {
  selectIndex(index);
  // Activation opens an editor/browser or repaints a new value; a lingering
  // flash would gray an unrelated row.
  handleSelection();
  requestUpdate();
}

void OpdsServerListActivity::handleSelection() {
  const auto serverCount = static_cast<int>(OPDS_STORE.getCount());

  if (ui.picker) {
    // Picker mode: selecting a server navigates to the OPDS browser
    if (selectedIndex() < serverCount) {
      const auto* server = OPDS_STORE.getServer(static_cast<size_t>(selectedIndex()));
      if (server) {
        activityManager.replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, *server));
      }
    } else {
      auto editor = makeUniqueNoThrow<OpdsSettingsActivity>(renderer, mappedInput, -1);
      if (!editor) {
        LOG_ERR("OPS", "OOM: OPDS settings activity");
        return;
      }
      startActivityForResult(std::move(editor), [this](const ActivityResult&) {
        OPDS_STORE.loadFromFile();
        closeRouting();
      });
    }
    return;
  }

  // Index layout: [servers 0..serverCount-1], [Add Server], [Download folder], [Filename format].
  if (selectedIndex() == serverCount + 1) {
    auto folderHandler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        const std::string norm = normalizeFolder(kb.text);
        strncpy(SETTINGS.opdsDownloadFolder, norm.c_str(), sizeof(SETTINGS.opdsDownloadFolder) - 1);
        SETTINGS.opdsDownloadFolder[sizeof(SETTINGS.opdsDownloadFolder) - 1] = '\0';
        SETTINGS.saveToFile();
        requestUpdate();
      }
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_OPDS_DOWNLOAD_FOLDER),
                                                std::string(SETTINGS.opdsDownloadFolder), 63, InputType::Text),
        folderHandler);
    return;
  }

  // "Filename format": picker like every other multi-option setting.
  if (selectedIndex() == serverCount + 2) {
    static constexpr StrId formatLabels[] = {StrId::STR_FMT_AUTHOR_TITLE, StrId::STR_FMT_TITLE_AUTHOR,
                                             StrId::STR_FMT_TITLE};
    optionPopup.show(StrId::STR_OPDS_FILENAME_FORMAT, formatLabels, static_cast<int>(OpdsFilenameFormat::Count),
                     SETTINGS.opdsFilenameFormat, [this](int idx) {
                       SETTINGS.opdsFilenameFormat = static_cast<uint8_t>(idx);
                       SETTINGS.saveToFile();
                     });
    requestUpdate();
    return;
  }

  // Settings mode: open editor for selected server, or create a new one
  auto resultHandler = [this](const ActivityResult&) {
    // Reload server list when returning from editor
    OPDS_STORE.loadFromFile();
    resetSelection();
    closeRouting();
  };

  if (selectedIndex() < serverCount) {
    startActivityForResult(std::make_unique<OpdsSettingsActivity>(renderer, mappedInput, selectedIndex()),
                           resultHandler);
  } else {
    startActivityForResult(std::make_unique<OpdsSettingsActivity>(renderer, mappedInput, -1), resultHandler);
  }
}

bool OpdsServerListActivity::rowAt(int index, XpUiCatalogRow& row) const {
  const int count = static_cast<int>(OPDS_STORE.getCount());
  if (index < 0 || index >= getItemCount()) return false;
  if (index < count) {
    const auto* server = OPDS_STORE.getServer(index);
    if (!server) return false;
    row.label = server->name.empty() ? server->url.c_str() : server->name.c_str();
    row.subtitle = server->name.empty() ? nullptr : server->url.c_str();
  } else if (index == count)
    row.label = tr(STR_ADD_SERVER);
  else if (index == count + 1) {
    row.label = tr(STR_OPDS_DOWNLOAD_FOLDER);
    row.subtitle = SETTINGS.opdsDownloadFolder[0] ? SETTINGS.opdsDownloadFolder : tr(STR_OPDS_SD_ROOT);
  } else {
    row.label = tr(STR_OPDS_FILENAME_FORMAT);
    row.subtitle = I18N.get(opdsFormatLabel(SETTINGS.opdsFilenameFormat));
  }
  return true;
}

void OpdsServerListActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the base skeleton.
  RustActivityAdapter::render(std::move(lock));
}
