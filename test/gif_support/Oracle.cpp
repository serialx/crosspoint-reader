#include <GifCommon.h>
#include <GifToBmpConverter.h>
#include <Print.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
void word(uint32_t value) {
  for (int i = 0; i < 4; ++i) std::putchar((value >> (8 * i)) & 255);
}
class Output final : public Print {
 public:
  std::vector<uint8_t> bytes;
  size_t limit = SIZE_MAX;
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    len = std::min(len, limit - bytes.size());
    bytes.insert(bytes.end(), data, data + len);
    return len;
  }
};
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) return 2;
  HalFile file;
  if (!file.open(argv[1], "rb")) return 3;
  GifBasicInfo info;
  word(GifCommon::readBasicInfo(file, info));
  word(info.canvasWidth);
  word(info.canvasHeight);
  word(info.frameX);
  word(info.frameY);
  word(info.frameWidth);
  word(info.frameHeight);
  word(info.interlaced);
  word(file.position());
  std::fflush(stdout);
  const int mode = std::atoi(argv[2]);
  Output output;
  if (argc == 4) output.limit = std::strtoull(argv[3], nullptr, 10);
  bool ok;
  if (mode < 4) {
    ok = GifToBmpConverter::gifFileToBmpStream(file, output, mode & 1, mode & 2);
  } else {
    constexpr int widths[] = {8, 4, 32};
    constexpr int heights[] = {4, 4, 16};
    ok = GifToBmpConverter::gifFileTo1BitBmpStreamWithSize(file, output, widths[mode - 4], heights[mode - 4]);
  }
  word(ok);
  word(output.bytes.size());
  std::fwrite(output.bytes.data(), 1, output.bytes.size(), stdout);
}
