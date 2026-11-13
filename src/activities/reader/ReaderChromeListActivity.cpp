#include "ReaderChromeListActivity.h"

#include <I18n.h>
#include <Memory.h>

#include "CrossPointSettings.h"
#include "RustReaderDrawing.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

bool ReaderChromeCommands::reserve(uint16_t count) {
  if (commands && count <= capacity) return true;
  if (count > 128) return false;
  commands.reset();
  capacity = std::max(count, capacity);
  commands = makeUniqueNoThrow<XpUiCommand[]>(capacity);
  if (!commands) {
    LOG_ERR("RUI", "Cannot allocate %u command bytes", static_cast<unsigned>(capacity * sizeof(XpUiCommand)));
    return false;
  }
  return true;
}

bool ReaderChromeCommands::grow() {
  const auto next = xp_ui_command_next_capacity(capacity, 0, 16, 128);
  return next && reserve(next);
}

void sampleReaderChromeTheme(const GfxRenderer& renderer, XpUiReaderTheme& theme, bool bookmarks) {
  XpUiReaderThemeFacts facts{};
  sampleReaderChildTheme(renderer, theme, facts, false);
  facts.body_line_height = renderer.getLineHeight(theme.body_font);
  xp_ui_reader_theme_compose(&theme, &facts, bookmarks ? XP_UI_READER_THEME_BOOKMARKS : XP_UI_READER_THEME_LIST);
}
