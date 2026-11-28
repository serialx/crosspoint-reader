#include "KOReaderSettingsActivity.h"

#include <GfxRenderer.h>
#include <HttpHeader.h>
#include <I18n.h>
#include <xp_ui_koreader.h>

#include <memory>
#include <string>
#include <utility>

#include "KOReaderAuthActivity.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace {
constexpr int IDX_USERNAME = 0;
constexpr int IDX_PASSWORD = 1;
constexpr int IDX_SERVER_URL = 2;
constexpr int IDX_SERVER_TYPE = 3;
constexpr int IDX_CUSTOM_HEADER_1 = 4;
constexpr int IDX_CUSTOM_HEADER_2 = 5;
constexpr int IDX_MATCH_METHOD = 6;
constexpr int IDX_SEND_METADATA = 7;
constexpr int IDX_SYNC_BEHAVIOR = 8;
constexpr int IDX_SYNC_CLIPPINGS = 9;
constexpr int IDX_SIGN_UP = 10;
constexpr int IDX_AUTHENTICATE = 11;

constexpr StrId menuNames[KOReaderSettingsActivity::MENU_ITEMS] = {
    StrId::STR_USERNAME,        StrId::STR_PASSWORD,        StrId::STR_SYNC_SERVER_URL,   StrId::STR_SERVER_TYPE,
    StrId::STR_CUSTOM_HEADER_1, StrId::STR_CUSTOM_HEADER_2, StrId::STR_DOCUMENT_MATCHING, StrId::STR_SEND_METADATA,
    StrId::STR_SYNC_BEHAVIOR,   StrId::STR_SYNC_CLIPPINGS,  StrId::STR_SIGN_UP,           StrId::STR_AUTHENTICATE};

constexpr int SERVER_TYPE_ITEMS = 3;
constexpr StrId serverTypeNames[SERVER_TYPE_ITEMS] = {StrId::STR_CROSSPOINT, StrId::STR_KOSYNC_SERVER,
                                                      StrId::STR_OTHER};
}  // namespace

KOReaderSettingsActivity::KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& input)
    : SettingsUiActivity("KOReaderSettings", renderer, input, XP_UI_SETTINGS_SECTION) {
  for (int i = 0; i < MENU_ITEMS; ++i) {
    rowItems[i].label = I18N.get(menuNames[xp_ui_koreader_label(i) - 32]);
    rowItems[i].actionValue = i;
  }
}
const char* KOReaderSettingsActivity::headerTitle() const { return tr(STR_KOREADER_SYNC); }
bool KOReaderSettingsActivity::handleCustomInput() {
  bool discard;
  {
    ModelLock lock(*this);
    discard = discardPopup;
    discardPopup = false;
  }
  if (discard) {
    const bool wasActive = optionPopup.isActive();
    optionPopup.dismiss();
    requestUpdate();
    return wasActive;
  }
  if (uiSceneFailed()) return false;
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}
void KOReaderSettingsActivity::activateIndex(int index) {
  if (uiSceneFailed() || optionPopup.isActive() || index < 0 || index >= MENU_ITEMS) return;
  uint8_t current = 0;
  switch (index) {
    case IDX_MATCH_METHOD:
      current = static_cast<uint8_t>(KOREADER_STORE.getMatchMethod());
      break;
    case IDX_SEND_METADATA:
      current = KOREADER_STORE.getSendMetadata();
      break;
    case IDX_SYNC_BEHAVIOR:
      current = static_cast<uint8_t>(KOREADER_STORE.getSyncBehavior());
      break;
    case IDX_SYNC_CLIPPINGS:
      current = KOREADER_STORE.getSyncClippings();
      break;
  }
  const auto effect = xp_ui_koreader_edit(index, current, KOREADER_STORE.hasCredentials());
  switch (effect.kind) {
    case XP_KO_EDIT_KEYBOARD: {
      const auto inputType = index == IDX_PASSWORD     ? InputType::Password
                             : index == IDX_SERVER_URL ? InputType::Url
                                                       : InputType::Text;
      const char* label = nullptr;
      std::string prefill;
      size_t limit = 64;
      switch (index) {
        case IDX_USERNAME:
          label = tr(STR_KOREADER_USERNAME);
          prefill = KOREADER_STORE.getUsername();
          break;
        case IDX_PASSWORD:
          label = tr(STR_KOREADER_PASSWORD);
          prefill = KOREADER_STORE.getPassword();
          break;
        case IDX_SERVER_URL:
          label = tr(STR_SYNC_SERVER_URL);
          prefill = KOREADER_STORE.getServerUrl().empty() ? "https://" : KOREADER_STORE.getServerUrl();
          limit = 128;
          break;
        case IDX_CUSTOM_HEADER_1:
        case IDX_CUSTOM_HEADER_2:
          label = I18N.get(menuNames[index]);
          prefill = formatHttpHeaderLine(KOREADER_STORE.getCustomHeaders()[index - IDX_CUSTOM_HEADER_1]);
          limit = 160;
          break;
      }
      startActivityForResult(
          std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, label, prefill, limit, inputType),
          [this, index](const ActivityResult& result) {
            if (result.isCancelled || uiSceneFailed()) return;
            const auto& value = std::get<KeyboardResult>(result.data).text;
            switch (index) {
              case IDX_USERNAME:
                KOREADER_STORE.setCredentials(value, KOREADER_STORE.getPassword());
                break;
              case IDX_PASSWORD:
                KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), value);
                break;
              case IDX_SERVER_URL:
                KOREADER_STORE.setServerUrl(
                    xp_ui_koreader_url_empty(reinterpret_cast<const uint8_t*>(value.data()), value.size()) ? ""
                                                                                                           : value);
                break;
              case IDX_CUSTOM_HEADER_1:
              case IDX_CUSTOM_HEADER_2: {
                HttpHeader header;
                parseHttpHeaderLine(value, header);
                KOREADER_STORE.setCustomHeader(index - IDX_CUSTOM_HEADER_1, header.name, header.value);
                break;
              }
            }
            KOREADER_STORE.saveToFile();
          });
      break;
    }
    case XP_KO_EDIT_POPUP:
      optionPopup.show(StrId::STR_SERVER_TYPE, serverTypeNames, SERVER_TYPE_ITEMS,
                       static_cast<int>(KOREADER_STORE.getServerType()), [this](int selected) {
                         if (uiSceneFailed() || selected == static_cast<int>(KOREADER_STORE.getServerType())) return;
                         KOREADER_STORE.setServerType(static_cast<KOReaderServerType>(selected));
                         KOREADER_STORE.saveToFile();
                       });
      requestUpdate();
      break;
    case XP_KO_EDIT_SAVE:
      switch (index) {
        case IDX_MATCH_METHOD:
          KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(effect.value));
          break;
        case IDX_SEND_METADATA:
          KOREADER_STORE.setSendMetadata(effect.value);
          break;
        case IDX_SYNC_BEHAVIOR:
          KOREADER_STORE.setSyncBehavior(static_cast<KOReaderSyncBehavior>(effect.value));
          break;
        case IDX_SYNC_CLIPPINGS:
          KOREADER_STORE.setSyncClippings(effect.value);
          break;
      }
      KOREADER_STORE.saveToFile();
      requestUpdate();
      break;
    case XP_KO_EDIT_AUTH:
      startActivityForResult(
          std::make_unique<KOReaderAuthActivity>(
              renderer, mappedInput,
              effect.value ? KOReaderAuthActivity::Mode::SIGN_UP : KOReaderAuthActivity::Mode::AUTHENTICATE),
          [](const ActivityResult&) {});
      break;
    default:
      break;
  }
}
void KOReaderSettingsActivity::prepareRows() {
  for (int i = 0; i < MENU_ITEMS; i++) {
    if (i == IDX_USERNAME) {
      const auto username = KOREADER_STORE.getUsername();
      rowValues[i] = username.empty() ? tr(STR_NOT_SET) : username;
    } else if (i == IDX_PASSWORD) {
      rowValues[i] = KOREADER_STORE.getPassword().empty() ? tr(STR_NOT_SET) : "******";
    } else if (i == IDX_SERVER_URL) {
      rowValues[i] = KOREADER_STORE.getServerUrl();
      if (rowValues[i].empty()) {
        std::string defaultUrl = KOREADER_STORE.getBaseUrl();
        const auto schemeEnd = defaultUrl.find("://");
        if (schemeEnd != std::string::npos) defaultUrl.erase(0, schemeEnd + 3);
        rowValues[i] = std::string(tr(STR_DEFAULT_VALUE)) + ": " + defaultUrl;
      }
    } else if (i == IDX_SERVER_TYPE) {
      const auto type = KOREADER_STORE.getServerType();
      rowValues[i] = type == KOReaderServerType::CROSSPOINT ? tr(STR_CROSSPOINT)
                     : type == KOReaderServerType::KOSYNC   ? tr(STR_KOSYNC_SERVER)
                                                            : tr(STR_OTHER);
    } else if (i == IDX_CUSTOM_HEADER_1 || i == IDX_CUSTOM_HEADER_2) {
      const size_t slot = static_cast<size_t>(i == IDX_CUSTOM_HEADER_1 ? 0 : 1);
      rowValues[i] = formatHttpHeaderLine(KOREADER_STORE.getCustomHeaders()[slot], true);
      if (rowValues[i].empty()) rowValues[i] = tr(STR_NOT_SET);
    } else if (i == IDX_MATCH_METHOD) {
      rowValues[i] =
          KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME ? tr(STR_FILENAME) : tr(STR_BINARY);
    } else if (i == IDX_SEND_METADATA || i == IDX_SYNC_CLIPPINGS) {
      rowValues[i].clear();
    } else if (i == IDX_SYNC_BEHAVIOR) {
      rowValues[i] =
          KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART ? tr(STR_SMART_SYNC) : tr(STR_ASK_EVERY_TIME);
    } else {
      rowValues[i] = KOREADER_STORE.hasCredentials() ? "" : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
    }
    rowItems[i].value = rowValues[i].empty() ? nullptr : rowValues[i].c_str();
  }
  setCheckboxRow(rowItems[IDX_SEND_METADATA], KOREADER_STORE.getSendMetadata());
  setCheckboxRow(rowItems[IDX_SYNC_CLIPPINGS], KOREADER_STORE.getSyncClippings());
}
void KOReaderSettingsActivity::render(RenderLock&& lock) {
  bool discard;
  {
    ModelLock modelLock(*this);
    discard = discardPopup;
  }
  if (!discard && !uiSceneFailed() && optionPopup.processRender(renderer, mappedInput)) return;
  SettingsUiActivity::render(std::move(lock));
}
