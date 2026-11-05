int main() {
  using Button = MappedInputManager::Button;
  unsigned cases = 0;
  for (const auto button : {Button::Up, Button::Left, Button::Down, Button::Right}) {
    for (const bool longHold : {false, true}) {
      EpubReaderActivity owner;
      owner.overlay = EpubReaderActivity::Overlay::Contents;
      owner.panelIndex = 20;
      owner.epub->tocCount = 64;
      OverlayInputSessionBridge session(owner);
      auto& input = owner.mappedInput;
      const auto bit = 1u << int(button);
      const int direction = button == Button::Up || button == Button::Left ? -1 : 1;
      input.pressed = input.pressEdges = bit;
      input.held = 0;
      owner.handleOverlayInput();
      assert(owner.panelIndex == 20 && !owner.panelHoldJumped);
      input.pressEdges = 0;
      input.held = 1499;
      owner.handleOverlayInput();
      assert(owner.panelIndex == 20 && !owner.panelHoldJumped);
      if (longHold) {
        for (const unsigned elapsed : {1500u, 1501u, 3000u}) {
          input.held = elapsed;
          owner.handleOverlayInput();
          if (owner.panelIndex != 20 + 10 * direction || !owner.panelHoldJumped) {
            std::fprintf(stderr, "EXPECTED_ASSERTION: OVERLAY_HELD_JUMP\n");
            return 42;
          }
        }
      }
      input.pressed = 0;
      input.released = bit;
      owner.handleOverlayInput();
      assert(owner.panelIndex == 20 + (longHold ? 10 : 1) * direction);
      assert(!owner.panelHoldJumped);
      input.released = 0;
      owner.handleOverlayInput();
      assert(owner.panelIndex == 20 + (longHold ? 10 : 1) * direction);
      ++cases;
    }
  }
  std::printf("PASS overlay frames cases=%u directions=4 thresholds=1499,1500,1501,3000\n", cases);
}
