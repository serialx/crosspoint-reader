#include <JpegToBmpConverter.h>
#include <ScreenshotUtil.h>
#include <QrUtils.h>
#include <Print.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
class Output final : public Print {
 public:
  std::vector<uint8_t> bytes;
  size_t limit = SIZE_MAX;
  size_t write(uint8_t b) override { if(bytes.size() == limit) return 0; bytes.push_back(b); return 1; }
};
int main(int argc,char** argv) {
  if(argc < 3) return 2;
  std::string mode(argv[1]);
  if(mode == "jpeg") {
    if(argc != 5) return 2;
    HalFile file; if(!file.open(argv[2],"rb")) return 2;
    Output output; output.limit = std::strtoull(argv[4],nullptr,10);
    int variant = std::atoi(argv[3]);
    bool ok;
    if(variant < 4) ok = JpegToBmpConverter::jpegFileToBmpStream(file,output,variant & 1,variant & 2);
    else if(variant < 8) ok = JpegToBmpConverter::jpegFileToBmpStreamWithSize(file,output,variant == 4 ? 37 : variant == 5 ? 240 : variant == 6 ? 1 : 0,variant == 4 ? 53 : variant == 5 ? 400 : variant == 6 ? 1 : 0);
    else ok = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(file,output,variant == 8 ? 37 : 240,variant == 8 ? 53 : 400);
    std::putchar(ok); std::fwrite(output.bytes.data(),1,output.bytes.size(),stdout);
  } else if(mode == "screenshot") {
    if(argc != 5) return 2;
    int w=std::atoi(argv[3]),h=std::atoi(argv[4]);
    std::vector<uint8_t> fb(w*h/8);
    for(size_t i=0;i<fb.size();++i) fb[i]=(i*73+(i>>5)*19)^0xa5;
    bool ok=ScreenshotUtil::saveFramebufferAsBmp(argv[2],fb.data(),w,h);
    std::putchar(ok);
    std::ifstream f(argv[2],std::ios::binary); std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
    std::fwrite(bytes.data(),1,bytes.size(),stdout);
  } else if(mode == "qr") {
    if(argc != 7) return 2;
    std::ifstream f(argv[2],std::ios::binary); std::string value((std::istreambuf_iterator<char>(f)),{});
    GfxRenderer renderer;
    QrUtils::drawQrCode(renderer,{std::atoi(argv[3]),std::atoi(argv[4]),std::atoi(argv[5]),std::atoi(argv[6])},value);
    for(int value:renderer.rectangles) std::printf("%d,",value);
  } else return 2;
}
