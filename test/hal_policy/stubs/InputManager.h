#pragma once
#include <Arduino.h>
inline bool physical = false, debounce = false;
inline int adc1 = 4095, adc2 = 4095;
inline uint8_t pageMask = 0;
struct InputManager {
 struct ButtonAdcSample { int raw; };
 void begin() { puts("input begin"); }
 void update() { puts("input update"); }
 bool prepareForDeepSleep() { puts("input sleep"); return true; }
 uint8_t capacitivePageButtonMask() { return pageMask; }
 bool isPowerButtonPhysicallyPressed() { puts("physical"); return physical; }
 bool isDebouncePending() { puts("debounce"); return debounce; }
 void readButtonAdc(ButtonAdcSample& a, ButtonAdcSample& b) { puts("adc"); a.raw=adc1; b.raw=adc2; }
 static void setSharedConfirmPowerShortPressEmitsPower(bool) {}
 void suppressTouchContact() {}
 template<typename... T> bool isPressed(T&&...) const { return false; }
 template<typename... T> bool wasPressed(T&&...) const { return false; }
 template<typename... T> bool wasAnyPressed(T&&...) const { return false; }
 template<typename... T> bool wasReleased(T&&...) const { return false; }
 template<typename... T> bool wasAnyReleased(T&&...) const { return false; }
 template<typename... T> bool hasTouch(T&&...) const { return false; }
 template<typename... T> bool hasToggleSwitch(T&&...) const { return false; }
 template<typename... T> bool isToggleSwitchOn(T&&...) const { return false; }
 template<typename... T> bool wasHomeKeyPressed(T&&...) const { return false; }
 template<typename... T> bool wasHomeKeyTapped(T&&...) const { return false; }
 template<typename... T> bool wasHomeKeyLongPressed(T&&...) const { return false; }
 template<typename... T> bool wasTouchTap(T&&...) const { return false; }
 template<typename... T> bool wasTouchPressedAt(T&&...) const { return false; }
 template<typename... T> bool wasTouchReleased(T&&...) const { return false; }
 template<typename... T> bool isTouchTapCandidate(T&&...) const { return false; }
 template<typename... T> bool isTouchHeldAt(T&&...) const { return false; }
 template<typename... T> bool wasTouchLongPress(T&&...) const { return false; }
 template<typename... T> bool wasSwipe(T&&...) const { return false; }
 template<typename... T> bool wasTouchActivity(T&&...) const { return false; }
 unsigned long getHeldTime() const { return 0; }
 unsigned long getPowerButtonHeldTime() const { return 0; }
 unsigned long lastTouchHeldMs() const { return 0; }
};
