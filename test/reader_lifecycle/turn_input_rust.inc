// Original suffix fixture now enters the production Rust loop with other leaves idle.
void EpubReaderActivity::turnInput() {
  const XpUiReaderLoopServices services{
      this,
      [](void* p, uint8_t field) -> uint32_t {
        auto& o = *static_cast<EpubReaderActivity*>(p);
        switch (field) {
          case XP_UI_READER_LOOP_FIELD_APPLIED_ORIENTATION: return SETTINGS.orientation;
          case XP_UI_READER_LOOP_FIELD_LAST_TURN: return o.lastPageTurnTime;
          case XP_UI_READER_LOOP_FIELD_PENDING_TURN: return static_cast<int32_t>(o.pendingManualTurn);
          case XP_UI_READER_LOOP_FIELD_PENDING_TOUCH: return o.pendingManualTurnTouch;
          default: return 0;
        }
      },
      [](void* p, uint8_t field, uint32_t value) {
        auto& o = *static_cast<EpubReaderActivity*>(p);
        switch (field) {
          case XP_UI_READER_LOOP_FIELD_PENDING_TURN: o.pendingManualTurn = static_cast<int8_t>(value); break;
          case XP_UI_READER_LOOP_FIELD_PENDING_TOUCH: o.pendingManualTurnTouch = value; break;
          default: break;
        }
      },
      [](void* p, uint8_t query) -> uint32_t {
        auto& o = *static_cast<EpubReaderActivity*>(p);
        switch (query) {
          case XP_UI_READER_LOOP_QUERY_HAS_EPUB: return 1;
          case XP_UI_READER_LOOP_QUERY_ORIENTATION: return SETTINGS.orientation;
          case XP_UI_READER_LOOP_QUERY_HAS_SECTION: return bool(o.section);
          case XP_UI_READER_LOOP_QUERY_BUSY: return RenderLock::peek();
          case XP_UI_READER_LOOP_QUERY_NOW: return millis();
          case XP_UI_READER_LOOP_QUERY_HELD: return o.mappedInput.getHeldTime();
          case XP_UI_READER_LOOP_QUERY_LONG_BUTTON_BEHAVIOR: return SETTINGS.longPressButtonBehavior;
          case XP_UI_READER_LOOP_QUERY_POWER_RELEASE: return o.mappedInput.wasReleased(MappedInputManager::Button::Power);
          case XP_UI_READER_LOOP_QUERY_DOWN_RELEASE: return o.mappedInput.wasReleased(MappedInputManager::Button::Down);
          default: return 0;
        }
      },
      [](void* p, uint8_t command, int32_t a, int32_t b) -> uint32_t {
        auto& o = *static_cast<EpubReaderActivity*>(p);
        switch (command) {
          case XP_UI_READER_LOOP_COMMAND_TURN: return o.pageTurn(a != 0);
          case XP_UI_READER_LOOP_COMMAND_SKIP: return o.skipPages(a);
          case XP_UI_READER_LOOP_COMMAND_NOTE_TURN: o.notePageTurn(a != 0, b != 0); break;
          case XP_UI_READER_LOOP_COMMAND_END_TURN: return o.handleEndOfBookPageTurn(a != 0, b != 0);
          case XP_UI_READER_LOOP_COMMAND_BUILD_TICK: o.backgroundBuildTick(); break;
          case XP_UI_READER_LOOP_COMMAND_ORIENTATION: o.applyOrientation(a); break;
          case XP_UI_READER_LOOP_COMMAND_HAPTIC: haptic_feedback::touchAction(a != 0); break;
          case XP_UI_READER_LOOP_COMMAND_UPDATE: o.requestUpdate(); break;
          default: break;
        }
        return 0;
      },
      [](void* p) -> XpUiReaderTurnInput {
        auto t = static_cast<EpubReaderActivity*>(p)->touchInput;
        return {static_cast<uint32_t>(t.heldMs), t.prev, t.next, 0, 0};
      },
      [](void* p) -> XpUiReaderTurnInput {
        auto t = ReaderUtils::detectPageTurn(static_cast<EpubReaderActivity*>(p)->mappedInput);
        return {0, t.prev, t.next, t.fromTilt, 0};
      },
      [](void*, uint8_t, int32_t*, int32_t*) -> uint8_t { return 0; }};
  xp_ui_reader_loop_run(&services);
}
