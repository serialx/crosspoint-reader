#pragma once
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <xp_ui_keyboard.h>

#include "activities/Activity.h"

enum class InputType { Text, Password, Url };

// Constructor compatibility and physical services; the App owns the Rust keyboard.
class KeyboardEntryActivity : public Activity {
 public:
  explicit KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string title = "Enter Text", std::string initialText = "",
                                 const size_t maxLength = 0, InputType inputType = InputType::Text);
  ~KeyboardEntryActivity() override { vSemaphoreDelete(textMutex); }
  bool preventAutoSleep() override { return true; }
  XpAppKeyboardLaunch launch() const {
    return {{reinterpret_cast<const uint8_t*>(title.data()), title.size()},
            {reinterpret_cast<const uint8_t*>(initialText.data()), initialText.size()},
            maxLength,
            static_cast<uint8_t>(inputType)};
  }
  void releaseLaunch() {
    std::string().swap(initialText);
    std::string().swap(title);
  }
  void textLock(bool acquire) {
    if (acquire)
      xSemaphoreTake(textMutex, portMAX_DELAY);
    else
      xSemaphoreGive(textMutex);
  }
  static XpUiKeyboardLayouts layouts();
  XpUiKeyboardMetrics metrics() const;
  XpUiKeyboardViewLayout viewLayout() const;
  void paintKey(const XpUiCommand&, const XpUiKeyboardPaintKey&, uint8_t layout, bool symbols, bool modeAbc);
  void paintChrome(const XpUiCommand&, XpUiRect clip, XpAppText title);

 private:
  std::string title, initialText;
  size_t maxLength;
  InputType inputType;
  StaticSemaphore_t textMutexStorage{};
  SemaphoreHandle_t textMutex = xSemaphoreCreateMutexStatic(&textMutexStorage);
};
