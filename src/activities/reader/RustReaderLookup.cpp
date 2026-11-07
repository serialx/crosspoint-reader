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
#include "DictionaryDefinitionActivity.h"
#include "MappedInputManager.h"
#include "activities/OwnedLegacyActivity.h"
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
struct Definition {
  OwnedLegacyActivity adapter{nullptr};
  alignas(DictionaryDefinitionActivity) std::byte activity[sizeof(DictionaryDefinitionActivity)];
};
static_assert(sizeof(Lookup) <= sizeof(XpEpubLookupStorage));
static_assert(alignof(Lookup) <= alignof(XpEpubLookupStorage));
static_assert(sizeof(Definition) <= sizeof(XpEpubDefinitionStorage));
static_assert(alignof(Definition) <= alignof(XpEpubDefinitionStorage));
static_assert(offsetof(Definition, adapter) == 0);
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(Lookup) == 360);
static_assert(sizeof(Definition) == 444);
#endif
Lookup& get(XpEpubLookupStorage* storage) { return *std::launder(reinterpret_cast<Lookup*>(storage)); }
void indexYield(void*) { vTaskDelay(1); }

Definition* construct(XpEpubDefinitionStorage* storage, GfxRenderer& renderer, MappedInputManager& input,
                      std::string&& title, std::string&& text, bool html) {
  auto* owner = ::new (static_cast<void*>(storage)) Definition;
  auto* viewer = ::new (static_cast<void*>(owner->activity))
      DictionaryDefinitionActivity(renderer, input, std::move(title), std::move(text), html);
  owner->adapter.activity.reset(viewer);
  return owner;
}
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
void* definitionLookup(XpEpubDefinitionStorage* storage, XpEpubLookupStorage* source, GfxRenderer& renderer,
                       MappedInputManager& input) {
  auto& lookup = get(source);
  auto* owner = construct(storage, renderer, input, std::move(lookup.headword), std::move(lookup.definition),
                          lookup.dictionary.definitionsAreHtml());
  auto* viewer = static_cast<DictionaryDefinitionActivity*>(owner->adapter.activity.get());
  if (!viewer->enableLookup(lookup.dictionary, std::move(lookup.query))) {
    definitionDrop(storage);
    return nullptr;
  }
  return &owner->adapter;
}
void* definitionPlain(XpEpubDefinitionStorage* storage, XpAppText title, uint32_t length, XpEpubDefinitionFill fill,
                      void* context, GfxRenderer& renderer, MappedInputManager& input) {
  std::string text;
  text.resize(length);
  if (!fill(context, reinterpret_cast<uint8_t*>(text.data()), length)) return nullptr;
  std::string heading(title.len ? reinterpret_cast<const char*>(title.data) : "", title.len);
  return &construct(storage, renderer, input, std::move(heading), std::move(text), false)->adapter;
}
void definitionDrop(XpEpubDefinitionStorage* storage) {
  auto* owner = std::launder(reinterpret_cast<Definition*>(storage));
  // The adapter points into caller storage; releasing first prevents delete.
  auto* viewer = static_cast<DictionaryDefinitionActivity*>(owner->adapter.activity.release());
  std::destroy_at(viewer);
  std::destroy_at(owner);
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
