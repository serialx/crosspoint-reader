#include <HalStorage.h>
#include <SdCardFontRegistry.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <new>
#include <string>

namespace {
size_t failNextArraySize = 0;
}  // namespace

// Fail one nothrow array allocation of exactly failNextArraySize bytes; the
// registry copies a non-canonical filename through makeUniqueNoThrow<char[]>.
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  if (failNextArraySize != 0 && size == failNextArraySize) {
    failNextArraySize = 0;
    return nullptr;
  }
  return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](size_t size) {
  void* allocation = std::malloc(size == 0 ? 1 : size);
  if (!allocation) std::abort();
  return allocation;
}
void operator delete[](void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation, size_t) noexcept { std::free(allocation); }
void operator delete[](void* allocation, const std::nothrow_t&) noexcept { std::free(allocation); }

namespace {

std::string pathOf(const SdCardFontFamilyInfo& family, const SdCardFontFileInfo& file) {
  char out[SdCardFontFamilyInfo::PATH_BUFFER_SIZE];
  EXPECT_TRUE(family.filePath(file, out, sizeof(out)));
  return out;
}

class SdFontRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override { fake::reset(); }
  void TearDown() override { fake::reset(); }
  SdCardFontRegistry registry;
};

TEST_F(SdFontRegistryTest, CanonicalNamesStoreNoFileString) {
  fake::add("/.fonts/Bitter/Bitter_12.cpfont");
  fake::add("/.fonts/Bitter/Bitter_14.cpfont");

  ASSERT_TRUE(registry.discover());
  const auto* family = registry.findFamily("Bitter");
  ASSERT_NE(family, nullptr);
  EXPECT_STREQ(family->root, SdCardFontRegistry::FONTS_DIR_HIDDEN);
  ASSERT_EQ(family->files.size(), 2u);
  for (const auto& file : family->files) EXPECT_EQ(file.name, nullptr);

  const auto* file14 = family->findFile(14);
  ASSERT_NE(file14, nullptr);
  EXPECT_EQ(pathOf(*family, *file14), "/.fonts/Bitter/Bitter_14.cpfont");
  EXPECT_EQ(pathOf(*family, *family->findFile(12)), "/.fonts/Bitter/Bitter_12.cpfont");
}

TEST_F(SdFontRegistryTest, OtherStemsAndZeroPaddedSizesKeepTheirFilename) {
  fake::add("/fonts/Bookerly/Bookerly-SD_14.cpfont");
  fake::add("/fonts/Bookerly/Bookerly_016.cpfont");

  ASSERT_TRUE(registry.discover());
  const auto* family = registry.findFamily("Bookerly");
  ASSERT_NE(family, nullptr);
  EXPECT_STREQ(family->root, SdCardFontRegistry::FONTS_DIR_VISIBLE);

  const auto* file14 = family->findFile(14);
  ASSERT_NE(file14, nullptr);
  EXPECT_STREQ(file14->name.get(), "Bookerly-SD_14.cpfont");
  EXPECT_EQ(pathOf(*family, *file14), "/fonts/Bookerly/Bookerly-SD_14.cpfont");

  const auto* file16 = family->findFile(16);
  ASSERT_NE(file16, nullptr);
  EXPECT_STREQ(file16->name.get(), "Bookerly_016.cpfont");
  EXPECT_EQ(pathOf(*family, *file16), "/fonts/Bookerly/Bookerly_016.cpfont");
}

TEST_F(SdFontRegistryTest, HiddenRootWinsAndKeepsItsOwnRoot) {
  fake::add("/.fonts/Lora/Lora_14.cpfont");
  fake::add("/fonts/Lora/Lora_18.cpfont");
  fake::add("/fonts/Inter/Inter_14.cpfont");

  ASSERT_TRUE(registry.discover());
  ASSERT_EQ(registry.getFamilyCount(), 2);
  const auto* lora = registry.findFamily("Lora");
  ASSERT_NE(lora, nullptr);
  EXPECT_STREQ(lora->root, SdCardFontRegistry::FONTS_DIR_HIDDEN);
  EXPECT_FALSE(lora->hasSize(18));
  const auto* inter = registry.findFamily("Inter");
  ASSERT_NE(inter, nullptr);
  EXPECT_EQ(pathOf(*inter, inter->files[0]), "/fonts/Inter/Inter_14.cpfont");
}

TEST_F(SdFontRegistryTest, OutOfMemoryNameCopySkipsOnlyThatFile) {
  fake::add("/.fonts/Bookerly/Bookerly-SD_14.cpfont");
  fake::add("/.fonts/Bookerly/Bookerly_16.cpfont");

  failNextArraySize = sizeof("Bookerly-SD_14.cpfont");
  ASSERT_TRUE(registry.discover());
  EXPECT_EQ(failNextArraySize, 0u);
  const auto* family = registry.findFamily("Bookerly");
  ASSERT_NE(family, nullptr);
  EXPECT_EQ(family->findFile(14), nullptr);
  ASSERT_NE(family->findFile(16), nullptr);
  EXPECT_EQ(pathOf(*family, *family->findFile(16)), "/.fonts/Bookerly/Bookerly_16.cpfont");
}

TEST_F(SdFontRegistryTest, OverlongPathsAreSkippedAtDiscovery) {
  // "/.fonts/" + 100 + "/" + 100 + "_14.cpfont" = 219 bytes: over PATH_BUFFER_SIZE.
  const std::string longName(100, 'L');
  fake::add("/.fonts/" + longName + "/" + longName + "_14.cpfont");
  fake::add("/.fonts/" + longName + "/Short_16.cpfont");  // 124 bytes: fits
  // The 120-byte filename fits the 128-byte name buffer, but the 239-byte path
  // does not fit PATH_BUFFER_SIZE.
  const std::string onlyLong(110, 'O');
  fake::add("/.fonts/" + onlyLong + "/" + onlyLong + "_12.cpfont");

  ASSERT_TRUE(registry.discover());
  EXPECT_EQ(registry.findFamily(onlyLong), nullptr);  // no loadable file left
  const auto* family = registry.findFamily(longName);
  ASSERT_NE(family, nullptr);
  ASSERT_EQ(family->files.size(), 1u);
  EXPECT_EQ(family->findFile(14), nullptr);
  const auto* nearest = family->findNearestSize(14);
  ASSERT_NE(nearest, nullptr);
  EXPECT_EQ(nearest->pointSize, 16);
  EXPECT_EQ(pathOf(*family, *nearest), "/.fonts/" + longName + "/Short_16.cpfont");
}

TEST_F(SdFontRegistryTest, FilePathReportsTruncation) {
  fake::add("/.fonts/Bitter/Bitter_14.cpfont");
  ASSERT_TRUE(registry.discover());
  const auto* family = registry.findFamily("Bitter");
  ASSERT_NE(family, nullptr);

  char tiny[16];
  EXPECT_FALSE(family->filePath(family->files[0], tiny, sizeof(tiny)));
  EXPECT_EQ(tiny[sizeof(tiny) - 1], '\0');
}

}  // namespace

TEST(SdFontRegistryBoundaries, FamilyCapKeepsSortedPrefixAfterBothRoots) {
  for (int count : {127, 128, 129}) {
    fake::reset();
    for (int i = count - 1; i >= 0; --i) {
      char path[80];
      snprintf(path, sizeof(path), "%s/F%03d/F%03d_14.cpfont", i % 2 ? "/fonts" : "/.fonts", i, i);
      fake::add(path);
    }
    SdCardFontRegistry registry;
    ASSERT_TRUE(registry.discover());
    ASSERT_EQ(registry.getFamilyCount(), std::min(count, SdCardFontRegistry::MAX_SD_FAMILIES));
    for (int i = 0; i < registry.getFamilyCount(); ++i) {
      char name[16];
      snprintf(name, sizeof(name), "F%03d", i);
      EXPECT_EQ(registry.getFamilies()[i].name, name);
      EXPECT_NE(registry.findFamily(name), nullptr);
    }
    EXPECT_EQ(registry.findFamily("F128"), nullptr);
    fake::reset();
    ASSERT_FALSE(registry.discover());
    EXPECT_EQ(registry.getFamilyCount(), 0);
  }
}

TEST(SdFontRegistryBoundaries, SizeEndpointsAndNearestTies) {
  SdCardFontFamilyInfo family;
  for (uint8_t size : {255, 0, 2, 254}) family.files.push_back({nullptr, size, 0});
  family.files.push_back({nullptr, 255, 1});
  EXPECT_TRUE(family.hasSize(0));
  EXPECT_TRUE(family.hasSize(255));
  EXPECT_FALSE(family.hasSize(253));
  EXPECT_EQ(family.availableSizes(), (std::vector<uint8_t>{0, 2, 254, 255}));
  EXPECT_EQ(family.findNearestSize(1)->pointSize, 0);
  EXPECT_EQ(family.findNearestSize(253)->pointSize, 254);
  EXPECT_EQ(family.findFile(255, 1), &family.files[4]);
  EXPECT_EQ(family.findFile(255, 2), nullptr);
}
