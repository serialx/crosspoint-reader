#include "RustReaderLookup.h"

#include <GfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstddef>
#include <memory>
#include <new>
#include <string>

#include "CrossPointSettings.h"
#include "DefinitionPayload.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/Dictionary.h"

namespace readerLookup {
namespace {
struct Lookup {
  Dictionary dictionary;
  std::string query;
  std::string definition;
  std::string headword;
  explicit Lookup(XpAppText text) : query(text.len ? reinterpret_cast<const char*>(text.data) : "", text.len) {}
};
static_assert(sizeof(Lookup) <= sizeof(XpEpubLookupStorage));
static_assert(alignof(Lookup) <= alignof(XpEpubLookupStorage));
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(Lookup) == 360);
#endif
Lookup& get(XpEpubLookupStorage* storage) { return *std::launder(reinterpret_cast<Lookup*>(storage)); }
void indexYield(void*) { vTaskDelay(1); }

}  // namespace

void init(XpEpubLookupStorage* storage, XpAppText query) { ::new (static_cast<void*>(storage)) Lookup(query); }
void drop(XpEpubLookupStorage* storage) { std::destroy_at(&get(storage)); }
uint8_t open(XpEpubLookupStorage* storage, uint8_t* needsIndex) {
  auto& dict = get(storage).dictionary;
  const bool opened = dict.open(SETTINGS.dictionaryName);
  *needsIndex = opened && dict.needsIndex();
  return opened;
}
uint8_t index(XpEpubLookupStorage* storage, uint8_t* result) {
  auto code = Dictionary::IndexResult::Ok;
  const bool ok = get(storage).dictionary.buildIndex(indexYield, nullptr, &code);
  *result = static_cast<uint8_t>(code);
  return ok;
}
uint8_t find(XpEpubLookupStorage* storage, uint8_t* result) {
  auto& lookup = get(storage);
  auto code = Dictionary::LookupResult::NotFound;
  const bool found = lookup.dictionary.lookup(lookup.query.c_str(), lookup.definition, lookup.headword, &code);
  *result = static_cast<uint8_t>(code);
  return found;
}
void discard(XpEpubLookupStorage* storage) {
  auto& lookup = get(storage);
  std::string{}.swap(lookup.headword);
  std::string{}.swap(lookup.definition);
}
uint8_t payloadLookup(XpDefinitionPayloadStorage* storage, XpEpubLookupStorage* source, XpAppTextSink sink,
                      void* context, uint8_t* html) {
  auto& lookup = get(source);
  if (!sink(context, {reinterpret_cast<const uint8_t*>(lookup.query.data()), lookup.query.size()})) return 0;
  auto* payload = ::new (storage) DefinitionPayload;
  payload->slots[0].headword = std::move(lookup.headword);
  payload->slots[0].text = std::move(lookup.definition);
  std::string{}.swap(lookup.query);
  *html = lookup.dictionary.definitionsAreHtml();
  return 1;
}
uint8_t payloadFind(XpDefinitionPayloadStorage* storage, uint8_t slot, XpEpubLookupStorage* source, XpAppText query,
                    uint8_t* result) {
  auto& payload = DefinitionPayload::get(storage).slot(slot);
  auto code = Dictionary::LookupResult::Found;
  const bool found = get(source).dictionary.lookup(query.len ? reinterpret_cast<const char*>(query.data) : "",
                                                   payload.text, payload.headword, &code);
  *result = static_cast<uint8_t>(code);
  return found;
}
void directionalHints(GfxRenderer& renderer, MappedInputManager& input, const XpAppText* labels) {
  const auto text = [labels](int index) { return reinterpret_cast<const char*>(labels[index].data); };
  const auto mapped = input.mapDirectionalLabels(text(0), text(1), text(2), text(3), text(4), text(5));
  GUI.drawButtonHints(renderer, mapped.btn1, mapped.btn2, mapped.btn3, mapped.btn4);
}
void popupLayout(const GfxRenderer& renderer, XpUiDictionaryPopup* out) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  *out = {{0, 0, static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(renderer.getScreenHeight())},
          UI_12_FONT_ID,
          static_cast<int16_t>(metrics.popupMarginX),
          static_cast<int16_t>(metrics.popupMarginY),
          static_cast<int16_t>(metrics.popupFrameThickness),
          static_cast<int16_t>(metrics.popupCornerRadius),
          static_cast<int16_t>(metrics.popupTextBaselineOffsetY),
          metrics.popupTopOffsetRatio,
          static_cast<uint8_t>(metrics.popupTextBold),
          static_cast<uint8_t>(metrics.popupTextInverted),
          {0, 0}};
}
}  // namespace readerLookup
