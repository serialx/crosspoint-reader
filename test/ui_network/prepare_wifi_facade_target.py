from pathlib import Path
import re,sys
import prepare
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
legacy = len(sys.argv)>2 and sys.argv[2]=='legacy'
s=(HERE/'WifiTarget.cpp').read_text().replace('for (int inset : {0, 7})','for (int inset : {0})')
# The original body uses the SDK; the candidate below uses the actual facade methods.
a=s.index('                Owner owner');b=s.index('                for (auto b : display.pixels)',a)
s=s[:a]+'''                const auto expectedAllocations=allocations::events;const auto expectedCount=allocations::count;const auto expectedPeak=allocations::peak;
                metrics.listRowHeight=theme.listMinRowHeight;metrics.listRowGap=theme.listRowGap;metrics.listInset=theme.listInset;
                metrics.listSidePadding=theme.listSidePadding;metrics.listScrollWidth=theme.listScrollWidth;metrics.listRowRadius=theme.listRowRadius;
                metrics.listSelectionStyle=style;metrics.listScrollSide=theme.listScrollSide;metrics.listTitleBold=theme.bodyText.bold;
                MappedInputManager input{touch};Candidate candidate(renderer,input);
                candidate.model.phase=phase;candidate.model.count=count;candidate.model.flash_index=-1;
                candidate.model.list=initialView;
                candidate.model.save_selection=candidate.model.forget_selection=selection;
                if(heldTouch)candidate.model.list.active=0;
                candidate.selectedSSID=original.selectedSSID;
                candidate.networks.resize(count);candidate.networkStatuses.resize(count);
                for(int i=0;i<count;++i){candidate.networks[i].ssid=labels[i];candidate.networkStatuses[i]=original.networkRowItems[i].value;}
                if(orientation==0&&!touch&&phase==2&&count==17&&selection==1&&length==33&&style==0){
                 failAllocation=0;candidate.render(RenderLock{});assert(candidate.rustSceneRejected()&&!candidate.commands&&!candidate.visibleRows);
                 failAllocation=1;candidate.render(RenderLock{});assert(candidate.rustSceneRejected()&&!candidate.commands&&candidate.visibleRows);
                 failAllocation=-1;
                }
                candidate.render(RenderLock{});
                maxBudget=std::max<unsigned>(maxBudget,candidate.commandCapacity);maxCommands=std::max<unsigned>(maxCommands,candidate.lastCommandCount);
                candidate.model.list=initialView;
                for(int n=0;n<frameOrdinal;++n)candidate.render(RenderLock{});
                allocations::begin();candidate.render(RenderLock{});allocations::enabled=false;
                if(allocations::live!=0||(phase!=2&&(allocations::peak!=expectedPeak||allocations::count!=expectedCount))){fprintf(stderr,"Allocation mismatch o=%d touch=%d phase=%d style=%d count=%d sel=%d len=%d live=%zu peak=%zu/%zu count=%zu/%zu\\n",orientation,touch,phase,style,count,selection,length,allocations::live,allocations::peak,expectedPeak,allocations::count,expectedCount);for(size_t i=0;i<allocations::count;++i)fprintf(stderr," %lld",(long long)allocations::events[i]);fprintf(stderr,"\\n");return 1;}
                if(phase!=2)for(size_t i=0;i<expectedCount;++i)assert(allocations::events[i]==expectedAllocations[i]);
                assert(allocations::peak<=expectedPeak+2048);
                assert(!candidate.rustSceneRejected());
                if(orientation==0&&!touch&&phase==2&&count==17&&selection==1&&length==33&&style==0&&!wide){
                  auto* borrowed=candidate.visibleRows.get();const auto capacity=candidate.rowCapacity;allocations::watchedPointer=borrowed;allocations::watchedFrees=0;
                  paintHook=[&]{
                    std::thread inputThread([&]{
                      {Candidate::ResourceLock resources(candidate);candidate.networks.clear();candidate.networkStatuses.clear();}
                      Candidate::CallContext context{candidate,{},{}};auto next=candidate.model;next.phase=XP_UI_WIFI_PHASE_CONNECTING;next.generation++;
                      Candidate::publish(&context,&next);
                      assert(candidate.visibleRows.get()==borrowed&&candidate.rowCapacity==capacity&&candidate.retireRequested&&allocations::watchedFrees==0);
                    });inputThread.join();
                  };
                  candidate.render(RenderLock{});paintHook={};
                  assert(!candidate.visibleRows&&candidate.rowCapacity==0&&!candidate.paintingRows);
                  candidate.retireRows();assert(!candidate.visibleRows&&allocations::watchedFrees==1);allocations::watchedPointer=nullptr;
                }
                if(display.pixels!=expected){fprintf(stderr,"Facade mismatch o=%d inset=%d touch=%d phase=%d style=%d count=%d selection=%d length=%d\\n",orientation,inset,touch,phase,style,count,selection,length);return 1;}
''' +s[b:]
s=s.replace('#include <optional>','#include <optional>\n#include <memory>\n#include <cstring>\n#include <type_traits>\n#include <mutex>\n#include <thread>\n#include <functional>\n#include <future>\n#include <chrono>')
s=s.replace('int popupFrameThickness = 2, popupCornerRadius = 12;', '''int popupFrameThickness = 2, popupCornerRadius = 12;
 int listRowHeight=0,listRowGap=3,listInset=5,listSidePadding=12,listScrollWidth=4,listRowRadius=12,listSelectionStyle=0,listScrollSide=0,listTitleBold=0,contentSidePadding=16;''')
s=s.replace('struct UITheme {','''struct UITheme {
 enum class TextVerticalAlignment{TOP,CENTER,BOTTOM};
 template<class...Args>static void drawCenteredText(Args&&...){}
 template<class...Args>static void drawCenteredWrappedText(Args&&...){std::abort();}
''')
support=(HERE/'wifi_facade_support.inc').read_text()
adapter=(ROOT/'src/activities/RustActivityAdapter.cpp').read_text()
support=support.replace('// ADAPTER_METHODS','\n'.join(prepare.extract(adapter,'RustActivityAdapter',m)for m in ['rejectRustScene','rustSceneRejected','consumeRejectedInput','publishRustScene']).replace('RustActivityAdapter','FakeBase'))
header=re.sub(r'^#(?:include|pragma)[^\n]*\n','',(ROOT/'src/activities/network/WifiSelectionActivity.h').read_text(),flags=re.M)
header=header.replace('WifiSelectionActivity','Candidate').replace('class Candidate final : public RustActivityAdapter','struct Candidate final : public FakeBase')
production=(ROOT/'src/activities/network/WifiSelectionActivity.cpp').read_text()
methods='\n'.join(prepare.extract(production,'WifiSelectionActivity',m)for m in ['WifiSelectionActivity','text','resources','buildRustScene','render','rustResources','rustPublished','rustSceneFailed','retireRows','publish','run','loop']).replace('WifiSelectionActivity','Candidate').replace('RustActivityAdapter(', 'FakeBase(').replace('makeUniqueNoThrow<','fixtureAllocate<').replace('millis()','fixtureMillis()')
methods+='\nvoid Candidate::onEnter(){} void Candidate::onExit(){} \n'
nh=re.sub(r'^#(?:include|pragma)[^\n]*\n','',(ROOT/'src/activities/network/NetworkUiBinding.h').read_text(),flags=re.M)
nb=re.sub(r'^#include[^\n]*\n','',(ROOT/'src/activities/network/NetworkUiBinding.cpp').read_text(),flags=re.M)
support=support.replace('// CANDIDATE_HEADER',header).replace('// SHARED_BINDING',nh+'\n'+nb).replace('// CANDIDATE_METHODS',methods)
s=s.replace('#define tr(x) #x','#define tr(x) fixtureTranslation(#x)\nconst char* fixtureTranslation(const char* name){return strcmp(name,"STR_NETWORKS_FOUND")==0?"%zu networks":name;}')
s=s.replace('int main() {',support+'\nint main() {')
frozen=(HERE/'wifi_render_original.inc').read_text()
s=s.replace('// ORIGINAL_BODY','\n'.join(prepare.extract(frozen,'WifiSelectionActivity',name)for name in ['buildListScreen','buildPromptDialog']))
s=s.replace('  unsigned cases =', '  checkWifiFailureAndPanel(renderer);\n  unsigned cases =')
if legacy:
 s=s.replace('for (int phase : {2, 7, 9})','for (int phase : {0, 1, 5, 6, 8})').replace('for (int count : {0, 1, 17, 60})','for (int count : {0})').replace('for (int style = 0; style < 4; ++style)','for (int style = 0; style < 1; ++style)')
 s=s.replace('  std::string selectedSSID;', '  std::string selectedSSID,connectedIP="192.168.1.123",connectionError="STR_ERROR_NETWORK_NOT_FOUND"; bool autoConnecting=false;')
 s=s.replace('    bool hasTouch() { return touch; }','    bool hasTouch() { return touch; }\n    MappedInputManager::Labels mapLabels(const char*a,const char*b,const char*c,const char*d)const{return{a,b,c,d};}')
 # The native original body declarations precede the candidate support.
 s=s.replace('MappedInputManager::Labels mapLabels','struct Labels{const char* btn1;const char* btn2;const char* btn3;const char* btn4;};Labels mapLabels')
 s=s.replace('  void buildListScreen(UiScreen&);','  void renderConnecting(const Rect*,const ThemeMetrics*)const;void renderConnected(const Rect*,const ThemeMetrics*)const;void renderConnectionFailed(const Rect*,const ThemeMetrics*)const;\n  void buildListScreen(UiScreen&);')
 s=s.replace('template<class...Args>static void drawCenteredText(Args&&...){}','static void drawCenteredText(const GfxRenderer&,Rect,int,int,const char*,bool=true,EpdFontFamily::Style=EpdFontFamily::REGULAR);')
 s=s.replace('template<class...Args>static void drawCenteredWrappedText(Args&&...){std::abort();}','static void drawCenteredWrappedText(const GfxRenderer&,Rect,int,const char*,int,bool=true,EpdFontFamily::Style=EpdFontFamily::REGULAR,TextVerticalAlignment=TextVerticalAlignment::CENTER);')
 s=s.replace('int main() {',(HERE/'wifi_theme_original.inc').read_text()+'\n'+'\n'.join(prepare.extract(frozen,'WifiSelectionActivity',name)for name in ['renderConnecting','renderConnected','renderConnectionFailed'])+'\nint main() {')
 s=s.replace('original.selectedSSID = std::string(length, \'w\');','original.selectedSSID = std::string(length, \'w\');original.autoConnecting=wide;')
 s=s.replace('original.buildListScreen(screen);','if(phase==6)original.renderConnected(&safe,&metrics);else if(phase==8)original.renderConnectionFailed(&safe,&metrics);else original.renderConnecting(&safe,&metrics);')
 s=s.replace('candidate.selectedSSID=original.selectedSSID;','candidate.selectedSSID=original.selectedSSID;candidate.connectedIP=original.connectedIP;candidate.connectionError=original.connectionError;candidate.model.automatic=wide;')
Path(sys.argv[1]).write_text(s)
