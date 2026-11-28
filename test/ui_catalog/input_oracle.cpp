#include <cstdio>
#include <string>
struct MappedInputManager {
  enum class Button { Back, Confirm, NavNext, NavPrevious, PageBack, PageForward };
  unsigned pressed = 0, released = 0, held = 0;
  bool touch = false;
  bool wasPressed(Button b) { return pressed & (1u << unsigned(b)); }
  bool wasReleased(Button b) { return released & (1u << unsigned(b)); }
  bool isPressed(Button b) { return held & (1u << unsigned(b)); }
  bool wasScreenTapped(int&, int&) { return touch; }
};
struct CatalogActivity {
  enum class State { CHECK_WIFI, WIFI_SELECTION, SEARCH_INPUT, LOADING, BROWSING, DOWNLOADING, ERROR, AUTH = 11 };
  State state = State::BROWSING;
  MappedInputManager mappedInput;
  struct {
    int selected = 0;
  } nav;
  bool searchPending = false, searchable = false, connected = true;
  int effect = 0;
  bool hasSearch() { return searchable; }
  bool wifiConnected() { return connected; }
  void onBackButton() { effect = 1; }
  void retryBrowse() { effect = 5; }
  void launchWifiSelection() { effect = 8; }
  void launchSearch() { effect = 4; }
  bool handleCustomInput();
};
struct OpdsBookBrowserActivity : CatalogActivity {
  std::string pageNextHref, pagePrevHref, pageFirstHref, pageLastHref;
  bool handleCustomInput();
  void followPageLink(const std::string& href) { effect = href == pageNextHref ? 10 : 11; }
};
#include "input_original.inc"
int main() {
  for (unsigned state : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 11u})
    for (unsigned flags = 0; flags < 16; ++flags)
      for (unsigned event = 0; event < 20; ++event) {
        OpdsBookBrowserActivity a;
        a.state = static_cast<CatalogActivity::State>(state);
        a.searchable = flags & 1;
        a.nav.selected = flags & 2 ? 1 : 0;
        a.searchPending = flags & 4;
        if (flags & 8) {
          a.pageNextHref = "next";
          a.pageFirstHref = "first";
        }
        if (event < 6)
          a.mappedInput.pressed = 1u << event;
        else if (event < 12)
          a.mappedInput.released = 1u << (event - 6);
        else if (event < 18)
          a.mappedInput.held = 1u << (event - 12);
        else
          a.mappedInput.touch = event == 18;
        bool consumed = a.handleCustomInput();
        std::printf("input %u %u %u %d %d %d\n", state, flags, event, consumed, a.effect, a.searchPending);
      }
}
