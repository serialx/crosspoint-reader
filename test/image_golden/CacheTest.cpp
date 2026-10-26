#include <ImagePipeline.h>
#include <gtest/gtest.h>

#include <filesystem>

namespace {
class PixelCacheTest : public ::testing::Test {
 protected:
  std::string path;
  GfxRenderer renderer;
  RenderConfig config;
  void SetUp() override {
    path = (std::filesystem::temp_directory_path() /
            (std::string("crosspoint-pixel-cache-") + ::testing::UnitTest::GetInstance()->current_test_info()->name()))
               .string();
    config.maxWidth = 4;
    config.maxHeight = 1;
    config.useExactDimensions = true;
    config.cachePath = path;
    HalFileTest::failNextWrite = false;
    HalFileTest::failNextClose = false;
  }
  void TearDown() override {
    HalFileTest::failNextWrite = false;
    HalFileTest::failNextClose = false;
    Storage.remove(path);
  }
};
TEST_F(PixelCacheTest, PublishesCompleteCache) {
  ImagePipeline cache(renderer, config);
  ASSERT_TRUE(cache.init(4, 1, ImagePipeline::Kind::Png));
  ASSERT_TRUE(cache.prepare());
  ASSERT_TRUE(cache.beginCache());
  bool success = false;
  EXPECT_TRUE(cache.finish(success));
  EXPECT_TRUE(success);
  EXPECT_TRUE(Storage.exists(path.c_str()));
  EXPECT_EQ(std::filesystem::file_size(path), 5u);
}
TEST_F(PixelCacheTest, RejectsFinalWriteFailure) {
  ImagePipeline cache(renderer, config);
  ASSERT_TRUE(cache.init(4, 1, ImagePipeline::Kind::Png));
  ASSERT_TRUE(cache.prepare());
  ASSERT_TRUE(cache.beginCache());
  bool success = false;
  HalFileTest::failNextWrite = true;
  EXPECT_FALSE(cache.finish(success));
  EXPECT_TRUE(success);
  EXPECT_FALSE(Storage.exists(path.c_str()));
}
TEST_F(PixelCacheTest, RejectsCloseFailure) {
  ImagePipeline cache(renderer, config);
  ASSERT_TRUE(cache.init(4, 1, ImagePipeline::Kind::Png));
  ASSERT_TRUE(cache.prepare());
  ASSERT_TRUE(cache.beginCache());
  bool success = false;
  HalFileTest::failNextClose = true;
  EXPECT_FALSE(cache.finish(success));
  EXPECT_TRUE(success);
  EXPECT_FALSE(Storage.exists(path.c_str()));
}
}  // namespace
