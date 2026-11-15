// A build without BLE links NoRadio.cpp: every call answers, nothing ever starts.

#include <gtest/gtest.h>

#include "BlePageTurner.h"
#include "BleRust.h"
#include "Runtime.h"

TEST(BlePageTurnerNoRadioTest, NothingStartsAndEveryCallAnswers) {
  bleturner::detail::resetForTests();
  // Hosts without the radio may never call begin(): the module answers with its defaults.
  EXPECT_FALSE(bleturner::switchOn());
  EXPECT_TRUE(bleturner::rust::xp_ble_before_screen(0, false));
  EXPECT_TRUE(bleturner::beforeSleep(100));
  EXPECT_FALSE(bleturner::holdsHeap());
  EXPECT_EQ(bleturner::beforeChapterBuild(), bleturner::BuildRelease::NotHeld);
  const bleturner::Status st = bleturner::status();
  EXPECT_FALSE(st.compiledIn);
  EXPECT_FALSE(st.running || st.starting || st.stopping);
}
