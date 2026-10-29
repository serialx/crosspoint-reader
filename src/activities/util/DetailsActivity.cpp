#include "DetailsActivity.h"

#include <Epub/converters/ImageDecoderFactory.h>
#include <FontCacheManager.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "activities/reader/DictionaryDefinitionActivity.h"
#include "components/UITheme.h"
#include "util/DictHtmlPages.h"
#include "util/HtmlToPlainText.h"
namespace {
constexpr char DETAILS_HEADING_MARKER = '\x01';
}
DetailsActivity::DetailsActivity(GfxRenderer& renderer, MappedInputManager& input, DetailsSpec spec)
    : UtilityActivity("Details", renderer, input, XP_UI_UTILITY_DETAILS, 23, 2), spec(std::move(spec)) {}
void DetailsActivity::onEnter() {
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  setResult(std::move(cancelled));
  UtilityActivity::onEnter();
}
bool DetailsActivity::needsLayout() const {
  return spec.descriptionHtml && descriptionRect.w > 0 && descriptionRect.h > 0 &&
         (pages.empty() || laidOutRect.w != descriptionRect.w || laidOutRect.h != descriptionRect.h);
}
void DetailsActivity::layoutDescription() {
  pages.clear();
  laidOutRect = descriptionRect;
  if (!buildDictionaryHtmlPages(renderer, spec.description, descriptionRect.w, descriptionRect.h, pages) ||
      pages.empty()) {
    LOG_INF("DETAILS", "Styled description unavailable; using plain text");
    pages.clear();
    spec.description = htmlToPlainText(spec.description, DETAILS_HEADING_MARKER);
    std::replace(spec.description.begin(), spec.description.end(), '\n', ' ');
    std::replace(spec.description.begin(), spec.description.end(), DETAILS_HEADING_MARKER, ' ');
    spec.descriptionHtml = false;
    return;
  }
  pages.resize(1);
}
void DetailsActivity::loop() {
  bool layout;
  {
    ModelLock lock(*this);
    layout = layoutPending;
  }
  if (layout) {
    {
      RenderLock lock;
      layoutDescription();
      ModelLock model(*this);
      layoutPending = false;
    }
    requestUpdate();
    return;
  }
  UtilityActivity::loop();
}
void DetailsActivity::render(RenderLock&&) {
  XpUiScene scene{};
  if (!buildRustScene(scene)) {
    {
      ModelLock lock(*this);
      publication.ready = false;
    }
    LOG_ERR("RUI", "Cannot build details scene");
    return;
  }
  descriptionRect = buildingFeedback.description;
  const bool pending = needsLayout();
  {
    ModelLock lock(*this);
    layoutPending = pending;
  }
  if (pending) return;
  paintRustScene(scene);
}
const char* DetailsActivity::utilityText(const uint32_t h) const {
  switch (h) {
    case Title:
      return spec.title.c_str();
    case Body:
      return spec.header.c_str();
    case Author:
      return spec.author.c_str();
    case Source:
      return spec.source.c_str();
    case Metadata:
      return spec.metadata.c_str();
    case Status:
      return spec.status.c_str();
    case Copies:
      return spec.copies.c_str();
    case Holds:
      return spec.holds.c_str();
    case DescriptionHeading:
      return spec.descriptionHeading.c_str();
    case Description:
      return spec.description.c_str();
    case ActionLabel:
      return spec.actionLabel.c_str();
    default:
      return UtilityActivity::utilityText(h);
  }
}
void DetailsActivity::utilityLayout(XpUiUtilityLayout& l) const {
  l.show_cover = spec.showCover;
  l.html_description = spec.descriptionHtml;
}
void DetailsActivity::utilityHints() const {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), spec.actionLabel.c_str(), "",
                                            spec.description.empty() ? "" : tr(STR_DETAILS));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
bool DetailsActivity::utilityResourceValid(const uint32_t h) const {
  return h == Cover || h == Html || UtilityActivity::utilityResourceValid(h);
}
void DetailsActivity::utilityResource(const XpUiCommand& c) {
  const Rect r{c.rect.x, c.rect.y, c.rect.w, c.rect.h};
  switch (c.resource) {
    case Header:
      GUI.drawHeader(renderer, r, spec.header.c_str(), nullptr);
      break;
    case Cover:
      if (spec.coverPath.empty())
        GUI.drawCoverPlaceholder(renderer, r);
      else if (auto* decoder = ImageDecoderFactory::getDecoder(spec.coverPath)) {
        RenderConfig cfg{r.x, r.y, r.width, r.height};
        decoder->decodeToFramebuffer(spec.coverPath, renderer, cfg);
      }
      break;
    case Html:
      if (!pages.empty()) {
        auto scope = renderer.getFontCacheManager()->createPrewarmScope();
        pages[0]->render(renderer, SETTINGS.getReaderFontId(), r.x, r.y);
        scope.endScanAndPrewarm();
        pages[0]->render(renderer, SETTINGS.getReaderFontId(), r.x, r.y);
      }
      break;
    default:
      UtilityActivity::utilityResource(c);
      break;
  }
}
void DetailsActivity::applyUtilityEffect(const XpUiUtilityEffect& e) {
  if (e.navigation.kind == XP_UI_ACTIVATE && e.navigation.value == 2) {
    if (spec.description.empty()) return;
    auto viewer = makeUniqueNoThrow<DictionaryDefinitionActivity>(renderer, mappedInput, spec.title, spec.description,
                                                                  spec.descriptionHtml);
    if (!viewer) {
      LOG_ERR("DETAILS", "OOM: description viewer");
      return;
    }
    startActivityForResult(std::move(viewer), nullptr);
  } else
    UtilityActivity::applyUtilityEffect(e);
}
