#include "Renderer.h"
#include "Fixtures.h"
#include <array>
#include <iostream>

const char* prepareVisualText(const char*, std::string&, BidiUtils::BidiBaseDir);
using Style = EpdFontFamily::Style;
using Dir = BidiUtils::BidiBaseDir;
using Mode = GfxRenderer::TextMeasureMode;
uint64_t checksum = 14695981039346656037ULL;
size_t cases = 0;
void observe(int value) {
  for (int i=0;i<4;++i) { checksum ^= static_cast<uint32_t>(value)>>(i*8)&255; checksum *= 1099511628211ULL; }
}
int main(int argc, char** argv) {
  EpdFont regular(&notosans_12_regular), bold(&notosans_12_bold), italic(&notosans_12_italic), bi(&notosans_12_bolditalic), ubuntu(&ubuntu_12_regular);
  GfxRenderer renderer;
  renderer.fontMap.emplace(1,EpdFontFamily(&regular,&bold,&italic,&bi));
  renderer.fontMap.emplace(2,EpdFontFamily(&ubuntu,nullptr,&italic));
  makeSdFont();
  if (argc>1) {
    std::ifstream file(argv[1],std::ios::binary);
    require(file.good(),"private SD fixture open");
    sdFontTestFile.assign(std::istreambuf_iterator<char>(file),{});
  }
  SdCardFont sd;
  require(sd.load("fixture.cpfont"),"SD load");
  renderer.fontMap.emplace(3,EpdFontFamily(sd.getEpdFont(0),sd.getEpdFont(1),sd.getEpdFont(2),sd.getEpdFont(3)));
  renderer.sdCardFonts_.emplace(3,&sd);
  renderer.fallbackFontMap_.emplace(2,3);
  auto strings=corpus();
  for (int warm=0;warm<3;++warm) {
    if (warm==1) sd.buildAdvanceTable("AV fi \u0301\u0327\u2588");
    if (warm==2) for (auto& s:strings) sd.buildAdvanceTable(s.c_str());
    for (int font : {1,2,3,99}) for (const auto& logical:strings) for (auto dir:{Dir::AUTO,Dir::LTR,Dir::RTL}) {
      std::string visualBuffer;
      const char* visual=prepareVisualText(logical.c_str(),visualBuffer,dir);
      for (int bits=0;bits<256;++bits) {
        auto style=static_cast<Style>(bits);
        int resolved=renderer.resolveTextFontId(font,logical.c_str(),style);
        observe(resolved);
        observe(renderer.getTextWidth(resolved,visual,style,Dir::LTR));
        observe(renderer.getSpaceWidth(font,style));
        observe(renderer.getSpaceAdvance(font,'A','V',style));
        int32_t advance=-123; int top=-456;
        observe(renderer.getCodepointMetrics(font,0x2588,style,advance,top)); observe(advance); observe(top);
        for (int tracking : {-128,-3,0,2,127}) for (auto mode:{Mode::Layout,Mode::Render}) {
          observe(renderer.getTextAdvanceX(resolved,visual,style,tracking,Dir::LTR,mode));
          observe(renderer.getKerning(font,'A','V',style,tracking));
          ++cases;
        }
      }
    }
  }
  for (int font:{1,2,3,99}) { observe(renderer.getFontAscenderSize(font)); observe(renderer.getTextHeight(font)); observe(renderer.getLineHeight(font)); observe(renderer.getLineHeight(font,0.85f)); }
  std::cout << "cases=" << cases << " checksum=" << std::hex << checksum << '\n';
}
