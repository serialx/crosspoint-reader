#pragma once

#include <xp_ui_reader.h>

class ReaderSession {
 public:
  void noteTurn(bool forward, bool succeeded);
  void onRenderComplete(uint32_t monotonicMs, int64_t trustedEpoch, int progressBp);
  void reset();

  bool isEmitWorthy() const { return xp_ui_reader_session_worthy(&state); }
  int64_t startTime() const { return state.start_epoch; }
  int64_t endTime() const { return state.start_epoch > 0 ? state.start_epoch + state.duration : 0; }
  uint32_t durationSeconds() const { return state.duration; }
  uint16_t startProgressBp() const { return state.start_progress; }
  uint16_t endProgressBp() const { return state.end_progress; }

 private:
  XpUiReaderSession state{};
};
