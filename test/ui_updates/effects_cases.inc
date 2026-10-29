static void reset(const char*label){cfg={};entries.clear();WiFi.modeValue=0;record("CASE %s",label);}
static ActivityResult pathResult(){ActivityResult r;r.data=FilePathResult{"/firmware.bin"};return r;}
static ActivityResult cancelled(){ActivityResult r;r.isCancelled=true;return r;}
static void seed(FontDownloadActivity&a){
 const char names[]="\0Family\0one.cpfont\0two.cpfont\0Group\0";
 a.stringArena_=std::make_unique<char[]>(sizeof(names));memcpy(a.stringArena_.get(),names,sizeof(names));
 a.families_={{1,0,0,2,200,1,false,false}};
 a.files_=std::make_unique<FontDownloadActivity::ManifestFile[]>(2);
 a.files_[0]={8,100,42};a.files_[1]={19,100,42};a.filteredIndices_={0};a.baseUrl_="http://fonts/";a.currentFileTotal_=2;
}
bool FontDownloadActivity::fetchAndParseManifest(){record("manifest.fetch");return cfg.fileOpen;}
bool FontDownloadActivity::computeFileCrc32(const char*p,uint32_t&out){record("font.crc %s",p);out=cfg.crc;return cfg.crcOpen;}
fui::ListNav& FontDownloadActivity::activeNav(){return state_==GROUP_LIST?groupNav_:nav;}
void FontDownloadActivity::buildScreen(UiScreen&){}
int main(){
 GfxRenderer r;MappedInputManager i;
 for(int result:{0,1,2})for(bool newer:{false,true}){
  reset("ota-check");cfg.otaCheck=result;cfg.newer=newer;
  OtaUpdateActivity a(r,i);a.onEnter();a.onWifiSelectionComplete(true);record("state %d sleep %d",a.state,a.preventAutoSleep());a.render(Activity::RenderLock(a));a.onExit();
 }
 reset("ota-wifi-cancel");{OtaUpdateActivity a(r,i);a.onWifiSelectionComplete(false);}
 for(int result:{0,2,3}){
  reset("ota-install");cfg.otaInstall=result;OtaUpdateActivity a(r,i);a.runUpdateInstall();record("state %d",a.state);a.render(Activity::RenderLock(a));a.loop();
 }
 reset("ota-progress");{OtaUpdateActivity a(r,i);a.state=OtaUpdateActivity::UPDATE_IN_PROGRESS;for(unsigned n:{0,1,2,3,4,49,50,99,100}){a.updater.processed=n;a.render(Activity::RenderLock(a));record("percent %u",a.lastUpdaterPercentage);}}
 for(int state:{4,5})for(int edge:{0,1,2,3}){
  reset("ota-dismiss");OtaUpdateActivity a(r,i);a.state=OtaUpdateActivity::State(state);i={};i.pressed=edge==0?1:edge==1?2:0;i.released=edge==2?1:0;i.tap=edge==3;a.loop();record("finished %d",a.finished);
 }
 i={};
 for(bool recovery:{false,true}){
  reset("sd-picker-cancel");SdFirmwareUpdateActivity a(r,i,recovery);a.onEnter();a.onPickerResult(cancelled());
  reset("sd-confirm-cancel");SdFirmwareUpdateActivity b(r,i,recovery);b.onConfirmationResult(cancelled());
  for(int edge:{0,1,2,3}){reset("sd-failed-input");SdFirmwareUpdateActivity c(r,i,recovery);c.state=SdFirmwareUpdateActivity::State::FAILED;i={};i.pressed=edge==0?1:edge==1?2:0;i.released=edge==2?1:0;i.tap=edge==3;c.loop();record("state %d",int(c.state));}
 }
 i={};
 for(int mode=0;mode<9;++mode){
  reset("sd-validate");cfg.fileOpen=mode!=1;cfg.partition=mode!=2;cfg.size=mode==3?1001:200;cfg.validate=mode>=4?mode-3:0;
  SdFirmwareUpdateActivity a(r,i);a.onPickerResult(pathResult());record("state %d error %s",int(a.state),a.errorMessage.c_str());a.render(Activity::RenderLock(a));
 }
 for(int result:{0,3,4,6}){
  reset("sd-flash");cfg.flash=result;SdFirmwareUpdateActivity a(r,i);a.firmwarePath="/firmware.bin";a.firmwareSize=200;a.onConfirmationResult(ActivityResult{});record("state %d error %s",int(a.state),a.errorMessage.c_str());a.render(Activity::RenderLock(a));
 }
 reset("sd-progress");{SdFirmwareUpdateActivity a(r,i);a.state=SdFirmwareUpdateActivity::State::UPDATING;a.firmwareSize=1000;for(unsigned n:{0,1,9,10,11,19,20,999,1000}){a.writtenBytes=n;a.render(Activity::RenderLock(a));record("percent %u",a.lastRenderedPercent);}}
 for(int mode=0;mode<3;++mode){
  reset("cache-clear");cfg.rootOpen=mode!=1;cfg.rootDir=mode!=2;entries={{"epub_good",true,true},{"txt_bad",true,false},{"xtc_ok",true,true},{"settings",true,true},{"epub_file",false,true}};
  ClearCacheActivity a(r,i);a.onEnter();a.beginClear();record("state %d removed %d failed %d",a.state,a.clearedCount,a.failedCount);a.render(Activity::RenderLock(a));
 }
 for(int select:{0,1}){reset("cache-confirm");ClearCacheActivity a(r,i);a.onEnter();a.confirmPopup.selected=select;i={};i.released=2;a.loop();record("state %d finished %d",a.state,a.finished);}
 i={};
 for(int mode=0;mode<10;++mode){
  reset("font-transfer");FontDownloadActivity a(r,i);seed(a);
  cfg.freeHeap=mode==1?100:200000;cfg.ensure=mode!=2;cfg.http=mode==3?2:0;cfg.crcOpen=mode!=4;cfg.crc=mode==5?43:42;cfg.fontValid=mode!=6;i.held=mode==7?1:0;i.pressed=mode==8?1:0;i.home=mode==9;
  a.downloadFamily(a.families_[0]);record("state %d files %zu installed %d update %d cancel %d home %d error %s",a.state_,a.currentFileIndex_,a.families_[0].installed,a.families_[0].hasUpdate,a.cancelRequested_,a.goHomeRequested_,a.errorMessage_.c_str());
 }
 i={};
 for(int mode=0;mode<6;++mode){reset("font-input");FontDownloadActivity a(r,i);seed(a);a.state_=mode<3?FontDownloadActivity::COMPLETE:FontDownloadActivity::ERROR;i={};i.pressed=mode%3==0?1:mode%3==1?2:0;i.tap=mode%3==2;a.handleCustomInput();record("state %d",a.state_);}
 i={};
 for(bool ok:{false,true}){reset("font-wifi");FontDownloadActivity a(r,i);a.onEnter();cfg.fileOpen=ok;a.onWifiSelectionComplete(true);record("state %d",a.state_);a.onExit();}
 for(int phase=0;phase<7;++phase){reset("font-view");FontDownloadActivity a(r,i);seed(a);a.state_=FontDownloadActivity::State(phase);a.errorMessage_="failure";a.fileProgress_=49;a.fileTotal_=100;a.render(Activity::RenderLock(a));record("sleep %d",a.preventAutoSleep());}
}
