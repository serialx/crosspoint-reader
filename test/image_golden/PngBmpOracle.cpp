#include <BuildScratch.h>
#include <PngToBmpConverter.h>
#include <Print.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

class Output : public Print {
 public:
  std::vector<uint8_t> bytes;
  size_t write(uint8_t byte) override {
    bytes.push_back(byte);
    return 1;
  }
};

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) return 2;
  alignas(uintptr_t) static uint8_t scratch[60000];
  if (argc == 4) buildscratch::lend(scratch, sizeof(scratch));
  HalFile file;
  if (!file.open(argv[1], "rb")) return 2;
  Output out;
  const int mode = std::atoi(argv[2]);
  bool ok;
  if (mode < 4)
    ok = PngToBmpConverter::pngFileToBmpStream(file, out, mode & 1, mode & 2);
  else if (mode == 4)
    ok = PngToBmpConverter::pngFileToBmpStreamWithSize(file, out, 37, 53);
  else
    ok = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(file, out, 37, 53);
  if (argc == 4) {
    auto* loan = buildscratch::claim(sizeof(scratch));
    if (loan != scratch) std::abort();
    buildscratch::release(loan);
    buildscratch::reclaim();
  }
  std::putchar(ok);
  std::fwrite(out.bytes.data(), 1, out.bytes.size(), stdout);
}
