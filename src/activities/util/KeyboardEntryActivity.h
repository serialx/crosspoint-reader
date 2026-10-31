#pragma once
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <xp_ui_keyboard.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "activities/RustActivityAdapter.h"

enum class InputType { Text, Password, Url };

class KeyboardEntryActivity : public RustActivityAdapter {
 public:
  explicit KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string title = "Enter Text", std::string initialText = "",
                                 const size_t maxLength = 0, InputType inputType = InputType::Text);
  ~KeyboardEntryActivity() override { vSemaphoreDelete(textMutex); }
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return true; }

 private:
  struct Bytes {
    std::unique_ptr<uint8_t[]> data;
    uint32_t capacity = 0;
    bool grow(uint32_t required, uint32_t used);
  };
  struct Arena {
    std::unique_ptr<XpUiCommand[]> commands;
    uint16_t commandCapacity = 0;
    std::unique_ptr<XpUiKeyboardTextSpan[]> spans;
    uint16_t spanCapacity = 0;
    XpUiKeyboardPlacedKey rowKeys[16];
    Bytes sceneText;
    XpUiKeyboardState snapshot{};
    Bytes raw, display, paintText;
  };
  std::string title, initialText;
  size_t maxLength;
  InputType inputType;
  std::unique_ptr<Arena> arena;
  // Loop owns edits; render borrows only while copying, never during paint.
  StaticSemaphore_t textMutexStorage{};
  SemaphoreHandle_t textMutex = xSemaphoreCreateMutexStatic(&textMutexStorage);
  class TextLock {
    SemaphoreHandle_t mutex;

   public:
    explicit TextLock(KeyboardEntryActivity& owner) : mutex(owner.textMutex) { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~TextLock() { xSemaphoreGive(mutex); }
    TextLock(const TextLock&) = delete;
    TextLock& operator=(const TextLock&) = delete;
  };
  Bytes text;
  XpUiInterest inputInterest{};
  XpUiKeyboardState previousPublished{};
  bool publicationPending = false, resetInput = false;
  void reconcileInput();
  XpUiKeyboardState state{}, beforeInput{};
  XpUiKeyboardViewLayout layout{};
  XpUiKeyboardEffect pending{};
  XpUiInput lastInput{};
  Publication publication;
  uint32_t tappedCursor = UINT32_MAX;
  XpUiKeyboardState publishedState{};
  XpUiKeyboardViewLayout publishedLayout{};

  static XpUiKeyboardLayouts layouts();
  XpUiKeyboardMetrics metrics() const;
  XpUiKeyboardViewLayout viewLayout() const;
  Publication* rustPublication() override { return &publication; }
  bool buildRustScene(XpUiScene&) override;
  void rustPublished() override;
  void rustSceneFailed() override { resetInput = true; }
  XpUiPaint rustResources() override;
  XpUiInterest rustInterest(uint32_t) const override { return inputInterest; }
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
  void paintKey(const XpUiCommand&);
  void paintRow(const XpUiCommand&);
  void paintText(const XpUiCommand&, XpUiRect);
  bool validText(uint32_t) const;
};
