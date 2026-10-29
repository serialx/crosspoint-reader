#include <cstdint>
#include <cstdio>
#include <initializer_list>
int main() {
  for (uint32_t total : {1u,100u,6553600u,16777217u,UINT32_MAX}) {
    for (uint32_t processed : {0u,1u,total-1,total,total==UINT32_MAX?total:total+1}) {
      const float progress=static_cast<float>(processed)/static_cast<float>(total);
      const auto pct=static_cast<int>(progress*100);
      const uint32_t sd=total>0?processed*100/total:0;
      std::printf("%u %u %d %u\n",processed,total,pct,sd);
    }
  }
}
