#include <gtest/gtest.h>

#include <cstdlib>

#include "util/HomeButtonInput.h"

TEST(HomeButtonInput, RecognizesConfiguredGestures) {
  static_assert(static_cast<uint8_t>(HomeButtonAction::ToggleFrontlight) == 10);
  // All ordered gesture assertions now exercise the Rust owner.
  ASSERT_EQ(std::system("cargo test --offline --locked --manifest-path \"" XP_HOME_MANIFEST
                        "\" -p xp-ui app::input::tests::native_home_gesture_sequence -- --exact"),
            0);
}
