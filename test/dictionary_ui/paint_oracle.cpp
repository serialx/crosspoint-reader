#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
struct Rect { int x,y,width,height; };
struct EpdFontFamily { enum Style { REGULAR }; };
namespace BidiUtils { enum class BidiBaseDir { AUTO }; }
enum class Color { LightGray };
struct HalDisplay { enum { FAST_REFRESH }; };
struct GfxRenderer {
  mutable uint8_t bytes[52272];
  int orientation=0;
  int getScreenWidth() const {return orientation%2==0 ? 528:792;}
  int getScreenHeight() const {return orientation%2==0 ? 792:528;}
  struct Cache {void prewarmCache(int,const char*,uint8_t) {}} cache;
  Cache* getFontCacheManager() const {return const_cast<Cache*>(&cache);}
  void drawPixel(int x,int y,bool ink) const {
    if(x<0||y<0||x>=getScreenWidth()||y>=getScreenHeight())return;
    int px=x,py=y;
    switch(orientation) {case 0:px=y;py=527-x;break;case 1:px=791-x;py=527-y;break;case 2:px=791-y;py=x;break;}
    auto& byte=bytes[py*99+px/8];uint8_t mask=0x80>>(px%8);
    if(ink)byte&=~mask;else byte|=mask;
  }
  void fillRect(int x,int y,int w,int h,bool ink) const {for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)drawPixel(i,j,ink);}
  void fillRectDither(int x,int y,int w,int h,Color) const {for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)drawPixel(i,j,i%2==0&&j%2==0);}
  void drawRect(int x,int y,int w,int h,bool ink) const {
    fillRect(x,y,w,1,ink);fillRect(x,y+h-1,w,1,ink);fillRect(x,y,1,h,ink);fillRect(x+w-1,y,1,h,ink);
  }
  void drawText(int,int x,int y,const char*,bool,EpdFontFamily::Style,BidiUtils::BidiBaseDir,int) const {
    for(int j=1;j<6;++j)for(int i=1;i<12;++i)if((i+j)%3!=0)drawPixel(x+i,y+j,true);
  }
  void displayBuffer(int=0) {}
};
struct Input {
  struct Labels {const char *btn1,*btn2,*btn3,*btn4;};
  bool hasTouch() const {return false;}
  Labels mapLabels(const char* a,const char* b,const char* c,const char* d) const {return {a,b,c,d};}
};
struct Theme {
  void drawSelectionHandle(const GfxRenderer&,Rect,bool) const {}
  void drawSelectionActions(const GfxRenderer&,Rect,int) const {}
  void drawButtonHints(const GfxRenderer&,const char*,const char*,const char*,const char*) const {}
} GUI;
enum {STR_BACK,STR_SELECT,STR_DIR_LEFT,STR_DIR_RIGHT,STR_LOOKUP,STR_DONE};
const char* tr(int) {return "";}
struct ClipSelectionActivity {
  enum class Mode {Clip,Lookup,DefinitionLookup};
  struct WordBox {
    int16_t x,y,width,height;uint16_t row;uint8_t pageOffset;bool isRtl;
    int fontId=0;const char* text="word";EpdFontFamily::Style style=EpdFontFamily::REGULAR;int8_t characterSpacing=0;
  };
  GfxRenderer renderer;Input mappedInput;
  struct Popup {bool isActive() const {return false;}} actionPopup;
  std::vector<WordBox> words;size_t wordCount=0;
  int selected=0,rangeStart=-1,currentPageOffset=0;
  int lastRenderedPageOffset=-1,lastRenderedSelected=-1,lastRenderedRangeStart=-1;
  int lastRenderedTextOffset=0,lastRenderedTextXOffset=0;
  bool touchDragSelecting=false;Mode mode=Mode::Clip;
  int textOffset() const {return 0;}int textXOffset() const {return 0;}
  bool isLookup() const {return mode!=Mode::Clip;}
  Rect handleRect(int,bool) const {return {};}
  Rect actionRect() const {return {};}
  void prewarmWord(int) const;
  void ditherGapBetween(const WordBox&,const WordBox&,int,int) const;
  void clearGapBetween(const WordBox&,const WordBox&,int,int) const;
  void drawWordClean(int,int,int) const;
  void drawWordHighlight(int,int,int,int,int) const;
  bool renderIncremental();void drawSelection() const;
};
// METHODS
int main() {
  for(int count:{240,720})for(int orientation=0;orientation<4;++orientation) {
    ClipSelectionActivity a;a.renderer.orientation=orientation;
    for(int i=0;i<count;++i)a.words.push_back({static_cast<int16_t>(5+i%8*60),static_cast<int16_t>(10+(i/8%30)*24),
      static_cast<int16_t>(24+i%3*6),18,static_cast<uint16_t>(i/8),static_cast<uint8_t>(i/240),false});
    a.wordCount=a.words.size();
    for(int page=0;page<count/240;++page) {
      a.currentPageOffset=page;a.selected=page*240+100;a.rangeStart=page*240+10;
      std::fill_n(a.renderer.bytes,52272,255);a.drawSelection();
      std::fwrite(a.renderer.bytes,1,52272,stdout);
      a.lastRenderedPageOffset=page;a.lastRenderedSelected=a.selected;a.lastRenderedRangeStart=a.rangeStart;
      for(int selected:{110,90,10,0,5}) {
        a.selected=page*240+selected;
        if(!a.renderIncremental())return 1;
        std::fwrite(a.renderer.bytes,1,52272,stdout);
      }
    }
  }
}
