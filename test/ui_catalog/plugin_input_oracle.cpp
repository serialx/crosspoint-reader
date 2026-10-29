#include <cstdio>
#include <string>
#include <vector>
#include "paging_original.inc"
int main() {
 using namespace plugincatalogpaging;
 for(size_t start: {0,1,8,16}) for(size_t limit: {0,1,8,16}) for(size_t count: {0,1,8,9,16,17,32}) {
  PageWindow w(start,limit);unsigned mask=0;
  for(size_t i=0;i<count;++i) if(w.take(i%3!=1))mask|=1u<<i;
  std::printf("window %zu %zu %zu %u %d\n",start,limit,count,mask,w.hasMore());
 }
 for(auto url:{"feed", "feed?page={page}"})for(auto body:{"", "{page}"})for(auto header:{"", "{page}"}) {
  std::vector<std::pair<std::string,std::string>> headers={{"test",header}};
  std::printf("static %d %d %d %d\n",url[4]!=0,body[0]!=0,header[0]!=0,isStaticFeed(url,body,headers));
 }
 // Original custom dispatch priorities: Back before Retry, completion before list,
 // device-code Back before polling; no list input while transfer/children own it.
 for(int state=0;state<12;state++)for(int flags=0;flags<16;flags++) {
  bool back=flags&1,confirm=flags&2,tap=flags&4,due=flags&8;
  int effect=0,mode=state;
  if(state==11) {if(back){mode=10;effect=9;} else if(due)effect=10;}
  else if(state==10){if(back)effect=1;else if(confirm||tap)effect=5;}
  else if(state==9){if(back||confirm||tap)effect=8;}
  else if(state==6){if(back)effect=1;else if(confirm||tap)effect=5;}
  else if(state==0||state==3){if(back)effect=1;}
  std::printf("input %d %d %d %d\n",state,flags,effect,mode);
 }
}
