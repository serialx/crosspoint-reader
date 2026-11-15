#include <Epub/Epub/PageLink.h>
#include <gtest/gtest.h>

#include <cstring>
#include <memory>

TEST(PageLinkTest, HitAreaIncludesFingerSlopAndMinimumWidth) {
  PageLink link;
  link.x = 100;
  link.y = 50;
  link.width = 8;
  link.height = 20;

  EXPECT_TRUE(link.contains(90, 44, 6, 28));
  EXPECT_TRUE(link.contains(117, 75, 6, 28));
  EXPECT_FALSE(link.contains(89, 50, 6, 28));
  EXPECT_FALSE(link.contains(118, 50, 6, 28));
  EXPECT_FALSE(link.contains(100, 76, 6, 28));
}

TEST(PageLinkTest, HrefStorageIsZeroEvenWhenReusingDirtyMemory) {
  alignas(PageLink) unsigned char storage[sizeof(PageLink)];
  std::memset(storage, 0xa5, sizeof(storage));
  auto* link = std::construct_at(reinterpret_cast<PageLink*>(storage));
  for (char byte : link->href) EXPECT_EQ(byte, 0);
  std::destroy_at(link);
}
