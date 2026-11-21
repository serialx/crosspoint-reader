#include <Bitmap.h>
#include <BmpToBmpConverter.h>
#include <HalStorage.h>
#include <Print.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../src/activities/boot_sleep/SleepCoverCache.h"

static int failArrayAllocation = 0;

void* operator new[](size_t bytes, const std::nothrow_t&) noexcept {
  if (failArrayAllocation > 0 && --failArrayAllocation == 0) return nullptr;
  return ::operator new[](bytes);
}

namespace {

class Output final : public Print {
 public:
  std::vector<uint8_t> bytes;
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t count) override {
    bytes.insert(bytes.end(), data, data + count);
    return count;
  }
};

uint32_t le32(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t{b[at]} | uint32_t{b[at + 1]} << 8 | uint32_t{b[at + 2]} << 16 | uint32_t{b[at + 3]} << 24;
}

void put32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// 24bpp or 1bpp BMP whose top (or left) half is black and the other half white.
std::vector<uint8_t> splitBmp(int width, int height, bool topDown, bool oneBit, bool leftRight = false) {
  const int bits = oneBit ? 1 : 24;
  const int rowBytes = (width * bits + 31) / 32 * 4;
  const uint32_t offset = 54 + (oneBit ? 8 : 0);
  std::vector<uint8_t> b = {'B', 'M'};
  put32(b, offset + rowBytes * height);
  put32(b, 0);
  put32(b, offset);
  put32(b, 40);
  put32(b, width);
  put32(b, topDown ? static_cast<uint32_t>(-height) : height);
  b.insert(b.end(), {1, 0, static_cast<uint8_t>(bits), 0});
  for (int i = 0; i < 6; ++i) put32(b, i == 4 && oneBit ? 2 : 0);
  if (oneBit) {
    put32(b, 0);
    put32(b, 0xFFFFFF);
  }
  for (int i = 0; i < height; ++i) {
    const int y = topDown ? i : height - 1 - i;
    if (!leftRight) {
      b.insert(b.end(), rowBytes, y < height / 2 ? 0 : 0xFF);
      continue;
    }
    for (int x = 0; x < rowBytes; ++x) b.push_back(x < rowBytes / 2 ? 0 : 0xFF);
  }
  return b;
}

class BmpToBmpTest : public ::testing::Test {
 protected:
  std::filesystem::path path;
  HalFile file;
  void SetUp() override {
    auto name = (std::filesystem::temp_directory_path() / "crosspoint-bmp-XXXXXX").string();
    const int fd = mkstemp(name.data());
    ASSERT_GE(fd, 0);
    path = name;
    ASSERT_EQ(close(fd), 0);
  }
  void TearDown() override {
    if (file.isOpen()) file.close();
    if (!path.empty()) std::filesystem::remove(path);
  }
  void openBmp(const std::vector<uint8_t>& bmp) {
    std::ofstream sink(path, std::ios::binary);
    sink.write(reinterpret_cast<const char*>(bmp.data()), bmp.size());
    sink.close();
    if (!sink || !file.open(path.c_str(), "rb")) throw std::runtime_error("Cannot open BMP fixture");
  }
  Output convert(const std::vector<uint8_t>& bmp, bool crop, bool thresholds = false) {
    openBmp(bmp);
    Output out;
    if (!BmpToBmpConverter::bmpFileToBmpStream(file, out, crop, thresholds))
      throw std::runtime_error("BMP conversion failed");
    if (!file.close()) throw std::runtime_error("Cannot close BMP fixture");
    return out;
  }
  // Display stub is 64x40, so portrait output is at most 40x64. Returns level of pixel (x, y).
  static int level(const Output& out, int x, int y) {
    const unsigned bits = out.bytes[28];
    const uint32_t width = le32(out.bytes, 18);
    const auto rawHeight = static_cast<int32_t>(le32(out.bytes, 22));
    if (rawHeight > 0) y = rawHeight - 1 - y;
    const size_t row = le32(out.bytes, 10) + y * ((width * bits + 31) / 32 * 4);
    const uint8_t byte = out.bytes[row + x * bits / 8];
    return bits == 1 ? (byte >> (7 - x % 8)) & 1 : (byte >> (6 - (x % 4) * 2)) & 3;
  }
};

TEST_F(BmpToBmpTest, UpscalesSmallImageToFillScreen) {
  for (const bool topDown : {false, true}) {
    const Output out = convert(splitBmp(10, 16, topDown, false), true);
    EXPECT_EQ(le32(out.bytes, 18), 40u);
    EXPECT_EQ(le32(out.bytes, 22), topDown ? static_cast<uint32_t>(-64) : 64u);
    EXPECT_EQ(out.bytes[28], 2);
    EXPECT_EQ(out.bytes.size(), le32(out.bytes, 2));
    EXPECT_EQ(level(out, 20, 2), 0);
    EXPECT_EQ(level(out, 20, 61), 3);
  }
}

TEST_F(BmpToBmpTest, DownscalesAndFits) {
  const Output out = convert(splitBmp(160, 128, false, false), false);
  EXPECT_EQ(le32(out.bytes, 18), 40u);
  EXPECT_EQ(le32(out.bytes, 22), 32u);
  EXPECT_EQ(level(out, 5, 1), 0);
  EXPECT_EQ(level(out, 5, 30), 3);
}

TEST_F(BmpToBmpTest, CropsCenterWhileDownscaling) {
  for (const bool topDown : {false, true}) {
    const Output out = convert(splitBmp(320, 128, topDown, false, true), true);
    EXPECT_EQ(le32(out.bytes, 18), 40u);
    EXPECT_EQ(le32(out.bytes, 22), topDown ? static_cast<uint32_t>(-64) : 64u);
    EXPECT_EQ(level(out, 2, 32), 0);
    EXPECT_EQ(level(out, 37, 32), 3);
  }
}

TEST_F(BmpToBmpTest, OneBitSourceStaysOneBit) {
  const Output out = convert(splitBmp(20, 32, false, true), true);
  EXPECT_EQ(out.bytes[28], 1);
  EXPECT_EQ(le32(out.bytes, 18), 40u);
  EXPECT_EQ(level(out, 3, 2), 0);
  EXPECT_EQ(level(out, 3, 61), 1);
}

TEST_F(BmpToBmpTest, ThresholdMapAndCacheIdentity) {
  auto bmp = splitBmp(20, 32, true, false);
  for (int x = 0; x < 20; ++x) std::fill_n(bmp.begin() + 54 + 3 * x, 3, x * 12);
  for (const bool original : {false, true}) {
    const Output out = convert(bmp, false, original);
    const std::string_view expected =
        original ? "0000000110111111111112212222222222232333" : "0000001111222222222222223233333333333333";
    for (size_t x = 0; x < expected.size(); ++x) EXPECT_EQ(level(out, x, 0), expected[x] - '0') << x;
    SleepCoverCache::Paths paths;
    ASSERT_TRUE(SleepCoverCache::makePaths(paths, "/sleep.bmp", bmp.size(), 123, 40, 64, false, original));
    const std::string key = "/sleep.bmp|" + std::to_string(bmp.size()) + "|123|40x64|f" + (original ? "|t" : "");
    const auto upstream = "/.crosspoint/sleep_" + std::to_string(std::hash<std::string>{}(key)) + ".bmp";
    EXPECT_EQ(paths.final, upstream);
    EXPECT_EQ(paths.temp, upstream + ".tmp");
  }
}

TEST_F(BmpToBmpTest, CacheKeyAllocationCanFailBeforeConversion) {
  SleepCoverCache::Paths paths;
  failArrayAllocation = 1;
  EXPECT_FALSE(SleepCoverCache::makePaths(paths, "/sleep.bmp", 64, 0, 40, 64, false, false));
  EXPECT_EQ(failArrayAllocation, 0);
}

TEST_F(BmpToBmpTest, AbandonsConversionOnRowAllocationFailure) {
  openBmp(splitBmp(10, 16, true, false));
  for (int allocation = 1; allocation <= 4; ++allocation) {
    failArrayAllocation = allocation;
    Output out;
    EXPECT_FALSE(BmpToBmpConverter::bmpFileToBmpStream(file, out, false, false));
    EXPECT_TRUE(out.bytes.empty());
    ASSERT_EQ(failArrayAllocation, 0);
  }
}

TEST_F(BmpToBmpTest, RejectsCacheWithOneOfTwoRows) {
  auto bmp = splitBmp(8, 2, false, true);
  bmp.resize(bmp.size() - 4);
  openBmp(bmp);
  Bitmap cached(file);
  ASSERT_EQ(cached.parseHeaders(), BmpReaderError::Ok);
  EXPECT_FALSE(cached.hasCompletePixelData());
  Output out;
  EXPECT_FALSE(BmpToBmpConverter::bmpFileToBmpStream(file, out, false, false));
  EXPECT_TRUE(out.bytes.empty());
}

TEST_F(BmpToBmpTest, ReadsRowsAcrossFourGiB) {
  auto bmp = splitBmp(8, 2, true, false);
  constexpr uint32_t start = UINT32_MAX - 15;
  for (int i = 0; i < 4; ++i) bmp[10 + i] = start >> (8 * i);
  std::ofstream sink(path, std::ios::binary);
  sink.write(reinterpret_cast<const char*>(bmp.data()), 54);
  sink.seekp(start);
  sink.write(reinterpret_cast<const char*>(bmp.data() + 54), 48);
  sink.close();
  ASSERT_TRUE(sink);
  ASSERT_TRUE(file.open(path.c_str(), "rb"));
  Bitmap image(file);
  ASSERT_EQ(image.parseHeaders(), BmpReaderError::Ok);
  uint8_t gray[8], raw[24];
  ASSERT_EQ(image.readRowGray(1, gray, raw), BmpReaderError::Ok);
  EXPECT_TRUE(std::all_of(gray, gray + 8, [](uint8_t value) { return value == 255; }));
  EXPECT_EQ(image.readRowGray(2, gray, raw), BmpReaderError::SeekPixelDataFailed);
}

}  // namespace
