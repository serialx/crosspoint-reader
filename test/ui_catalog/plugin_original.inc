// Frozen from 074bac4a before P9-U8b.
void pluginAuthScreen(UiAppHost::UiScreen& screen, const char* authVerifyUrl, const char* authUserCode, fui::Rect& authQrRect) {
  fui::TextStyle centered = screen.theme().bodyText;
  centered.align = fui::TextAlign::Center;
  fui::TextStyle code = screen.theme().titleText;
  code.align = fui::TextAlign::Center;
  code.bold = true;
  const int16_t lh = screen.target().lineHeight(centered.font);
  const int16_t gap = screen.theme().spaceMd;

  screen.target().text(screen.takeTop(lh, gap), authVerifyUrl, centered);
  screen.target().text(screen.takeTop(screen.target().lineHeight(code.font), gap), authUserCode, code);

  constexpr int16_t qrSize = 180;
  const fui::Rect band = screen.takeTop(qrSize, gap);
  authQrRect = fui::Rect{static_cast<int16_t>(band.x + (band.width - qrSize) / 2), band.y, qrSize, qrSize};

  screen.target().text(screen.takeTop(lh), tr(STR_PLUGIN_AUTH_WAITING), centered);
}

