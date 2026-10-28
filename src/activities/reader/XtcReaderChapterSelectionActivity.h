#pragma once
#include <Xtc.h>

#include <memory>

#include "activities/RustActivityAdapter.h"

class XtcReaderChapterSelectionActivity final : public RustActivityAdapter {
  // One fallible allocation per visit. Chapter strings stay in the book.
  struct Arena {
    XpUiCommand commands[80];
    XpUiInteraction hits[32];
    const char* hints[4]{};
    XpUiListState nextState{};
  };
  static_assert(sizeof(Arena) <= 4096);
  std::shared_ptr<Xtc> xtc;
  uint32_t currentPage;
  std::unique_ptr<Arena> arena;
  XpUiListState state{};
  Publication publication;

  const char* text(uint32_t handle) const;
  XpUiResources resources();
  Publication* rustPublication() override { return &publication; }
  void rustPublished() override { state = arena->nextState; }
  XpUiPaint rustResources() override;
  bool buildRustScene(XpUiScene& scene) override;
  XpUiEffect updateRust(const XpUiInput& input) override;
  void applyRustEffect(const XpUiEffect& effect) override;

 public:
  explicit XtcReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const std::shared_ptr<Xtc>& xtc, uint32_t currentPage);
  void onEnter() override;
  void onExit() override;
};
