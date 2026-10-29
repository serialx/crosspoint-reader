#include "Oracle.h"
#include "PersistableStore.h"
#include "network/LcpDevice.h"
#include "network/HttpDownloader.h"
#include <iostream>
std::string response;
bool HttpDownloader::postForm(const std::string& url,const std::string& body,std::string& out,int* status){oracle::record("post %s %s",url.c_str(),body.c_str());*status=oracle::fault=="http"?401:200;out=response;return *status==200;}
int main(int argc,char** argv){
 int scenario=argc>1?atoi(argv[1]):0;
 response="{\"device_id\":\"enrolled-device\"}";
 if(scenario==1){oracle::deviceId="stored-device";oracle::privHex=std::string(64,'f');}
 if(scenario==2)oracle::fault="keygen";
 if(scenario==3)oracle::fault="http";
 if(scenario==4)oracle::fault="save";
 if(scenario==5)response="{\"device_id\":\"\"}";
 if(scenario==6)response="{\"device_id\":\"first\",\"nested\":{\"device_id\":\"last\"}}";
 if(scenario==7)response="{\"device_id\":\""+std::string(80,'x')+"\"}";
 if(scenario==8)response="{\"device_id\":\"incomplete\"";
 if(scenario>=10){oracle::deviceId="stored";oracle::privHex=std::string(64,'0');const char* pairs[]={"ff","+f","-f","0x","x0"," f","  ","9z"};oracle::privHex.replace(0,2,pairs[(scenario-10)%8]);}
 std::string id="before";bool notEnrollable=true;
 bool ok=lcpdevice::ensureEnrolled(id,notEnrollable);oracle::record("enroll %d %d %s",ok,notEnrollable,id.c_str());
 uint8_t out[32]={};ok=lcpdevice::unwrapContentKey(std::string(32,'e'),std::string(12,'i'),std::string(32,'c'),std::string(16,'t'),out);oracle::record("key %d %u",ok,oracle::hash(out,32));
 lcpdevice::forget();
 for(const auto& l:oracle::trace)std::cout<<l<<'\n';
}
