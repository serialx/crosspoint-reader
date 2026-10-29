// Host leaves for the original activity effects; no network, flash, or device access.
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

static void record(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  puts("");
}
#define LOG_DBG(origin, ...) record("DBG " origin " " __VA_ARGS__)
#define LOG_INF(origin, ...) record("INF " origin " " __VA_ARGS__)
#define LOG_ERR(origin, ...) record("ERR " origin " " __VA_ARGS__)
#define CROSSPOINT_VERSION "baseline"
#define UI_10_FONT_ID 10
struct EpdFontFamily { enum Style { REGULAR, BOLD }; };
struct Rect { int x,y; union {int w;int width;}; union {int h;int height;}; Rect(int a,int b,int c,int d):x(a),y(b),w(c),h(d){} };
struct FontCacheManager { void releaseSdFontCaches(){record("font-cache.release");} };
struct GfxRenderer {
  int width=528, height=792;
  FontCacheManager cache;
  int getScreenWidth() const {return width;}
  int getScreenHeight() const {return height;}
  int getLineHeight(int) const {return 24;}
  void drawRect(int x,int y,int w,int h) const {record("paint.rect %d %d %d %d",x,y,w,h);}
  void fillRect(int x,int y,int w,int h) const {record("paint.fill %d %d %d %d",x,y,w,h);}
  void clearScreen(){record("paint.clear");}
  void displayBuffer(){record("panel.fast");}
  void drawCenteredText(int,int y,const char* text,bool=true,EpdFontFamily::Style style=EpdFontFamily::REGULAR) const {record("paint.center %d %d %s",y,style,text);}
  void drawText(int,int x,int y,const char* text){record("paint.text %d %d %s",x,y,text);}
  FontCacheManager* getFontCacheManager(){return &cache;}
};
struct MappedInputManager {
  enum class Button {Back,Confirm,NavNext,NavPrevious};
  uint32_t pressed=0,released=0,held=0; bool tap=false,home=false;
  bool wasPressed(Button b)const{return pressed&(1u<<int(b));}
  bool wasReleased(Button b)const{return released&(1u<<int(b));}
  bool isPressed(Button b)const{return held&(1u<<int(b));}
  bool wasScreenTapped(int&,int&)const{return tap;}
  bool wasHomeGesture()const{return home;}
  void update(bool v){record("input.pump %d",v);}
  struct Labels {const char *btn1,*btn2,*btn3,*btn4;};
  Labels mapLabels(const char*a,const char*b,const char*c,const char*d)const{return {a,b,c,d};}
};
struct FilePathResult{std::string path;};
struct ActivityResult {bool isCancelled=false;std::variant<std::monostate,FilePathResult> data;};
struct Activity {
  GfxRenderer& renderer;MappedInputManager& mappedInput;std::string name;
  bool finished=false;
  Activity(const char*n,GfxRenderer&r,MappedInputManager&i):renderer(r),mappedInput(i),name(n){}
  virtual ~Activity()=default;
  virtual void onEnter(){record("enter %s",name.c_str());}
  virtual void onExit(){record("exit %s",name.c_str());}
  struct RenderLock{explicit RenderLock(Activity&){} };
  virtual void render(RenderLock&&){}
  virtual void loop(){}
  virtual bool preventAutoSleep(){return false;}
  virtual bool skipLoopDelay(){return false;}
  void finish(){finished=true;record("finish");}
  void onGoHome(){record("home");}
  void requestUpdate(bool immediate=false){record("render.request %d",immediate);}
  void requestUpdateAndWait(){record("render.wait");}
  template<class T,class F> void startActivityForResult(std::unique_ptr<T> child,F){record("push %s",child->name.c_str());}
};
struct WifiSelectionActivity:Activity {WifiSelectionActivity(GfxRenderer&r,MappedInputManager&i):Activity("WifiSelection",r,i){}};
struct FileBrowserActivity:Activity {enum class Mode{PickFirmware};FileBrowserActivity(GfxRenderer&r,MappedInputManager&i,const char*,Mode):Activity("FileBrowser",r,i){}};
struct ConfirmationActivity:Activity {ConfirmationActivity(GfxRenderer&r,MappedInputManager&i,const std::string&h,const std::string&b):Activity("Confirmation",r,i){record("confirm %s | %s",h.c_str(),b.c_str());}};
namespace freeink::ui {
struct ListNav{int selected=0;void reset(){selected=0;}};
struct ListItem{const char*label=nullptr,*subtitle=nullptr,*value=nullptr;int actionValue=0;};
}
namespace fui=freeink::ui;
struct UiScreen{};
struct UiListActivity:Activity {
  fui::ListNav nav;
  struct App{void clearTapFlash(){record("tap.clear");}}app;
  using Activity::Activity;
  virtual int listCount()const{return 0;}
  virtual void buildScreen(UiScreen&){}
  virtual void activateIndex(int){}
  virtual fui::ListNav& activeNav(){return nav;}
  virtual void onBackButton(){}
  virtual bool handleCustomInput(){return false;}
  void closeRouting(){record("routing.close");}
  void renderUi(){record("paint.list");}
};
struct OptionPopup {
  bool active=false;int selected=0;std::function<void(int)> callback;
  void show(const char*title,const char*const*options,int count,int current,std::function<void(int)> cb){active=true;selected=current;callback=cb;record("popup %s %d %d %s %s",title,count,current,options[0],options[1]);}
  void showMessage(const char*t,const char*m,const char*const*o,int n,int s,std::function<void(int)>cb){show(t,o,n,s,cb);record("popup.message %s",m);}
  bool isActive()const{return active;}
  bool processRender(GfxRenderer&r,MappedInputManager&){if(!active)return false;record("paint.popup %d",selected);r.displayBuffer();return true;}
  bool handleInput(MappedInputManager&i,std::function<void()> repaint){
    if(!active)return false;
    if(i.wasPressed(MappedInputManager::Button::NavPrevious)||i.wasPressed(MappedInputManager::Button::NavNext)){selected=1-selected;repaint();}
    else if(i.wasReleased(MappedInputManager::Button::Confirm)){active=false;callback(selected);repaint();}
    else if(i.wasReleased(MappedInputManager::Button::Back)){active=false;repaint();}
    return true;
  }
};
static struct Config {
  bool fileOpen=true,partition=true,rootOpen=true,rootDir=true,newer=true,ensure=true,fontValid=true,crcOpen=true;
  int validate=0,flash=0,otaCheck=0,otaInstall=0,http=0,deleteResult=0;
  unsigned size=200,partitionSize=1000,freeHeap=200000,maxHeap=100000,crc=42;
}cfg;
struct WiFiLeaf {
  int modeValue=0;
  void mode(int m){modeValue=m;record("wifi.mode %d",m);}
  int getMode(){return modeValue;}
  void disconnect(bool erase){record("wifi.disconnect %d",erase);}
}WiFi;
constexpr int WIFI_STA=1,WIFI_MODE_NULL=0;
static void delay(int ms){record("delay %d",ms);}
static void silentRestart(){record("restart.silent");}
struct {unsigned getFreeHeap(){return cfg.freeHeap;}unsigned getMaxAllocHeap(){return cfg.maxHeap;}void restart(){record("restart.normal");}}ESP;
struct OtaUpdater {
 enum Result{OK,NO_UPDATE,FAIL,WRONG_DEVICE_ERROR};
 unsigned processed=0,total=100;
 int checkForUpdate(){record("ota.check");return cfg.otaCheck;}
 bool isUpdateNewer(){record("ota.newer");return cfg.newer;}
 const char*getLatestVersion(){return "new";}
 unsigned getProcessedSize(){return processed;}
 unsigned getTotalSize(){return total;}
 int installUpdate(void(*cb)(void*),void*ctx){record("ota.install");for(unsigned p:{0,1,2,99,100}){processed=p;record("ota.progress %u",p);cb(ctx);}return cfg.otaInstall;}
};
using String=std::string;
struct Entry{const char*name;bool dir;bool removeOk;};
static std::vector<Entry> entries;
struct HalFile {
 bool valid=false,root=false;int index=-1,next=0;
 explicit operator bool()const{return valid;}
 bool isDirectory()const{return root?cfg.rootDir:entries[index].dir;}
 size_t fileSize(){record("file.size %u",cfg.size);return cfg.size;}
 void close(){if(valid)record("file.close %s",root?"root":index<0?"firmware":entries[index].name);valid=false;}
 HalFile openNextFile(){if(next>=int(entries.size()))return {};auto n=next++;record("file.next %s",entries[n].name);return {true,false,n};}
 void getName(char*p,size_t cap){snprintf(p,cap,"%s",entries[index].name);record("file.name %s",p);}
};
struct {
 bool openFileForRead(const char*tag,const char*path,HalFile&f){record("file.open %s %s",tag,path);f.valid=cfg.fileOpen;return f.valid;}
 HalFile open(const char*path){record("directory.open %s",path);return {cfg.rootOpen,true};}
 bool removeDir(const char*path){record("directory.remove %s",path);for(auto&e:entries)if(std::string(path)=="/.crosspoint/"+std::string(e.name))return e.removeOk;return false;}
}Storage;
static bool isBookCacheDirectoryName(const char*s){return strncmp(s,"epub_",5)==0||strncmp(s,"xtc_",4)==0||strncmp(s,"txt_",4)==0;}
struct esp_partition_t {size_t size;};
static const esp_partition_t*esp_ota_get_next_update_partition(void*){static esp_partition_t p;record("partition.next");p.size=cfg.partitionSize;return cfg.partition?&p:nullptr;}
namespace firmware_flash {
 enum class Result{OK,TOO_LARGE,TOO_SMALL,BAD_CHIP,WRONG_BOARD,BAD_IMAGE,WRITE};
 const char*resultName(Result r){static const char*names[]={"OK","TOO_LARGE","TOO_SMALL","BAD_CHIP","WRONG_BOARD","BAD_IMAGE","WRITE"};return names[int(r)];}
 Result validateImageFile(const char*path,size_t limit){record("firmware.validate %s %zu",path,limit);return Result(cfg.validate);}
 Result flashFromSdPath(const char*path,void(*cb)(size_t,size_t,void*),void*ctx){record("firmware.flash %s revalidate",path);for(unsigned n:{0,1,2,99,100}){record("firmware.progress %u 100",n);cb(n,100,ctx);}return Result(cfg.flash);}
}
struct FontInstaller {
 enum class Error{OK,FAIL};
 FontInstaller(int){}
 bool ensureFamilyDir(const char*n){record("font.ensure %s",n);return cfg.ensure;}
 Error deleteFamily(const char*n){record("font.delete %s",n);return Error(cfg.deleteResult);}
 void refreshRegistry(){record("font.registry");}
 bool validateCpfontFile(const char*p){record("font.validate %s",p);return cfg.fontValid;}
 static void buildFontPath(const char*f,const char*n,char*out,size_t size){snprintf(out,size,"/fonts/%s/%s",f,n);}
};
struct {int registry(){return 0;}}sdFontSystem;
struct HttpDownloader {
 enum{OK,ABORTED,FAIL};static constexpr unsigned MIN_TLS_FREE_HEAP=40000,MIN_TLS_MAX_ALLOC=20000;
 static int downloadToFile(const std::string&url,const char*path,std::function<void(size_t,size_t)>cb,bool*cancel,const char*,const char*,std::vector<int>,bool downgrade){record("http.download %s %s downgrade=%d",url.c_str(),path,downgrade);for(unsigned n:{0,50,100}){cb(n,100);if(*cancel){record("http.aborted");return ABORTED;}}return cfg.http;}
};
struct UITheme {
 struct Metrics {int topPadding=12,headerHeight=36,contentSidePadding=16,verticalSpacing=8,progressBarHeight=24;}metrics;
 static UITheme&getInstance(){static UITheme t;return t;}
 const Metrics&getMetrics(){return metrics;}
 enum class TextVerticalAlignment{TOP};
 static void drawCenteredWrappedText(GfxRenderer&,Rect r,int,const char*t,int lines,bool,EpdFontFamily::Style,TextVerticalAlignment){record("paint.wrap %d %d %d %d %d %s",r.x,r.y,r.w,r.h,lines,t);}
 void drawHeader(GfxRenderer&,Rect r,const char*t,const char*s=nullptr){record("paint.header %d %d %d %d %s | %s",r.x,r.y,r.w,r.h,t,s?s:"");}
 void drawProgressBar(const GfxRenderer&,Rect,size_t,size_t);
 void drawButtonHints(GfxRenderer&,const char*a,const char*b,const char*c,const char*d){record("paint.hints %s | %s | %s | %s",a,b,c,d);}
};
#define GUI UITheme::getInstance()
// LABELS
// ORIGINAL
// STUBS
// CASES
