#pragma once
#include <Epub/Page.h>

#include <memory>
#include <string>
#include <vector>

#include "UtilityActivity.h"
struct DetailsSpec {
  std::string header;    // header band title
  std::string title;     // bold hero title
  std::string author;    // hero line under the title
  std::string source;    // hero line under the author (e.g. repository)
  std::string metadata;  // short facts (version, status, ...)
  // Portrait cover slot: draws the image at coverPath, or the home screen's
  // placeholder when empty. The caller owns (and deletes) the file.
  bool showCover = false;
  std::string coverPath;
  // Availability block under the hero; each line is optional.
  std::string status, copies, holds;
  std::string descriptionHeading;  // optional heading above the description
  std::string description;         // plain text, or HTML when descriptionHtml
  bool descriptionHtml = false;
  std::string actionLabel;  // primary button; confirming finishes with a non-cancelled result
};

class DetailsActivity final : public UtilityActivity {
  DetailsSpec spec;
  XpUiRect descriptionRect{}, laidOutRect{};
  std::vector<std::unique_ptr<Page>> pages;
  const char* utilityText(uint32_t) const override;
  void utilityHints() const override;
  void utilityLayout(XpUiUtilityLayout&) const override;
  bool utilityResourceValid(uint32_t) const override;
  void utilityResource(const XpUiCommand&) override;
  void applyUtilityEffect(const XpUiUtilityEffect&) override;
  bool layoutPending = false;
  bool needsLayout() const;
  void layoutDescription();

 public:
  DetailsActivity(GfxRenderer&, MappedInputManager&, DetailsSpec);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
