#include "ReaderSession.h"

void ReaderSession::noteTurn(const bool forward, const bool succeeded) {
  xp_ui_reader_session_note(&state, forward, succeeded);
}

void ReaderSession::onRenderComplete(const uint32_t monotonicMs, const int64_t trustedEpoch, const int progressBp) {
  xp_ui_reader_session_render(&state, monotonicMs, trustedEpoch, progressBp);
}

void ReaderSession::reset() { state = {}; }
