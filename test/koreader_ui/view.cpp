#include "../ui_settings/Recorder.h"
#include <memory>
#include <string>
#include <utility>
namespace fui = freeink::ui;
using UiScreen = fui::Screen<64>;
struct Rect { int x,y,width,height; operator fui::Rect() const { return {int16_t(x),int16_t(y),int16_t(width),int16_t(height)}; } };
struct RenderLock {};
namespace EpdFontFamily { enum Style { REGULAR, BOLD }; }
constexpr int UI_10_FONT_ID=0;
const char* translation(const char*);
#define tr(id) translation(#id)
struct Metrics { int topPadding=10,headerHeight=38,buttonHintsHeight=48,verticalSpacing=8,listRowHeight=44; } metrics;
struct GfxRenderer {
  int width=528,height=792;
  Recorder target;
  int getScreenWidth() const {return width;}
  int getScreenHeight() const {return height;}
  int getLineHeight(int f) {return target.lineHeight(f);}
  void clearScreen() {puts("clear");}
  void displayBuffer() {puts("refresh FAST");}
  void drawCenteredText(int font,int top,const char* text,bool=true,EpdFontFamily::Style style=EpdFontFamily::REGULAR) {
    fui::TextStyle s;s.font=font;s.bold=style;s.align=fui::TextAlign::Center;
    target.text({0,static_cast<int16_t>(top),static_cast<int16_t>(width),target.lineHeight(font)},text,s);
  }
};
struct UITheme {
  static UITheme& getInstance() {static UITheme t;return t;}
  const Metrics& getMetrics() {return metrics;}
  Rect getScreenSafeArea(GfxRenderer& r,bool,bool) {return {0,0,static_cast<int16_t>(r.width),static_cast<int16_t>(r.height)};}
  Rect getHeaderRect(GfxRenderer& r,bool,bool) {return {0,static_cast<int16_t>(metrics.topPadding),static_cast<int16_t>(r.width),static_cast<int16_t>(metrics.headerHeight)};}
  static void drawCenteredText(GfxRenderer& r,Rect,int f,int y,const char* text,bool white=true,EpdFontFamily::Style s=EpdFontFamily::REGULAR) {r.drawCenteredText(f,y,text,white,s);}
};
struct {
  void drawHeader(GfxRenderer&,Rect r,const char* title) {Recorder::rect("header",r);printf(" %s\n",title);}
  void drawButtonHints(GfxRenderer&,const char* a,const char* b,const char* c,const char* d) {printf("hints [%s][%s][%s][%s]\n",a,b,c,d);}
} GUI;
struct Input {
  bool touch=false;
  bool hasTouch() {return touch;}
  struct Labels {const char *btn1,*btn2,*btn3,*btn4;};
  Labels mapLabels(const char*a,const char*b,const char*c,const char*d) {return {a,b,c,d};}
};
constexpr int icon_download_24=0,icon_upload_24=1;
namespace freeink::ui { BitmapRef bitmapFromIcon(int) {static const uint8_t bits[72]={};return {bits,24,24};} }
struct Epub {
  int getTocIndexForSpineIndex(int index) {return index;}
  struct Item {std::string title;};
  Item getTocItem(int) {return {"Remote chapter"};}
};
struct KOReaderSyncActivity {
  enum State { WIFI_SELECTION,CONNECTING,SYNCING,SHOWING_RESULT,UPLOADING,UPLOAD_COMPLETE,SYNC_COMPLETE,NO_REMOTE_PROGRESS,SYNC_FAILED,NO_CREDENTIALS };
  State state=SHOWING_RESULT;
  GfxRenderer renderer;Input mappedInput;
  std::shared_ptr<Epub> epub=std::make_shared<Epub>();
  struct {int spineIndex=0,pageNumber=2,totalPages=10;} localPosition,remotePosition;
  struct {float percentage=.75;std::string device="Oracle reader";} remoteProgress;
  struct {float percentage=.25;} localProgress;
  std::string localChapterName="Local chapter",statusMessage="Status message";
  int selectedOption=0;
  fui::ThemeTokens theme;
  void buildResultScreen(UiScreen&);
  void render(RenderLock&&);
  void renderUi() {
    fui::DeviceContext device;device.width=renderer.width;device.height=renderer.height;device.hasTouch=mappedInput.touch;
    fui::InteractionBuffer<64> hits;fui::Frame<64> frame(renderer.target,device,{},hits);UiScreen screen(frame,theme);
    buildResultScreen(screen);
    for(size_t i=0;i<hits.count();++i){const auto& h=hits.data()[i];Recorder::rect("hit",h.rect);printf(" %u %d\n",h.action,h.value);}
  }
};
constexpr unsigned ACTION_ROW=1;
struct KOReaderAuthActivity {
  enum State { WIFI_SELECTION,CONNECTING,AUTHENTICATING,SUCCESS,FAILED };
  enum class Mode {AUTHENTICATE,SIGN_UP};
  State state=AUTHENTICATING;Mode mode=Mode::AUTHENTICATE;
  std::string statusMessage="Status message",errorMessage="Service error";
  GfxRenderer renderer;Input mappedInput;
  void render(RenderLock&&);
};
// METHODS
int main() {
  for(int width:{480,528,792,800})for(bool touch:{false,true})for(int theme:{0,1,2}) {
    KOReaderSyncActivity activity;activity.renderer.width=width;activity.renderer.height=width>600?528:792;activity.renderer.target.clip={0,0,static_cast<int16_t>(width),static_cast<int16_t>(activity.renderer.height)};
    activity.mappedInput.touch=touch;activity.theme=fui::themeTokensForLineHeight(24);
    activity.theme.listRowRadius=theme==0?0:12;activity.theme.listSelectionStyle=theme==0?fui::SelectionStyle::InvertFill:fui::SelectionStyle::LightPill;
    activity.theme.listInset=theme==2?5:0;activity.theme.listRowGap=theme==2?3:0;
    for(int phase=0;phase<10;++phase)for(int option:{0,1}) {
      printf("sync %d %d %d %d %d\n",width,touch,theme,phase,option);activity.state=static_cast<KOReaderSyncActivity::State>(phase);activity.selectedOption=option;activity.render({});
    }
    for(bool signup:{false,true})for(int phase=0;phase<5;++phase) {
      printf("auth %d %d %d %d %d\n",width,touch,theme,signup,phase);
      KOReaderAuthActivity auth;auth.renderer.width=width;auth.renderer.height=activity.renderer.height;auth.mode=signup?KOReaderAuthActivity::Mode::SIGN_UP:KOReaderAuthActivity::Mode::AUTHENTICATE;auth.state=static_cast<KOReaderAuthActivity::State>(phase);auth.render({});
    }
  }
}
