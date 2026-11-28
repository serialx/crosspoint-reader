int main() {
  using B = MappedInputManager::Button;
  unsigned holdCases = 0, swipeCases = 0;
  for (unsigned orientation = 0; orientation < 4; ++orientation) {
    for (bool remap : {false, true}) {
      SETTINGS.frontButtonBack = remap ? 2 : 0;
      SETTINGS.frontButtonConfirm = remap ? 3 : 1;
      SETTINGS.frontButtonLeft = remap ? 0 : 2;
      SETTINGS.frontButtonRight = remap ? 1 : 3;
      for (const auto button : {B::Up, B::Left, B::Down, B::Right}) {
        for (bool longHold : {false, true}) {
          EpubReaderActivity owner;
          owner.overlay = EpubReaderActivity::Overlay::Contents;
          owner.panelIndex = 20;
          owner.epub->tocCount = 64;
          auto& input = owner.mappedInput;
          input.renderer.orientation = static_cast<GfxRenderer::Orientation>(orientation);
          const unsigned physical = button == B::Up     ? HalGPIO::BTN_UP
                                    : button == B::Down ? HalGPIO::BTN_DOWN
                                    : button == B::Left ? SETTINGS.frontButtonLeft
                                                        : SETTINGS.frontButtonRight;
          const int direction = button == B::Up || button == B::Left ? -1 : 1;
          const unsigned bit = 1u << physical;
          OverlayInputSessionBridge session(owner);
          input.gpio.pressed = input.gpio.pressEdges = bit;
          owner.handleOverlayInput();
          assert(owner.panelIndex == 20 && !owner.panelHoldJumped);
          input.gpio.pressEdges = 0;
          input.gpio.held = 1499;
          owner.handleOverlayInput();
          assert(owner.panelIndex == 20 && !owner.panelHoldJumped);
          if (longHold) {
            for (unsigned elapsed : {1500u, 1501u, 3000u}) {
              input.gpio.held = elapsed;
              owner.handleOverlayInput();
              if (owner.panelIndex != 20 + direction * 10 || !owner.panelHoldJumped) {
                std::fprintf(stderr, "EXPECTED_ASSERTION: OVERLAY_PHYSICAL_HELD\n");
                return 42;
              }
            }
          }
          input.gpio.pressed = 0;
          input.gpio.released = bit;
          owner.handleOverlayInput();
          assert(owner.panelIndex == 20 + direction * (longHold ? 10 : 1) && !owner.panelHoldJumped);
          input.gpio.released = 0;
          owner.handleOverlayInput();
          assert(owner.panelIndex == 20 + direction * (longHold ? 10 : 1));
          ++holdCases;
        }
      }
    }
    // Expected logical directions for physical None, Left, Right, Up, Down.
    using S = MappedInputManager::SwipeDir;
    constexpr S expected[4][5] = {{S::None, S::Up, S::Down, S::Right, S::Left},
                                  {S::None, S::Right, S::Left, S::Down, S::Up},
                                  {S::None, S::Down, S::Up, S::Left, S::Right},
                                  {S::None, S::Left, S::Right, S::Up, S::Down}};
    for (unsigned direction = 0; direction < 5; ++direction) {
      EpubReaderActivity owner;
      owner.overlay = EpubReaderActivity::Overlay::Contents;
      owner.panelIndex = 20;
      owner.epub->tocCount = 64;
      owner.panelCursorShown = true;
      owner.toolbarUi->rows = 3;
      auto& input = owner.mappedInput;
      input.renderer.orientation = static_cast<GfxRenderer::Orientation>(orientation);
      input.gpio.swipePresent = direction != 0;
      input.gpio.ex += direction == 1 ? -.25f : direction == 2 ? .25f : 0;
      input.gpio.ey += direction == 3 ? -.25f : direction == 4 ? .25f : 0;
      assert(input.wasSwipe() == expected[orientation][direction]);
      OverlayInputSessionBridge session(owner);
      owner.handleOverlayInput();
      const auto logical = expected[orientation][direction];
      const int delta = logical == S::Up ? 3 : logical == S::Down ? -3 : 0;
      if (owner.panelIndex != 20 + delta) {
        std::fprintf(stderr, "EXPECTED_ASSERTION: OVERLAY_PHYSICAL_SWIPE\n");
        return 42;
      }
      ++swipeCases;
    }
  }
  std::printf("PASS physical overlay holds=%u swipes=%u orientations=4 mappings=2\n", holdCases, swipeCases);
}
