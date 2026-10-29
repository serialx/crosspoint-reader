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
