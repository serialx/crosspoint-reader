#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
extern "C" uint8_t xp_chapter_test_storage_retry(void (*)());
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();
extern "C" size_t xp_test_allocation_attempts();
namespace {
size_t reclaims = 0;
bool recoverAllocation = false;
void reclaim() {
  ++reclaims;
  if (recoverAllocation) xp_test_fail_allocation_after(SIZE_MAX);
}
}  // namespace
TEST(ChapterStorageRetry, ReclaimsOnceForSlotAndPayloadGrowth) {
  for (size_t fail : {0u, 1u}) {
    for (bool recover : {false, true}) {
      reclaims = 0;
      recoverAllocation = recover;
      xp_test_fail_allocation_after(fail);
      EXPECT_EQ(bool(xp_chapter_test_storage_retry(reclaim)), recover);
      EXPECT_EQ(reclaims, 1u);
      if (!recover) EXPECT_EQ(xp_test_allocation_attempts(), fail + 2);
      EXPECT_EQ(xp_test_live_allocations(), 0u);
      xp_test_fail_allocation_after(SIZE_MAX);
    }
  }
}
