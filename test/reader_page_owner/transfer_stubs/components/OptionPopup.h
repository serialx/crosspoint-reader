#pragma once
#include <cassert>
// No popup is opened during construction or destruction.
class OptionPopup {
 public:
  bool isActive() const {
    assert(false);
    return false;
  }
  template <class F>
  void handleButtons(bool, bool, bool, bool, F) {
    assert(false);
  }
};
