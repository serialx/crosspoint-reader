// Header predicate frozen from SleepActivity.cpp at da609bbf.
#include <xp_ffi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

enum {
  PNG_PIXEL_GRAYSCALE = 0,
  PNG_PIXEL_TRUECOLOR = 2,
  PNG_PIXEL_INDEXED = 3,
  PNG_PIXEL_GRAY_ALPHA = 4,
  PNG_PIXEL_TRUECOLOR_ALPHA = 6
};
struct Input {
  const uint8_t* data;
  size_t size, position = 0;
  bool seek(size_t offset) {
    position = offset;
    return offset <= size;
  }
  int read() { return position < size ? data[position++] : -1; }
  int read(uint8_t* output, size_t count) {
    count = std::min(count, size - position);
    std::memcpy(output, data + position, count);
    position += count;
    return count;
  }
};
uint32_t readBE32(Input& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) return 0;
  return (static_cast<uint32_t>(c0) << 24) | (static_cast<uint32_t>(c1) << 16) | (static_cast<uint32_t>(c2) << 8) |
         static_cast<uint32_t>(c3);
}

bool isValidPngHeader(Input& file) {
  static constexpr uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  static constexpr uint32_t MAX_SOURCE_PIXELS = 2048u * 1536u;
  uint8_t signature[8];
  if (!file.seek(0) || file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature)) ||
      !std::equal(std::begin(signature), std::end(signature), std::begin(PNG_SIGNATURE))) {
    return false;
  }

  const uint32_t ihdrLength = readBE32(file);
  char chunkType[4];
  if (file.read(reinterpret_cast<uint8_t*>(chunkType), sizeof(chunkType)) != static_cast<int>(sizeof(chunkType)) ||
      ihdrLength != 13 || !std::equal(std::begin(chunkType), std::end(chunkType), "IHDR")) {
    return false;
  }

  const uint32_t width = readBE32(file);
  const uint32_t height = readBE32(file);
  const int bitDepth = file.read();
  const int colorType = file.read();
  const int compression = file.read();
  const int filter = file.read();
  const int interlace = file.read();

  const bool supportedBitDepth =
      bitDepth == 8 || ((colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_INDEXED) &&
                        (bitDepth == 1 || bitDepth == 2 || bitDepth == 4));
  const bool supportedColorType = colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_TRUECOLOR ||
                                  colorType == PNG_PIXEL_INDEXED || colorType == PNG_PIXEL_GRAY_ALPHA ||
                                  colorType == PNG_PIXEL_TRUECOLOR_ALPHA;
  return width > 0 && height > 0 && width <= 2048 && height <= 3072 && width * height <= MAX_SOURCE_PIXELS &&
         supportedBitDepth && supportedColorType && compression == 0 && filter == 0 && interlace == 0;
}

int main() {
  std::array<uint8_t, 33> valid{137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0,
                                0,   0,  7,  0,  0,  0,  5,  8,  2, 0, 0, 0,  0,  0,  0,  0};
  size_t cases = 0;
  auto check = [&](const auto& bytes, size_t len) {
    Input file{bytes.data(), len};
    if (isValidPngHeader(file) != xp_png_sleep_header(bytes.data(), len)) std::abort();
    ++cases;
  };
  for (size_t length = 0; length <= valid.size(); ++length) check(valid, length);
  for (size_t at = 0; at < valid.size(); ++at) {
    for (int value = 0; value < 256; ++value) {
      auto bytes = valid;
      bytes[at] = value;
      check(bytes, bytes.size());
    }
  }
  for (uint32_t width : {0u, 1u, 2048u, 2049u, 32767u, 0xffffffffu}) {
    for (uint32_t height : {0u, 1u, 1536u, 3072u, 3073u, 0xffffffffu}) {
      auto bytes = valid;
      for (int i = 0; i < 4; ++i) {
        bytes[16 + i] = width >> (24 - i * 8);
        bytes[20 + i] = height >> (24 - i * 8);
      }
      check(bytes, bytes.size());
    }
  }
  std::printf("%zu sleep header cases match da609bbf\n", cases);
}
