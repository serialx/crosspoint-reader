#include <QrUtils.h>
#include <qrcode.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const std::string text(std::istreambuf_iterator<char>(std::cin), {});
  const auto version = static_cast<uint8_t>(std::atoi(argv[2]));
  const size_t bytes = qrcode_getBufferSize(version);
  std::vector<uint8_t> bitmap(bytes + 2, 0xa5);
  if (std::string(argv[1]) == "app") {
    uint8_t modules = 0;
    assert(QrUtils::encodeQrBitmap(text, bitmap.data() + 1, bytes, modules));
    assert(modules == version * 4 + 17);
  } else {
    const auto sizes = qrcode_getScratchSizes(version, ECC_LOW);
    std::vector<uint8_t> scratch(sizes.codewords + sizes.functionGrid + sizes.errorCorrection + 2, 0xa5);
    QRCode code;
    assert(qrcode_initBytes(&code, bitmap.data() + 1, version, ECC_LOW,
                            reinterpret_cast<uint8_t*>(const_cast<char*>(text.data())), text.size(), scratch.data() + 1,
                            scratch.data() + 1 + sizes.codewords,
                            scratch.data() + 1 + sizes.codewords + sizes.functionGrid) == 0);
    assert(scratch.front() == 0xa5 && scratch.back() == 0xa5);
  }
  assert(bitmap.front() == 0xa5 && bitmap.back() == 0xa5);
  assert(std::fwrite(bitmap.data() + 1, 1, bytes, stdout) == bytes);
}
