"""Add original Wi-Fi entry/keyboard/touch/resource paths to transport leaves."""
from pathlib import Path
import re
import prepare
from test_oracle import frozen
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
def source():
 s=frozen()
 s=s[:s.index('int main() {')]+(HERE/'wifi_extra_main.inc').read_text()
 s=s.replace('struct WifiResult {','struct KeyboardResult {std::string text;};\nstruct WifiResult {')
 s=s.replace('std::variant<WifiResult, NetworkModeResult>', 'std::variant<WifiResult, NetworkModeResult, KeyboardResult>')
 s=s.replace('    bool invalidated()', '    template<class F> void on(int,F,void*) {}\n    bool invalidated()')
 s=s.replace('struct WifiSelectionActivity : Activity {','''namespace fui {
struct ActionEvent {int16_t value=0;bool longPress=false;};
struct ListItem {const char* label=nullptr;const char* value=nullptr;int16_t actionValue=0;};
}
struct UiScreen {};
constexpr int ACTION_ROW=1,ACTION_SCAN=2,ACTION_PROMPT=3;
enum class InputType {Text};
struct KeyboardEntryActivity {
 KeyboardEntryActivity(GfxRenderer&,MappedInputManager&,const char* label,const char*,int limit,InputType) {
  record(std::string("keyboard:")+label+":"+std::to_string(limit));
 }
};
struct WifiSelectionActivity : Activity {''')
 s=s.replace('std::vector<int> networkRowItems;', 'std::vector<fui::ListItem> networkRowItems;\n  bool allowAutoConnect=true;\n  std::string cachedMacAddress;')
 for name in ['rebuildNetworkRowItems','promptPasswordEntry','promptHiddenSsid']:
  a=s.index('  void '+name+'() {');b=s.index('{',a);level=1;i=b+1
  while level:level+=(s[i]=='{')-(s[i]=='}');i+=1
  s=s[:a]+'  void '+name+'();'+s[i:]
 s=s.replace('  void startWifiScan(bool = false);','''  static void listScreen(UiScreen&,void*) {}
  static void onRowEvent(const fui::ActionEvent&,void*);
  static void onScanEvent(const fui::ActionEvent&,void*);
  static void onPromptEvent(const fui::ActionEvent&,void*);
  static std::string getSignalStrengthIndicator(int32_t);
  void onEnter();void onExit();
  void startWifiScan(bool = false);''')
 s=s.replace('  std::vector<WifiCredential> credentials;', '''  std::vector<WifiCredential> credentials;
  std::string lastConnected;
  void loadFromFile(){record("persist-load");}
  size_t getCredentialCount(){return credentials.size();}
  std::string getLastConnectedSsid(){record("get-last");return lastConnected;}''')
 s=s.replace('static void start(', (HERE/'wifi_extra_original.inc').read_text()+'\nstatic void start(')
 return s
