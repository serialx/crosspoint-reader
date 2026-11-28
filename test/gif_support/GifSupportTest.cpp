#include <GifLimitedWriter.h>
#include <gtest/gtest.h>

namespace {
class CountingOutput : public Print {
 public:
  size_t total = 0;
  size_t write(uint8_t) override {
    ++total;
    return 1;
  }
  size_t write(const uint8_t*, size_t size) override {
    total += size;
    return size;
  }
};

TEST(GifSupport, StopsAnExtractionAtTheByteLimit) {
  CountingOutput output;
  GifLimitedWriter writer(output, 10);
  constexpr uint8_t bytes[8] = {};
  EXPECT_EQ(writer.write(bytes, 8), 8u);
  EXPECT_EQ(writer.write(bytes, 8), 0u);
  EXPECT_EQ(writer.write(bytes, 2), 2u);
  EXPECT_EQ(writer.write(0), 0u);
  EXPECT_EQ(output.total, 10u);
}

}  // namespace
