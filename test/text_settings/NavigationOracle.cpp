// Frozen tab-ring behavior from dc21f50f; button timing matches ButtonNavigator.
#include <cstdio>
#include <cstdint>
#include <array>
struct MappedInputManager {
 enum class Button { NavNext=1, NavPrevious=2, Back=4, Confirm=8 };
 uint32_t pressed=0, released=0, held=0, now=0, duration=0;
 bool wasPressed(Button b) const { return pressed & int(b); }
 bool wasReleased(Button b) const { return released & int(b); }
};
struct Nav { int selected=0; bool followOnBuild=false; void requestSelection(int n) { selected=n; } };
struct Buttons {
 MappedInputManager& input; uint32_t last=0;
 static int nextIndex(int n,int count) { return (n+1)%count; }
 static int previousIndex(int n,int count) { return (n+count-1)%count; }
 template<class F> void onNextPress(F f) { if(input.pressed&1) f(); }
 template<class F> void onPreviousPress(F f) { if(input.pressed&2) f(); }
 template<class F> void continuous(int mask,F f) { if((input.pressed|input.released)&mask) last=0; else if((input.held&mask)&&input.duration>500&&uint32_t(input.now-last)>500) { f();last=input.now; } }
 template<class F> void onNextContinuous(F f) { continuous(1,f); }
 template<class F> void onPreviousContinuous(F f) { continuous(2,f); }
};
using ButtonNavigator=Buttons;
struct TextSettingsActivity {
 enum class Tab {Family,Size,Layout,Style,Count};
 MappedInputManager mappedInput; Buttons buttonNavigator{mappedInput};
 bool navigationStartedOnTabs=false; Tab tab_=Tab::Family;
 std::array<Nav,4> tabNavs{}; int action=-1, repaint=0;
 int listCount() { static const int counts[]={3,5,7,4};return counts[int(tab_)]; }
 Nav& activeNav() {return tabNavs[int(tab_)];}
 int ringPos() {return activeNav().selected;}
 void requestUpdate(){repaint=1;}
 void rebuildRowItems(){}
 void activateRow(int i){action=i;}
 void finish(){action=-2;}
 void stepTab(int d){switchTab(d);}
 void moveRingTo(int);void navigateButtons();void switchTab(int direction=1);bool handleButtons();
};
void TextSettingsActivity::moveRingTo(const int ringIndex) {
#if FREEINK_CAP_TOUCH
  closeSwipeDelete();
#endif
  activeNav().requestSelection(ringIndex);
  requestUpdate();
}
void TextSettingsActivity::navigateButtons() {
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
      mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    navigationStartedOnTabs = ringPos() == 0;
  }
  // Buttons walk the tab band (index 0) plus the rows (1..listCount).
  const int ringSize = listCount() + 1;
  buttonNavigator.onNextPress([this, ringSize] { moveRingTo(ButtonNavigator::nextIndex(ringPos(), ringSize)); });
  buttonNavigator.onPreviousPress(
      [this, ringSize] { moveRingTo(ButtonNavigator::previousIndex(ringPos(), ringSize)); });
  buttonNavigator.onNextContinuous([this] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
    } else if (ringPos() == 0 && listCount() > 0) {
      activeNav().selected = 1;
    }
    stepTab(1);
  });
  buttonNavigator.onPreviousContinuous([this] {
    if (navigationStartedOnTabs) {
      activeNav().selected = 0;
    } else if (ringPos() == 0 && listCount() > 0) {
      activeNav().selected = 1;
    }
    stepTab(-1);
  });
}
void TextSettingsActivity::switchTab(const int direction) {
  const bool onTabBar = ringPos() == 0;
  constexpr int count = static_cast<int>(Tab::Count);
  tab_ = static_cast<Tab>((static_cast<int>(tab_) + direction + count) % count);
  rebuildRowItems();
  auto& n = activeNav();
  if (onTabBar) n.selected = 0;
  n.followOnBuild = true;  // pull the new tab's viewport to its remembered selection
  requestUpdate();
}
bool TextSettingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      switchTab();
    } else {
      activateRow(ringPos() - 1);
    }
    return true;
  }

  return false;
}
int main() {
 struct Event { uint32_t p,r,h,t,d; };
 const Event events[]={ {1,0,1,100,0},{0,1,0,150,50},{2,0,2,200,0},{0,2,0,250,50},
 {0,8,0,300,0},{1,0,1,400,0},{0,0,1,901,501},{0,0,1,1401,1001},{0,0,1,1402,1002},
 {0,1,0,1450,1050},{2,0,2,1600,0},{0,0,2,2101,501},{0,2,0,2150,550},{0,8,0,2200,0},{0,4,0,2300,0}};
 for(int tab=0;tab<4;++tab) for(int ring=0;ring<5;++ring) {
 TextSettingsActivity s; s.tab_=static_cast<TextSettingsActivity::Tab>(tab);
 for(auto& n:s.tabNavs)n.selected=1;s.activeNav().selected=ring;
 for(const auto& e:events) {s.mappedInput={e.p,e.r,e.h,e.t,e.d};s.action=-1;s.repaint=0;
 if(!s.handleButtons()) s.navigateButtons();
 std::printf("%d %d %d %d %d %d %d %d %u\n",int(s.tab_),s.tabNavs[0].selected,s.tabNavs[1].selected,s.tabNavs[2].selected,s.tabNavs[3].selected,s.action,s.repaint,int(s.navigationStartedOnTabs),s.buttonNavigator.last);
 }
 }
}
