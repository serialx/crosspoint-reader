#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <cstdlib>
#include <iostream>
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
static void expect(const char* name, std::initializer_list<const char*> messages) {
  std::vector<std::string> wanted;
  for (auto s : messages) wanted.emplace_back(s);
  if (h6CandidateLogs != wanted) {
    std::cerr << name << ": diagnostic mismatch\n";
    for (const auto& s : wanted) std::cerr << "expected: " << s << '\n';
    for (const auto& s : h6CandidateLogs) std::cerr << "actual: " << s << '\n';
    std::exit(1);
  }
  h6CandidateLogs.clear();
}
int main() {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  h6CandidateLogs.clear();
  renderer.getTextWidth(404, "abc");
  expect("width missing font", {"ERR GFX Font 404 not found"});
  renderer.getTextWidth(404, "");
  expect("empty width", {});
  renderer.getTextAdvanceX(404, "abc", EpdFontFamily::REGULAR);
  expect("advance missing font", {"ERR GFX Font 404 not found"});
  renderer.getSpaceWidth(404);
  expect("space missing font", {"ERR GFX Font 404 not found"});
  renderer.drawText(404, 0, 0, "abc");
  expect("draw missing font", {"ERR GFX Font 404 not found"});
  renderer.drawTextRotated90CW(404, 0, 0, "abc");
  expect("rotated missing font", {"ERR GFX Font 404 not found"});
  renderer.getFontAscenderSize(404);
  expect("ascender missing font", {"ERR GFX Font 404 not found"});
  renderer.getLineHeight(404);
  expect("line height missing font", {"ERR GFX Font 404 not found"});
  renderer.getTextHeight(404);
  expect("text height missing font", {"ERR GFX Font 404 not found"});
  EpdFontData empty{};
  EpdFont font(&empty);
  renderer.insertFont(1, EpdFontFamily(&font));
  h6CandidateLogs.clear();
  renderer.drawText(1, 0, 0, "A");
  expect("normal missing glyph", {"ERR GFX No glyph for codepoint 65"});
  renderer.drawTextRotated90CW(1, 0, 0, "A");
  expect("rotated missing glyph", {"ERR GFX No glyph for codepoint 65"});
  renderer.drawText(1, 0, 0, "A", true, EpdFontFamily::SUP);
  expect("scaled missing glyph stays silent", {});
  renderer.insertFont(2, EpdFontFamily(&font));
  h6CandidateLogs.clear();
  std::string longText(129, 'A'), out = "unchanged";
  if (BidiUtils::applyBidiVisual(longText.c_str(), out) || out != "unchanged") return 1;
  const char* overflow = "DBG BIDI applyBidiVisual: input exceeds BIDI_MAX_LINE (128 chars), returning unprocessed";
  expect("direct bidi overflow", {overflow});
  renderer.getTextWidth(2, longText.c_str(), EpdFontFamily::REGULAR, BidiUtils::BidiBaseDir::RTL);
  expect("native bidi overflow", {overflow});
  std::string token;
  for (int i = 0; i < 129; ++i) token += "ب";
  BidiUtils::appendShapedRtlTokens(token.c_str(), out);
  expect("token bidi overflow", {overflow});
  BidiUtils::applyBidiVisual(std::string(128, 'A').c_str(), out);
  expect("bidi boundary", {});
  std::cout << "renderer and bidi diagnostic cases passed\n";
}
