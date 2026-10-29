#include <blocks/ImageBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <GfxRenderer.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
int main(int argc,char** argv) {
  if(argc!=5) return 2;
  const std::string path=std::string(argv[1])+"/image.jpg";
  const std::string cache=std::string(argv[1])+"/image.px2";
  const int variant=std::atoi(argv[2]),orientation=std::atoi(argv[3]),mode=std::atoi(argv[4]);
  int w=407,h=311;
  std::vector<uint8_t> bytes(4+((w+3)/4)*h);
  bytes[0]=w;bytes[1]=w>>8;bytes[2]=h;bytes[3]=h>>8;
  for(size_t i=4;i<bytes.size();++i) bytes[i]=(i*71+(i>>6))^0x96;
  if(variant==2) bytes.resize(3);
  if(variant==3) bytes.resize(bytes.size()-1);
  if(variant==4) bytes[0]=23;
  if(variant<5) {auto* file=std::fopen(cache.c_str(),"wb");std::fwrite(bytes.data(),1,bytes.size(),file);std::fclose(file);}
  if(variant==6 || variant==7) {auto* file=std::fopen(path.c_str(),"wb");if(variant==7) std::fputc(1,file);std::fclose(file);}
  if(variant==1) ESP.freeHeap=0;
  GfxRenderer renderer;renderer.orientation=static_cast<GfxRenderer::Orientation>(orientation);renderer.mode=static_cast<GfxRenderer::RenderMode>(mode%3);renderer.absolute=mode>=3;
  if(variant==8) renderer.fonts.scanning=true;
  ImageBlock image(path,"",w,h);
  std::putchar(image.hasValidCache());std::putchar(image.needsDecode());
  for(int i=0;i<3;++i) image.render(renderer,variant==9 ? -1:11,17);
  std::putchar(image.needsDecode());
  ImageBlock::clearRenderFailures();ImageBlock::releaseRenderCache();
  std::fwrite(renderer.buffer.data(),1,renderer.buffer.size(),stdout);
  for(int n:renderer.rectangles) std::printf("%d,",n);
}
