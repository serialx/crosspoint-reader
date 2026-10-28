#include "Oracle.h"
#include "SecureHttpClient.h"
#include "network/FirmwareBoardTag.h"
#include "network/FirmwareFlasher.h"
#include "network/HttpDownloader.h"
#include "network/ProtectedPaths.h"
#include "esp_partition.h"
#include <iostream>
namespace ota_boot { bool switchTo(const esp_partition_t*) { oracle::record("switch"); return oracle::fault!="switch"; } }
static void dump(const std::string& name) {
 std::cout<<"CASE "<<name<<'\n';
 for(const auto& l:oracle::trace) std::cout<<l<<'\n';
 for(const auto& [p,b]:oracle::files) std::cout<<"FILE "<<p<<' '<<b.size()<<' '<<oracle::hash(b.data(),b.size())<<'\n';
}
static std::vector<uint8_t> firmware(bool sha=false) {
 std::vector<uint8_t> b(65536+(sha?32:0),0); b[0]=0xe9; b[1]=1; b[12]=5; b[23]=sha;
 const unsigned len=65500; memcpy(b.data()+28,&len,4);
 for(size_t i=32;i<65532;++i) b[i]=uint8_t(i);
 uint8_t checksum=0xef; for(size_t i=32;i<65532;++i) checksum^=b[i]; b[65535]=checksum;
 if(sha) {unsigned h=oracle::hash(b.data(),65536);for(size_t i=0;i<32;++i)b[65536+i]=uint8_t(h>>(8*(i%4)));}
 return b;
}
int main() {
 for(bool sha:{false,true}) for(int mutation=0;mutation<10;++mutation) {
  oracle::reset(); auto b=firmware(sha);
  switch(mutation) {case 1:b[0]=0;break;case 2:b[12]=9;break;case 3:b[31]=255;break;case 4:b[65535]^=1;break;case 5:b.pop_back();break;case 6:b.resize(50);break;case 7:memcpy(b.data()+100,"CROSSPOINT-BOARD-V1:sticky;",26);break;case 8:oracle::fault="open";break;case 9:oracle::fault="read";break;}
  oracle::files["/fw.bin"]=b;
  auto r=firmware_flash::validateImageFile("/fw.bin",131072);
  oracle::record("result %s",firmware_flash::resultName(r)); dump("validate-"+std::to_string(sha)+"-"+std::to_string(mutation));
 }
 for(const char* fault:{"","open","read","partition","erase","partition-write","switch"}) for(bool skip:{false,true}) {
  oracle::reset();oracle::files["/fw.bin"]=firmware();oracle::fault=fault;
  auto r=firmware_flash::flashFromSdPath("/fw.bin",[](size_t n,size_t total,void*){oracle::record("progress %zu %zu",n,total);},nullptr,skip);
  oracle::record("result %s",firmware_flash::resultName(r));dump(std::string("flash-")+fault+"-"+std::to_string(skip));
 }
 for(const auto& tag:std::vector<std::string>{"x4","sticky","","abcdefghijklmnopqrstuvw","abcdefghijklmnopqrstuvwx","x4\x01"}) for(size_t split=1;split<30;++split) {
  oracle::reset();std::string data="prefixCCROSSPOINT-BOARD-V1:"+tag+";CROSSPOINT-BOARD-V1:x4;";
  board_tag::Scanner scan; for(size_t i=0;i<data.size();i+=split)scan.feed(reinterpret_cast<const uint8_t*>(data.data()+i),std::min(split,data.size()-i));
  oracle::record("tag %d %s",scan.mismatch(),scan.foundName());dump("tag-"+tag+"-"+std::to_string(split));
 }
 for(int kind=0;kind<5;++kind) for(int scenario=0;scenario<14;++scenario) {
  oracle::reset();oracle::responses={oracle::Response{}};oracle::responseIndex=0; bool cancel=scenario==8;
  auto& r=oracle::responses[0];std::string url="https://host/file";
  if(scenario==1){r.known=false;r.chunks={"a","bc","def"};}
  if(scenario==2){r.status=401;r.chunks={"denied"};}
  if(scenario==3){r.status=403;}
  if(scenario==4){r.status=500;}
  if(scenario==5){r.chunks={"abc"};r.complete=false;oracle::Response next;next.status=206;next.chunks={"def"};next.length=3;next.range="bytes 3-5/6";oracle::responses.push_back(next);}
  if(scenario==6){r.status=302;r.location="https://other/file";oracle::responses.push_back({});}
  if(scenario==7){r.chunks.clear();r.length=0;}
  if(scenario==9)oracle::fault="write";
  if(scenario==10)oracle::fault="replace";
  if(scenario==11)url="bad";
  if(scenario==12){r.chunks={std::string(5000,'x')};r.length=5000;}
  if(scenario==13){r.chunks={"abc"};r.complete=false;oracle::responses.push_back({});}
  std::string output; int status=-7;bool ok=false;
  auto sink=[&](const uint8_t* p,size_t n){oracle::record("sink %zu %u",n,oracle::hash(p,n));output.append(reinterpret_cast<const char*>(p),n);return scenario!=9;};
  auto progress=[](size_t n,size_t total){oracle::record("progress %zu %zu",n,total);};
  if(kind==0) {auto result=HttpDownloader::downloadToFile(url,"/book",progress,&cancel,"user","pass",{{"X-Test","value"}},scenario==6);oracle::record("result %d",result);}
  if(kind==1) {auto result=HttpDownloader::postToFile(url,"payload","custom/type","/book",progress,&cancel);oracle::record("result %d",result);}
  if(kind==2){ok=HttpDownloader::fetchUrl(url,sink,"user","pass",{{"X-Test","value"}});oracle::record("ok %d",ok);}
  if(kind==3){HttpDownloader::FetchOptions options;options.username="user";options.password="pass";options.bearer="token";options.accept="json";options.acceptLanguage="en";options.headers={{"X-Test","value"}};options.captureErrorBody=true;options.statusOut=&status;ok=HttpDownloader::fetchUrl(url,sink,options);oracle::record("ok %d status %d",ok,status);}
  if(kind==4){ok=HttpDownloader::postForm(url,"payload",output,&status);oracle::record("ok %d status %d",ok,status);}
  oracle::record("last %d %d output %zu %u",HttpDownloader::lastStatus(),HttpDownloader::lastAnswerRedirected(),output.size(),oracle::hash(reinterpret_cast<const uint8_t*>(output.data()),output.size()));dump("http-"+std::to_string(kind)+"-"+std::to_string(scenario));
 }
 for(const char* p:{"/","/.crosspoint/wifi.json","//.crosspoint/./wifi.json.","/a/../.crosspoint/opds.json","/CROSSP~1/WIFI~1.JSO","/a/b/IMG~1.JPG","/.fonts/a","/a/../../.crosspoint/koreader.json","/ .crosspoint / wifi.json.backup"}) {oracle::reset();oracle::record("protected %d plugin %d",protectedpaths::isSensitivePath(p),protectedpaths::isPluginPath(p));dump(p);}
}
