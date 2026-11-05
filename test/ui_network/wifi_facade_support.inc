struct MappedInputManager {
 bool touch=false;
 enum class Button {Back,Confirm,NavNext,NavPrevious,Left,Right,Up,Down};
 enum class SwipeDir {None,Up,Down,Left,Right};
 uint32_t pressed=0,released=0,held=0,heldTime=0;
 fui::InputSnapshot snapshot{};SwipeDir swipe=SwipeDir::None;
 bool wasPressed(Button b)const{return pressed&(1u<<static_cast<unsigned>(b));}
 bool wasReleased(Button b)const{return released&(1u<<static_cast<unsigned>(b));}
 bool isPressed(Button b)const{return held&(1u<<static_cast<unsigned>(b));}
 uint32_t getHeldTime()const{return heldTime;}
 SwipeDir wasSwipe()const{return swipe;}
 bool hasTouch()const{return touch;}
 struct Labels{const char* btn1;const char* btn2;const char* btn3;const char* btn4;};
 Labels mapLabels(const char*a,const char*b,const char*c,const char*d){return{a,b,c,d};}
};
fui::InputSnapshot touchSnapshotFrom(MappedInputManager& input,bool){return input.snapshot;}
uint32_t nowMs=0;uint32_t fixtureMillis(){return nowMs;}
struct RenderLock {};
struct WifiCredential {std::string ssid,password;};
struct StaticSemaphore_t {std::mutex mutex;};
using SemaphoreHandle_t=StaticSemaphore_t*;
constexpr int portMAX_DELAY=0;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* s){return s;}
void xSemaphoreTake(SemaphoreHandle_t s,int){assert(allocations::criticalDepth==0);s->mutex.lock();}
void xSemaphoreGive(SemaphoreHandle_t s){s->mutex.unlock();}
#define LOG_ERR(...) ((void)0)
size_t strlcpy(char* dst,const char* src,size_t n){const size_t len=strlen(src);if(n){memcpy(dst,src,std::min(len,n-1));dst[std::min(len,n-1)]=0;}return len;}
int failAllocation=-1;
template<class T>auto fixtureAllocate(size_t n){if(failAllocation==0)return std::unique_ptr<T>{};if(failAllocation>0)--failAllocation;return std::unique_ptr<T>(new(std::nothrow)std::remove_extent_t<T>[n]{});}
struct FontSpec{int smallFontId=UI_10_FONT_ID,bodyFontId=UI_12_FONT_ID;};
FontSpec uiScaleSpec(){return{};}
fui::GfxRendererTarget makeUiTarget(const GfxRenderer& renderer){auto t=fui::GfxRendererTarget(renderer,false);t.setFont(0,UI_10_FONT_ID);t.setFont(1,UI_12_FONT_ID);return t;}
std::function<void()> paintHook,displayHook;
void testDisplayCallback(){assert(allocations::criticalDepth==0);if(displayHook)displayHook();}
struct FakeBase {
 GfxRenderer& renderer;MappedInputManager& mappedInput;
 uint16_t lastCommandCount=0;std::mutex modelMutex;unsigned requested=0,completed=0;void requestUpdate(){++requested;}
 FakeBase(const char*,GfxRenderer&r,MappedInputManager&i):renderer(r),mappedInput(i){}
 struct ModelLock{std::unique_lock<std::mutex> lock;explicit ModelLock(FakeBase& base):lock(base.modelMutex) {++allocations::criticalDepth;}~ModelLock(){--allocations::criticalDepth;}};
 virtual ~FakeBase()=default;
 virtual void onEnter(){};virtual void onExit(){};virtual void loop(){};virtual void render(RenderLock&&){};virtual bool preventAutoSleep(){return false;}
 virtual bool buildRustScene(XpUiScene&){return false;}
 virtual XpUiPaint rustResources(){return{};}
 virtual void rustPublished(){};virtual void rustSceneFailed(){};
 struct Publication{XpUiScene scene{};bool ready=false;};
 bool sceneFailed=false,suppressContact=false,lastContact=false;uint16_t suppressButtons=0,lastHeld=0;
 virtual Publication* rustPublication(){return nullptr;}
 virtual bool rustSceneCurrent()const{return true;}
 virtual void cancelRejectedInput(const XpUiInput&){}
 XpUiInput sampleRustInput()const{XpUiInput i{};i.pressed=mappedInput.pressed;i.released=mappedInput.released;i.held=mappedInput.held;
 i.touch_flags=(mappedInput.snapshot.touchPressed?XP_UI_TOUCH_PRESSED:0)|(mappedInput.snapshot.touchReleased?XP_UI_TOUCH_RELEASED:0)|(mappedInput.snapshot.touchHeld?XP_UI_TOUCH_HELD:0);return i;}
 bool rustSceneRejected()const;void rejectRustScene();bool consumeRejectedInput();void publishRustScene(const XpUiScene&);
 bool paintRustScene(const XpUiScene& scene){lastCommandCount=scene.command_count;if(paintHook)paintHook();auto paint=rustResources();bool okay=renderer.drawUiScene(scene,&paint);if(okay)publishRustScene(scene);else rejectRustScene();return okay;}

};
struct {
 template<class...Args>void drawHeader(Args&&...){}
 template<class...Args>void drawSubHeader(Args&&...){}
 template<class...Args>void drawButtonHints(Args&&...){}
 template<class...Args>void drawProgressBar(Args&&...){}
 template<class...Args>void drawHelpText(Args&&...){}
}GUI;
struct QrUtils{template<class...Args>static void drawQrCode(Args&&...){std::abort();}};
// ADAPTER_METHODS
// CANDIDATE_HEADER
// SHARED_BINDING
// CANDIDATE_METHODS

uint32_t Candidate::service(void* ctx,uint32_t op,uint32_t value){
 assert(allocations::criticalDepth==0);auto& self=static_cast<CallContext*>(ctx)->activity;
 switch(op){
 case XP_UI_WIFI_CALL_NOW:return nowMs;
 case XP_UI_WIFI_CALL_PAINT:self.requestUpdate();break;
 case XP_UI_WIFI_CALL_ROW_FLAGS:{ResourceLock lock(self);return value<self.networks.size()&&self.networks[value].hasSavedPassword?1:0;}
 case XP_UI_WIFI_CALL_COMPLETE:++self.completed;break;
 default:fprintf(stderr,"Unexpected WiFi mock service %u value %u phase %u\n",op,value,self.model.phase);assert(false);
 }return 0;
}

void checkWifiFailureAndPanel(GfxRenderer& renderer){
 safe={3,5,renderer.getScreenWidth()-8,renderer.getScreenHeight()-45};
 MappedInputManager input;Candidate candidate(renderer,input);
 candidate.model.phase=XP_UI_WIFI_PHASE_SAVE;candidate.model.count=17;candidate.model.flash_index=-1;
 xp_ui_settings_list_init(&candidate.model.list,0);candidate.selectedSSID="network";
 candidate.networks.resize(17);candidate.networkStatuses.resize(17);
 for(auto& n:candidate.networks)n.ssid="network";
 candidate.render(RenderLock{});assert(!candidate.rustSceneRejected()&&candidate.commandCapacity==13);
 auto* old=candidate.commands.get();const auto oldCapacity=candidate.commandCapacity;
 candidate.model.phase=XP_UI_WIFI_PHASE_LIST;
 failAllocation=0;candidate.render(RenderLock{});assert(candidate.rustSceneRejected()&&candidate.commands.get()==old&&candidate.commandCapacity==oldCapacity);
 failAllocation=1;candidate.render(RenderLock{});assert(candidate.rustSceneRejected()&&candidate.commands.get()==old&&candidate.visibleRows);
 input.pressed=input.held=XP_UI_CONFIRM|XP_UI_NAV_NEXT;input.snapshot.touchPressed=input.snapshot.touchHeld=true;
 candidate.loop();assert(candidate.model.list.selected==0&&candidate.rustSceneRejected());
 input.pressed=XP_UI_BACK;candidate.loop();assert(candidate.completed==1);
 failAllocation=-1;candidate.render(RenderLock{});assert(!candidate.rustSceneRejected()&&candidate.commands.get()!=old);
 input.pressed=input.held=0;input.released=XP_UI_CONFIRM|XP_UI_NAV_NEXT;input.snapshot.touchPressed=input.snapshot.touchHeld=false;input.snapshot.touchReleased=true;
 candidate.loop();assert(candidate.model.list.selected==0&&candidate.completed==1);
 input.released=0;input.snapshot={};
 displayHook=[&]{
   assert(candidate.resourceMutex->mutex.try_lock());candidate.resourceMutex->mutex.unlock();
   auto pending=std::async(std::launch::async,[&]{input.pressed=input.held=XP_UI_NAV_NEXT;candidate.loop();input.pressed=input.held=0;input.released=XP_UI_NAV_NEXT;candidate.loop();});
   assert(pending.wait_for(std::chrono::seconds(1))==std::future_status::ready);pending.get();
   assert(candidate.model.list.selected==1);
 };
 candidate.render(RenderLock{});displayHook={};
 input.released=0;input.held=XP_UI_NAV_NEXT;input.heldTime=501;nowMs=501;candidate.loop();assert(candidate.model.list.selected==2);
 nowMs=1001;candidate.loop();assert(candidate.model.list.selected==2);
 nowMs=1002;candidate.loop();assert(candidate.model.list.selected==3);
 input.held=0;input.released=XP_UI_NAV_NEXT;candidate.loop();assert(candidate.model.list.last_repeat==0);
 puts("WiFi arena growth failure recovers; panel-busy press/release and repeat boundaries pass");
}
