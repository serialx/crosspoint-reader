#include "OpdsSettingsActivity.h"

#include <GfxRenderer.h>
#include <HttpHeader.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
// Editable fields: Name, URL, Username, Password, Custom Header 1, Custom
// Header 2. Existing servers also show a Delete option (BASE_ITEMS + 1).
constexpr int BASE_ITEMS = 6;
constexpr int CUSTOM_HEADER_1_INDEX = 4;
constexpr int CUSTOM_HEADER_2_INDEX = 5;
}  // namespace

OpdsSettingsActivity::OpdsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const int serverIndex)
    : CatalogUiActivity("OpdsSettings", renderer, mappedInput, XP_UI_CATALOG_EDITOR, serverIndex) {}

int OpdsSettingsActivity::getMenuItemCount() const {
  return ui.new_server ? BASE_ITEMS : BASE_ITEMS + 1;  // +1 for Delete
}

void OpdsSettingsActivity::onEnter() {
  CatalogUiActivity::onEnter();
  if (!hasUiStorage()) return;

  ui.new_server = (ui.server_index < 0);
  ui.save_error = false;

  if (!ui.new_server) {
    // Edit flow: copy the selected server into local editable state.
    // Changes are persisted field-by-field through saveServer().
    const auto* server = OPDS_STORE.getServer(static_cast<size_t>(ui.server_index));
    if (server) {
      editServer = *server;
    } else {
      // Server was deleted between navigation and entering this screen — treat as new
      ui.new_server = true;
      ui.server_index = -1;
    }
  }
}

void OpdsSettingsActivity::activateIndex(const int index) {
  selectIndex(index);
  // Activation opens a keyboard or leaves the screen; a lingering flash would
  // gray an unrelated row.
  handleSelection();
}

bool OpdsSettingsActivity::saveServer() {
  bool success = false;

  if (ui.new_server) {
    // Create flow: first save inserts a new server record into the multi-server store.
    success = OPDS_STORE.addServer(editServer);
    if (success) {
      // After the first successful save, promote to an existing server so
      // subsequent field edits update in-place rather than creating duplicates.
      ui.new_server = false;
      ui.server_index = static_cast<int>(OPDS_STORE.getCount()) - 1;
    } else {
      LOG_ERR("OPS", "Failed to add OPDS server");
    }
  } else {
    // Edit flow: update the same server entry in-place.
    success = OPDS_STORE.updateServer(static_cast<size_t>(ui.server_index), editServer);
    if (!success) {
      LOG_ERR("OPS", "Failed to update OPDS server at index %d", ui.server_index);
    }
  }

  ui.save_error = !success;
  if (ui.save_error) {
    requestUpdate();
  }

  return success;
}

void OpdsSettingsActivity::handleSelection() {
  // Each field edit is saved immediately so partially configured servers
  // survive navigation and power-loss scenarios.
  if (selectedIndex() == 0) {
    // Server Name
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.name = kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SERVER_NAME),
                                                                   editServer.name, 63, InputType::Text),
                           handler);
  } else if (selectedIndex() == 1) {
    // Server URL
    const std::string prefillUrl = editServer.url.empty() ? "https://" : editServer.url;
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.url = (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_OPDS_SERVER_URL),
                                                                   prefillUrl, 127, InputType::Url),
                           handler);
  } else if (selectedIndex() == 2) {
    // Username
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.username = kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_USERNAME),
                                                                   editServer.username, 63, InputType::Text),
                           handler);
  } else if (selectedIndex() == 3) {
    // Password
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editServer.password = kb.text;
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PASSWORD),
                                                                   editServer.password, 63, InputType::Text),
                           handler);
  } else if (selectedIndex() == CUSTOM_HEADER_1_INDEX || selectedIndex() == CUSTOM_HEADER_2_INDEX) {
    // Custom Header 1 or 2 - single "Name: Value" line, parsed on save.
    const size_t slot = static_cast<size_t>(selectedIndex() == CUSTOM_HEADER_1_INDEX ? 0 : 1);
    const StrId label =
        selectedIndex() == CUSTOM_HEADER_1_INDEX ? StrId::STR_CUSTOM_HEADER_1 : StrId::STR_CUSTOM_HEADER_2;
    const std::string prefill = formatHttpHeaderLine(editServer.customHeaders[slot]);
    auto handler = [this, slot](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        parseHttpHeaderLine(kb.text, editServer.customHeaders[slot]);
        saveServer();
        requestUpdate();
      }
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(label), prefill, 160, InputType::Text),
        handler);
  } else if (selectedIndex() == BASE_ITEMS && !ui.new_server) {
    // Delete flow is only available for existing servers.
    if (!OPDS_STORE.removeServer(static_cast<size_t>(ui.server_index))) {
      LOG_ERR("OPS", "Failed to remove OPDS server at index %d", ui.server_index);
      ui.save_error = true;
      requestUpdate();
      return;
    }
    finish();
  }
}

void OpdsSettingsActivity::prepareRows() {
  for (size_t i = 0; i < OpdsServer::MAX_CUSTOM_HEADERS; ++i)
    headerDisplayBuf[i] = formatHttpHeaderLine(editServer.customHeaders[i], true);
}
bool OpdsSettingsActivity::rowAt(int index, XpUiCatalogRow& row) const {
  static constexpr StrId labels[] = {StrId::STR_SERVER_NAME,  StrId::STR_OPDS_SERVER_URL, StrId::STR_USERNAME,
                                     StrId::STR_PASSWORD,     StrId::STR_CUSTOM_HEADER_1, StrId::STR_CUSTOM_HEADER_2,
                                     StrId::STR_DELETE_SERVER};
  if (index < 0 || index >= getMenuItemCount()) return false;
  row.label = I18N.get(labels[index]);
  const char* value = nullptr;
  switch (index) {
    case 0:
      value = editServer.name.c_str();
      break;
    case 1:
      value = editServer.url.c_str();
      break;
    case 2:
      value = editServer.username.c_str();
      break;
    case 3:
      value = editServer.password.empty() ? "" : "******";
      break;
    case 4:
    case 5:
      value = headerDisplayBuf[index - 4].c_str();
      break;
    default:
      return true;
  }
  row.value = value[0] ? value : tr(STR_NOT_SET);
  return true;
}

const char* OpdsSettingsActivity::headerTitle() const {
  // Reuse STR_OPDS_BROWSER as the "edit existing server" title.
  // New server creation uses STR_ADD_SERVER.
  return ui.new_server ? tr(STR_ADD_SERVER) : tr(STR_OPDS_BROWSER);
}

void OpdsSettingsActivity::drawFooter() {
  CatalogUiActivity::drawFooter();
  if (ui.save_error) {
    GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
  }
}
