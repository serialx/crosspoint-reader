#pragma once
#include <GfxRenderer.h>
#include <Logging.h>
#include <MappedInputManager.h>
#include <Memory.h>

#include <string>
#include <variant>
struct FootnoteResult {
  std::string href;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<std::monostate, FootnoteResult> data;
};
struct RenderLock {};
class Activity {
 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

 public:
  ActivityResult result;
  bool finished = false;
  Activity(const char*, GfxRenderer& r, MappedInputManager& m) : renderer(r), mappedInput(m) {}
  virtual ~Activity() = default;
  virtual void onEnter() {}
  virtual void loop() {}
  virtual void render(RenderLock&&) {}
  void requestUpdate() {}
  void setResult(ActivityResult&& value) { result = std::move(value); }
  void finish() { finished = true; }
};
