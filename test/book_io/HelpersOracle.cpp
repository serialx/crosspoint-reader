#include <AsciiText.h>
#include <XmlQName.h>
#include <Utf8.h>
#include <cstdio>
#include <string>
int main() {
  for (unsigned a=0;a<256;++a) for(unsigned b=0;b<256;++b) {
    std::string s{char(a),char(b)};
    auto t=trimAsciiWhitespace(s);
    std::printf("a %u %u %d %td %zu\n",a,b,isAsciiWhitespace(char(a)),t.data()-s.data(),t.size());
    unsigned char bytes[]{(unsigned char)a,(unsigned char)b,0x80,0x80,0};
    const unsigned char* p=bytes;
    auto cp=utf8NextCodepoint(&p);
    std::printf("u %u %u %u %td\n",a,b,cp,p-bytes);
  }
  for(const char* name : {"",":","a:b","a:b:c","package","ns:package","::package"})
    std::printf("q %s %s %d\n", name, xmlLocalName(name), xmlLocalNameEquals(name,"package"));
  std::printf("q-null %s\n",xmlLocalName(nullptr));
}
