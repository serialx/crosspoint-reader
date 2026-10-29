#include <gtest/gtest.h>

#include "ReaderStripBuffers.h"

struct FaultAllocator {
  int* calls;
  int fail;
  template <typename T>
  std::unique_ptr<T[]> operator()(size_t count) const {
    return ++*calls == fail ? nullptr : makeUniqueNoThrow<T[]>(count);
  }
};

TEST(ReaderStripBuffers, EveryReservationFailureReleasesPartialStorageAndCanRetry) {
  for (int fail = 1; fail <= 4; ++fail) {
    ReaderStripBuffers buffers;
    int calls = 0;
    EXPECT_FALSE(buffers.prepare(99, true, FaultAllocator{&calls, fail}));
    EXPECT_FALSE(buffers.scratch);
    EXPECT_FALSE(buffers.commands);
    EXPECT_FALSE(buffers.bounds);
    EXPECT_FALSE(buffers.imageWindow);
    EXPECT_TRUE(buffers.prepare(99, true));
    auto* scratch = buffers.scratch.get();
    calls = 0;
    EXPECT_TRUE(buffers.prepare(99, true, FaultAllocator{&calls, 1}));
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(scratch, buffers.scratch.get());
  }
}

TEST(ReaderStripBuffers, ImageWindowFailureAfterTextAlsoFallsBackWithoutReservations) {
  ReaderStripBuffers buffers;
  ASSERT_TRUE(buffers.prepare(100, false));
  int calls = 0;
  EXPECT_FALSE(buffers.prepare(100, true, FaultAllocator{&calls, 1}));
  EXPECT_EQ(calls, 1);
  EXPECT_FALSE(buffers.scratch);
  EXPECT_FALSE(buffers.commands);
  EXPECT_FALSE(buffers.bounds);
  EXPECT_FALSE(buffers.imageWindow);
}

TEST(ReaderStripBuffers, ActiveSourceLoanBlocksReclamationAndReallocation) {
  ReaderStripBuffers buffers;
  ASSERT_TRUE(buffers.prepare(99, true));
  auto* scratch = buffers.scratch.get();
  buffers.setSourceLoanActive(true);
  EXPECT_FALSE(buffers.releaseIdle());
  EXPECT_FALSE(buffers.prepare(99, false));
  EXPECT_EQ(buffers.scratch.get(), scratch);
  EXPECT_TRUE(buffers.commands);
  EXPECT_TRUE(buffers.bounds);
  EXPECT_TRUE(buffers.imageWindow);
  buffers.setSourceLoanActive(false);
  EXPECT_TRUE(buffers.releaseIdle());
  EXPECT_FALSE(buffers.scratch);
  EXPECT_FALSE(buffers.commands);
  EXPECT_FALSE(buffers.bounds);
  EXPECT_FALSE(buffers.imageWindow);
  EXPECT_FALSE(buffers.releaseIdle());
  int calls = 0;
  ASSERT_TRUE(buffers.prepare(99, false, FaultAllocator{&calls, 0}));
  EXPECT_EQ(calls, 3);
  EXPECT_FALSE(buffers.imageWindow);
  ASSERT_TRUE(buffers.prepare(99, true, FaultAllocator{&calls, 0}));
  EXPECT_EQ(calls, 4);
}
