from pathlib import Path
import re
import prepare, prepare_wifi_extra
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
METHODS=['CallContext::inputCurrent','onEnter','onExit','run','service','publish','keyboard','appendHiddenNetworkEntry','rebuildNetworkStatuses','getSignalStrengthIndicator','onComplete','loop']
def source(core=False):
 s=prepare_wifi_extra.source()
 # Exercise keyboard entry through network selection in both implementations.
 s=s.replace('if(hidden) a.promptHiddenSsid();else {a.selectedSSID="ordinary";a.promptPasswordEntry();}', 'if(hidden) a.networks[0].isHiddenPlaceholder=true; a.selectNetwork(0);')
 if core:
  main=(HERE/'effects.cpp').read_text()
  start=main.index('  for (bool automatic : {false, true}) {',main.index('int main()'))
  end=main.index('  for (bool connected : {false, true})',start)
  s=s[:s.index('int main()')]+ 'int main(){\n'+main[start:end].replace('a.startWifiScan(automatic);','a.allowAutoConnect=automatic; if(automatic) WIFI_STORE.credentials={{"missing","secret"}}; a.onEnter();')+'\n}\n'
 s=s.replace('if(phase==2) a.onRowEvent(event,&a);else a.onPromptEvent(event,&a);','if(phase==2) a.onRowEvent(event,&a);else a.onPromptEvent(event,&a); haptic_feedback::touchAction(longPress); a.requestUpdate();')
 s=s.replace('int main() {','namespace haptic_feedback {void touchAction(bool longPress){record("haptic:"+std::to_string(longPress));}}\nint main() {')
 return s

def candidate(core=False):
 s=source(core).replace(' haptic_feedback::touchAction(longPress); a.requestUpdate();','').replace('struct MappedInputManager {','struct MappedInputManager {\n uint32_t getHeldTime()const{return 0;}')
 a=s.index('struct WifiSelectionActivity : Activity {');b=s.index('\n};',a)+3
 cls='''struct WifiSelectionActivity:Activity {
 XpUiWifiState model{};XpUiWifiHits hits{};
 bool allowAutoConnect=true,routingReady=false,testRejected=false;
 bool rustSceneRejected()const{return testRejected;}
 std::vector<WifiNetworkInfo> networks;
 std::vector<std::string> networkStatuses,autoAttemptedSsids;
 std::vector<fui::ListItem> networkRowItems;
 std::string selectedSSID,connectedIP,connectionError,enteredPassword,cachedMacAddress;
 struct PhaseProxy {uint8_t& value;explicit operator int()const{return value;}operator WifiSelectionState()const{return WifiSelectionState(value);}PhaseProxy& operator=(WifiSelectionState v){value=uint8_t(v);return *this;}} state{model.phase};
 int32_t& selectedNetworkIndex=model.list.selected;
 uint16_t& realNetworkCount=model.real_count;
 uint8_t& selectedRequiresPassword=model.requires_password;uint8_t& usedSavedPassword=model.used_saved;
 uint8_t& autoConnecting=model.automatic;uint8_t& manualNetworkListRequested=model.manual_list;
 uint8_t& savePromptSelection=model.save_selection;uint8_t& forgetPromptSelection=model.forget_selection;
 uint32_t& connectionStartTime=model.connection_at;
 struct ModelLock{explicit ModelLock(WifiSelectionActivity&) {}};
 struct ResourceLock{explicit ResourceLock(WifiSelectionActivity&) {}};
 // CALL_CONTEXT
 WifiSelectionActivity(){model.flash_index=-1;model.phase=XP_UI_WIFI_PHASE_SCANNING;xp_ui_settings_list_init(&model.list,0);}
 WifiSelectionActivity(GfxRenderer&,MappedInputManager&):WifiSelectionActivity(){}
 void onEnter();void onExit();void productionLoop();void loop(){model.count=networks.size();productionLoop();}
 void run(XpUiWifiEvent,uint32_t=0,const XpUiInput*=nullptr);
 static uint32_t service(void*,uint32_t,uint32_t);static void publish(void*,XpUiWifiState*);
 void retireRows(){}
 void keyboard(bool);void appendHiddenNetworkEntry();void rebuildNetworkStatuses();
 static std::string getSignalStrengthIndicator(int32_t);void onComplete(bool);
 void processWifiScanResults(){run(XP_UI_WIFI_EVENT_TICK);}
 void selectNetwork(int i){model.count=networks.size();model.phase=XP_UI_WIFI_PHASE_LIST;model.list.selected=i;XpUiInput input{};input.released=XP_UI_CONFIRM;run(XP_UI_WIFI_EVENT_TICK,0,&input);}
 void attemptConnection(){model.phase=XP_UI_WIFI_PHASE_PASSWORD;run(XP_UI_WIFI_EVENT_TICK);}
 void checkConnectionStatus(){run(XP_UI_WIFI_EVENT_TICK);}
 static void onRowEvent(const fui::ActionEvent& event,void* ctx){auto& a=*static_cast<WifiSelectionActivity*>(ctx);a.model.count=a.networks.size();XpUiInput i{};i.action=1;i.value=event.value;i.touch_flags=XP_UI_TOUCH_RELEASED;a.run(XP_UI_WIFI_EVENT_TICK,event.longPress|2,&i);}
 static void onPromptEvent(const fui::ActionEvent& event,void* ctx){auto& a=*static_cast<WifiSelectionActivity*>(ctx);XpUiInput i{};i.action=3;i.value=event.value;i.touch_flags=XP_UI_TOUCH_RELEASED;a.run(XP_UI_WIFI_EVENT_TICK,event.longPress|2,&i);}
};'''.replace('XpUiInput*=','XpUiInput* =')
 header=(ROOT/'src/activities/network/WifiSelectionActivity.h').read_text()
 context=header[header.index('  struct CallContext {'):header.index('  void run(',header.index('  struct CallContext {'))]
 cls=cls.replace('// CALL_CONTEXT',context)
 s=s[:a]+cls+s[b:]
 names=prepare.METHODS['WifiSelectionActivity']+['onEnter','onExit','onRowEvent','onScanEvent','onPromptEvent','promptPasswordEntry','promptHiddenSsid','rebuildNetworkRowItems','getSignalStrengthIndicator']
 for name in names:s=s.replace(prepare.extract(s,'WifiSelectionActivity',name),'')
 code=(ROOT/'src/activities/network/WifiSelectionActivity.cpp').read_text()
 methods='\n'.join(prepare.extract(code,'WifiSelectionActivity',m) for m in METHODS).replace('void WifiSelectionActivity::loop()','void WifiSelectionActivity::productionLoop()')
 s=s.replace('namespace haptic_feedback {void touchAction(bool longPress){record("haptic:"+std::to_string(longPress));}}\n','')
 s=s.replace('static void start(','namespace haptic_feedback {void touchAction(bool longPress){record("haptic:"+std::to_string(longPress));}}\n'+methods+'\nstatic void start(')
 s=s.replace('static void start(','''namespace fui {struct InputSnapshot{int16_t touchX=0,touchY=0;bool touchPressed=false,touchReleased=false,touchHeld=false,longPress=false;};}
static fui::InputSnapshot touchSnapshotFrom(MappedInputManager&,bool){return {};}
static void start(''',1)
 # Snapshot helper declaration must precede the extracted production loop.
 helper=s[s.index('namespace fui {struct InputSnapshot'):s.index('static void start(',s.index('namespace fui {struct InputSnapshot'))]
 s=s.replace(helper,'').replace(methods,helper+methods)
 return '#include <optional>\n#include "xp_ui_network.h"\n'+s
