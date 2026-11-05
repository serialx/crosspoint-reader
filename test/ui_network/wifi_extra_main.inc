int main() {
 for(bool automatic:{false,true}) for(int saved:{0,1,2}) for(size_t length:{size_t(33),size_t(64),size_t(255)}) {
  start("wifi-entry:"+std::to_string(automatic)+":"+std::to_string(saved)+":"+std::to_string(length));
  WifiSelectionActivity a;a.allowAutoConnect=automatic;
  const std::string ssid(length,'x');
  if(saved) WIFI_STORE.credentials={{ssid,saved==1?"":"secret"}};
  WIFI_STORE.lastConnected=ssid;
  a.onEnter();record("state:"+std::to_string(int(a.state)));a.onExit();flush();
 }
 for(bool hidden:{false,true}) for(bool cancelled:{false,true}) for(const std::string value:{"", "a wireless network", "비공개 네트워크"}) {
  start("wifi-keyboard:"+std::to_string(hidden)+":"+std::to_string(cancelled)+":"+value);
  WifiSelectionActivity a;
  a.networks={{"ordinary",-50,true,false,false}};
  if(hidden) a.promptHiddenSsid();else {a.selectedSSID="ordinary";a.promptPasswordEntry();}
  ActivityResult result;result.isCancelled=cancelled;result.data=KeyboardResult{value};childResult(result);
  record("after-child:"+std::to_string(int(a.state))+":"+a.selectedSSID+":"+a.enteredPassword);
  a.loop();record("after-loop:"+std::to_string(int(a.state)));childResult={};flush();
 }
 for(int phase:{2,7,9}) for(bool longPress:{false,true}) for(int choice:{0,1}) {
  start("wifi-touch:"+std::to_string(phase)+":"+std::to_string(longPress)+":"+std::to_string(choice));
  WifiSelectionActivity a;a.state=WifiSelectionState(phase);
  a.networks={{"saved",-50,true,true,false},{"new",-70,false,false,false}};
  a.selectedSSID="saved";a.enteredPassword="secret";
  fui::ActionEvent event{static_cast<int16_t>(choice),longPress};
  if(phase==2) a.onRowEvent(event,&a);else a.onPromptEvent(event,&a);
  record("state:"+std::to_string(int(a.state)));record("selected:"+a.selectedSSID);flush();
 }
 for(int variant:{0,1,2}) {
  start("wifi-dedup:"+std::to_string(variant));
  WifiSelectionActivity a;
  WIFI_STORE.credentials={{"saved","password"}};
  WiFi.rows={{"equal-a",-50,0},{"equal-b",-50,1},{"equal-a",-50,1},{"saved",-90,1},{"",-10,0},{std::string(40,'x'),-40,1},{"한글",-70,0}};
  if(variant==1) std::reverse(WiFi.rows.begin(),WiFi.rows.end());
  if(variant==2) WiFi.rows.push_back({"equal-a",-49,1});
  WiFi.scan=WiFi.rows.size();a.processWifiScanResults();
  for(size_t i=0;i<a.networks.size();++i) {
   const auto& row=a.networks[i];
   record("row:"+row.ssid+":"+std::to_string(row.rssi)+":"+std::to_string(row.isEncrypted)+":"+std::to_string(row.hasSavedPassword)+":"+a.networkStatuses[i]);
  }
  flush();
 }
}
