#include <BlePageTurner.h>
#include <gtest/gtest.h>

#include "../reader_lifecycle/build_wait_fixture.h"

namespace {
bleturner::Status radio() { return bleturner::Status{}; }
}  // namespace

TEST(ChapterBuildGate, RunsWithTheRadioOff) {
  EXPECT_TRUE(buildRoomForRadio(radio().running, radio().stopping, radio().starting));
  auto idle = radio();
  idle.idleStopped = true;  // stopped for this build, or for idleness
  EXPECT_TRUE(buildRoomForRadio(idle.running, idle.stopping, idle.starting));
  auto refused = radio();
  refused.readerDeferred = true;  // refused for memory: it holds nothing
  EXPECT_TRUE(buildRoomForRadio(refused.running, refused.stopping, refused.starting));
}

// The radio did not stop within its budget (StillUp), or a start was still settling after it
// (NotHeld): it holds or is taking the heap the build needs.
TEST(ChapterBuildGate, RefusedWhileTheRadioHoldsOrTakesTheHeap) {
  auto up = radio();
  up.running = true;
  up.connected = true;
  EXPECT_FALSE(buildRoomForRadio(up.running, up.stopping, up.starting));
  auto stopping = radio();
  stopping.stopping = true;
  EXPECT_FALSE(buildRoomForRadio(stopping.running, stopping.stopping, stopping.starting));
  auto starting = radio();
  starting.starting = true;
  EXPECT_FALSE(buildRoomForRadio(starting.running, starting.stopping, starting.starting));
}

namespace {
// A clock that only moves when the wait sleeps.
struct FakeClock {
  uint32_t nowMs = 1000;
  unsigned asks = 0;
  bool wait(const uint32_t readyAtMs) {
    const uint32_t started = nowMs;
    return waitForBuildRoom(
        [&] {
          ++asks;
          return nowMs - started >= readyAtMs ? 0u : 1u;
        },
        [&] { return nowMs; }, [&](const uint32_t ms) { nowMs += ms; });
  }
};
}  // namespace

TEST(ChapterBuildGate, ReadyAtOnceDoesNotWait) {
  FakeClock clock;
  EXPECT_TRUE(clock.wait(0));
  EXPECT_EQ(clock.nowMs, 1000u);
  EXPECT_EQ(clock.asks, 1u);
}

// The radio stops a little after the module's own 3 s budget: the build waits for it.
TEST(ChapterBuildGate, RadioThatStopsAfterASecondLetsTheBuildRun) {
  FakeClock clock;
  EXPECT_TRUE(clock.wait(1000));
  EXPECT_GE(clock.nowMs - 1000, 1000u);
  EXPECT_LT(clock.nowMs - 1000, 1050u);
}

TEST(ChapterBuildGate, RadioThatNeverStopsIsRefusedAfterTheWait) {
  FakeClock clock;
  EXPECT_FALSE(clock.wait(UINT32_MAX));
  EXPECT_GE(clock.nowMs - 1000, 5000u);
  EXPECT_LT(clock.nowMs - 1000, 5050u);
}

// The first ask is the module's own stop, which can take seconds: the extra wait starts after it.
TEST(ChapterBuildGate, TheWaitStartsAfterASlowFirstAsk) {
  uint32_t nowMs = 0;
  unsigned asks = 0;
  const bool ok = waitForBuildRoom(
      [&] {
        if (asks++ == 0) nowMs += 3000;
        return 1u;
      },
      [&] { return nowMs; }, [&](const uint32_t ms) { nowMs += ms; });
  EXPECT_FALSE(ok);
  EXPECT_GE(nowMs, 8000u);
  EXPECT_LT(nowMs, 8050u);
}

TEST(ChapterBuildGate, WaitClockWraps) {
  FakeClock clock;
  clock.nowMs = UINT32_MAX - 500;
  const uint32_t start = clock.nowMs;
  EXPECT_TRUE(clock.wait(1000));
  EXPECT_EQ(clock.nowMs - start, 1000u);
  EXPECT_EQ(clock.asks, 21u);

  clock.nowMs = start;
  clock.asks = 0;
  EXPECT_FALSE(clock.wait(UINT32_MAX));
  EXPECT_EQ(clock.nowMs - start, 5000u);
  EXPECT_EQ(clock.asks, 101u);
}
