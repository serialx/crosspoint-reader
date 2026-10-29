#pragma once

#include <BlePageTurner.h>
#include <xp_ui_reader.h>

struct LinkNoteTitle {
  bleturner::LinkNote draw(const bool titleShown, const bleturner::LinkNote now) {
    return static_cast<bleturner::LinkNote>(xp_ui_reader_link_draw(&state, titleShown, static_cast<uint8_t>(now)));
  }
  bool repaint(const bleturner::LinkNote now) { return xp_ui_reader_link_repaint(&state, static_cast<uint8_t>(now)); }

 private:
  XpUiReaderLinkNote state{};
};
