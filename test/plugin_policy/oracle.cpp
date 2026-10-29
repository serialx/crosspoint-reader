#include <HalStorage.h>
#include <SecureHttpClient.h>
#include <CrossPointSettings.h>
#include <components/UITheme.h>
#include "util/PluginEvents.h"
#include "util/PluginHttp.h"
#include "util/PluginLocations.h"

extern "C" time_t time(time_t* p) { if(p) *p=1734212345; return 1734212345; }
void file(const std::string& p,const std::string& s) { oracle::files[p]={s.begin(),s.end()}; }
void response(int status,const std::string& text="ok",bool complete=true) {
  oracle::Response r; r.status=status; r.chunks={text}; r.complete=complete; oracle::responses.push_back(r);
}
void dump() {
  for(const auto& s:oracle::trace) puts(s.c_str());
  for(const auto& [p,b]:oracle::files) {
    printf("FILE %s %zu ",p.c_str(),b.size());
    for(auto c:b) printf("%02x",c);
    puts("");
  }
}
int main(int argc,char** argv) {
  int c=argc>1?atoi(argv[1]):0;
  oracle::dirs={"/plugins","/plugins/demo"};
  const std::string dir="/plugins/demo", queue=dir+"/events.jsonl";
  std::string manifest=R"({"token":{"file":"token.json","path":"auth.token"},"config":{"file":"config.json"},"auth":{"type":"password","token_path":"access_token","request":{"url":"https://auth","body":"{cfg.secret}:{event.book}","headers":{"Auth":"{token}"}}},"events":{"unknown":{},"reader.exit":{"connect":true,"request":{"url":"https://send/{event.id}","headers":{"Auth":"{token}"},"body":"{event.book}|{event.percent}|{event.ts}|{cfg.secret}|{meta.id}"},"toast":"Sent {event.book}"},"sleep.enter":{"download":{"url":"https://image","dest":"/sleep.bmp"}}}})";
  if(c==20) manifest=R"({"events":{"reader.exit":{}}})";
  if(c==21) manifest="{";
  if(c==22) manifest=R"({"events":{"reader.exit":{"download":{"url":"https://image","dest":"/.crosspoint/wifi.json"}}}})";
  if(c==23) manifest=R"({"events":{"reader.exit":{"download":{"url":"https://image","dest":"/book.epub"}}}})";
  if(c==24) manifest=R"({"events":{"reader.exit":{"download":{"url":"https://image"},"request":{"url":"https://ignored"}}}})";
  file(dir+"/device.json",manifest);
  file(dir+"/token.json",R"({"auth":{"token":"old"}})");
  file(dir+"/config.json",R"({"secret":"value","n":42,"b":true,"nested":{},"null":null})");
  file("/book.epub.meta.json",R"({"id":"123"})");
  if(c==25) SETTINGS.pluginsEnabled=false;
  if(c==26) file(dir+"/.installing","");
  if(c==27) { oracle::dirs.insert("/.crosspoint/plugins"); oracle::dirs.insert("/.crosspoint/plugins/demo"); }
  pluginevents::refreshSubscriptions();
  printf("MASK %u ANY %d\n",pluginevents::subscriptionMask("demo"),pluginevents::anySubscriber(pluginevents::Event::ReaderExit));
  const pluginevents::Var vars[]={{"book","/book.epub"},{"percent","74"}};
  if(c==1) file(queue,std::string(4097,'x'));
  if(c==2) file(queue,std::string(4096,'x'));
  if(c==3) oracle::fault="open";
  if(c==4) oracle::fault="write";
  if(c==5) { std::string big(550,'x'); pluginevents::Var v={"book",big.c_str()}; pluginevents::emit(pluginevents::Event::ReaderExit,&v,1); }
  else pluginevents::emit(pluginevents::Event::ReaderExit,vars,2);
  oracle::fault.clear();
  pluginevents::emit(pluginevents::Event::SleepEnter,vars,2);
  if(c==6) file(queue,"\ninvalid\n{\"e\":\"removed\"}\n{\"e\":\"reader.exit\",\"vars\":{\"book\":\"/book.epub\"}}");
  printf("CONNECT %d\n",pluginevents::wantsConnectAny());
  if(c==7) { response(401); response(200,R"({"access_token":"fresh"})"); response(204); }
  else if(c==8) { response(403); response(200,R"({"access_token":"fresh"})"); response(500); }
  else if(c==9) { response(401); response(200,"{}"); }
  else if(c==10) response(500);
  else if(c==11) response(200,"partial",false);
  else response(200);
  response(c==12?404:200,"image");
  response(200);
  file("/sleep.bmp","old-image");
  if(c==13) oracle::fault="backup";
  if(c==14) oracle::fault="swap";
  if(c==15) oracle::fault="rename";
  if(c==16) oracle::fault="rewrite";
  if(c==17) oracle::fault="read";
  if(c==18) oracle::fault="read-oom";
  if(c==19) oracle::fault="write";
  GfxRenderer renderer;
  pluginevents::drain(&renderer,c==16?1:c==28?0:4);
  if(c>=29) {
    JsonDocument doc;
    deserializeJson(doc,R"({"a":[{"n":123}],"s":"x","b":true,"f":1.5,"null":null})");
    for(const auto* path:{"a.0.n","..a..0x.n.","b","f","null","missing","s"}) printf("VALUE %s %s\n",path,pluginhttp::variantToString(pluginhttp::resolvePath(doc,path)).c_str());
    std::string s="{x}{x}-{y}"; pluginhttp::substituteAll(s,"{x}","{x}!"); printf("SUB %s\n",s.c_str());
    printf("URL %s\n",pluginhttp::urlEncodeQuery("a /+_~é").c_str());
    for(const auto* path:{"a..b.","", "0.1"}) printf("TOKEN %s %d\n",path,pluginhttp::saveTokenToFile(dir+"/save.json",path,"q\"\n"));
    if(c==30) oracle::fault="write";
    if(c==31) oracle::fault="reserve";
    if(c==32) oracle::fault="concat";
    String out="prior"; response(400,"error"); oracle::responseIndex=oracle::responses.size()-1;
    printf("HTTP %d\n",pluginhttp::request(nullptr,"https://example","GET","",{},out,c==33?2:16));
    printf("BODY %s\n",out.c_str());
    printf("SAVE %d\n",pluginhttp::saveTokenToFile(dir+"/save.json","a.b","new"));
  }
  dump();
}
