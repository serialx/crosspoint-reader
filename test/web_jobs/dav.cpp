#include "Oracle.h"
#include "network/WebDAVHandler.h"
#include <iostream>
int main(){
 for(auto method:{HTTP_OPTIONS,HTTP_PROPFIND,HTTP_GET,HTTP_HEAD,HTTP_PUT,HTTP_DELETE,HTTP_MKCOL,HTTP_MOVE,HTTP_COPY,HTTP_LOCK,HTTP_UNLOCK,HTTP_POST})
 for(int scenario=0;scenario<12;++scenario){
  oracle::reset();oracle::files["/book.epub"]={'a','b','c'};oracle::dirs={"/","/folder"};
  WebServer s;s.verb=method;s.headers["Destination"]="https://host/new.epub";
  if(scenario==1)s.path="/.hidden/book.epub";
  if(scenario==2)s.path="/absent";
  if(scenario==3)s.path="/";
  if(scenario==4)s.path="/folder";
  if(scenario==5)oracle::fault="open";
  if(scenario==6)oracle::fault="write";
  if(scenario==7){s.headers["Destination"]="/book.epub";s.headers["Overwrite"]="F";}
  if(scenario==8)s.headers["Destination"]="/absent/file";
  if(scenario==9)s.headers["Depth"]="0";
  if(scenario==10)oracle::fault="body";
  if(scenario==11)s.path="/book%2Eepub";
  {
   WebDAVHandler h;oracle::record("can %d %d",h.canHandle(s,method,s.path),h.canRaw(s,s.path));
   if(method==HTTP_PUT){HTTPRaw raw{RAW_START};h.raw(s,s.path,raw);uint8_t data[]={1,2,3,4};raw={RAW_WRITE,data,4,4};h.raw(s,s.path,raw);raw.status=scenario==9?RAW_ABORTED:RAW_END;h.raw(s,s.path,raw);}
   oracle::record("handled %d",h.handle(s,method,s.path));
  }
  std::cout<<"CASE "<<int(method)<<'-'<<scenario<<'\n';for(const auto& l:oracle::trace)std::cout<<l<<'\n';for(const auto& [p,b]:oracle::files)std::cout<<"FILE "<<p<<' '<<b.size()<<' '<<oracle::hash(b.data(),b.size())<<'\n';
 }
}
