#include "DictionaryWordSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "DictionaryUiBindings.h"
#include "components/UITheme.h"

namespace {

void indexBuildYield(void*) { vTaskDelay(1); }

}  // namespace

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  xp_ui_lookup_init(&state);
  popupMsg = StrId::STR_DICT_LOOKING_UP;
  state.popup = 1;
  requestUpdate();
}

void DictionaryWordSelectActivity::performLookup() {
  {
    RenderLock lock;
    popupMsg = StrId::STR_DICT_LOOKING_UP;
    state.popup = 1;
  }
  if (!dictOpenAttempted) {
    dictOpenAttempted = true;
    dictOpenOk = dict.open(SETTINGS.dictionaryName);
    // needsIndex() opens and validates the .qidx sidecar, so ask it once per
    // open rather than once per word: the answer only changes when we build
    // the sidecar ourselves, which is handled below.
    dictNeedsIndex = dictOpenOk && dict.needsIndex();
  }
  {
    RenderLock lock;
    popupMsg = dictNeedsIndex ? StrId::STR_DICT_INDEXING : StrId::STR_DICT_LOOKING_UP;
  }
  requestUpdateAndWait();  // paint the page + busy popup before blocking on SD

  bool ok = dictOpenOk;
  Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
  if (ok && dictNeedsIndex) {
    ok = dict.buildIndex(&indexBuildYield, nullptr, &indexResult);
    dictNeedsIndex = !ok;  // a successful build leaves the sidecar fresh; a failed one retries
  }

  std::string definition;
  std::string headword;
  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
  const bool found = ok && dict.lookup(lookupText.c_str(), definition, headword, &result);

  if (found) {
    {
      RenderLock lock;
      state.popup = 0;
      page.reset();
    }
    auto activity = makeUniqueNoThrow<DictionaryDefinitionActivity>(renderer, mappedInput, std::move(headword),
                                                                    std::move(definition), dict.definitionsAreHtml());
    if (!activity || !activity->enableLookup(dict, std::move(lookupText))) {
      LOG_ERR("DICT", "OOM: definition activity");
      state.popup = 3;
      popupMsg = StrId::STR_DICT_LOW_MEMORY;
      state.since = millis();
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(activity), [this](const ActivityResult& result) {
      setResult(ActivityResult{result});
      finish();
    });
    return;
  }
  xp_ui_lookup_result(&state, ok, static_cast<uint8_t>(indexResult), static_cast<uint8_t>(result), found, millis());
  static constexpr StrId MESSAGES[] = {StrId::STR_DICT_LOOKING_UP,  StrId::STR_DICT_INDEXING,
                                       StrId::STR_DICT_NOT_FOUND,   StrId::STR_DICT_LOW_MEMORY,
                                       StrId::STR_DICT_READ_FAILED, StrId::STR_DICT_DECOMPRESS_ERROR,
                                       StrId::STR_DICT_ERROR};
  popupMsg = MESSAGES[state.message];
  requestUpdate();
}

void DictionaryWordSelectActivity::loop() {
  if (rustSceneRejected()) requestUpdate();
  if (state.pending) {
    state.pending = 0;
    performLookup();
    return;
  }
  if (xp_ui_lookup_expired(&state, millis())) finish();
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  const XpUiBuffer buffer{commands, nullptr, 3, 0};
  XpUiScene scene{};
  const XpUiRect screen{0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                        static_cast<int16_t>(renderer.getScreenHeight())};
  if (xp_ui_lookup_view(&state, screen, &buffer, &scene) != 1) {
    rejectRustScene();
    return;
  }
  const bool complete = paintRustScene(scene, !state.popup);
  if (complete && state.popup) {
    if (dictionaryUiPopup(renderer, I18N.get(popupMsg), commands, 3, scene))
      paintRustScene(scene);
    else
      rejectRustScene();
  }
  framePrewarm.reset();
}

XpUiPaint DictionaryWordSelectActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void*, uint32_t handle, uint32_t generation) -> uint8_t {
            return generation == 1 && (handle == XP_UI_DICT_PAGE || handle == XP_UI_DICT_MESSAGE);
          },
          [](void* context, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<DictionaryWordSelectActivity*>(context);
            if (command->resource == XP_UI_DICT_MESSAGE) {
              self.renderer.drawUiText(*command, clip, I18N.get(self.popupMsg));
              return;
            }
            self.framePrewarm.emplace(*self.renderer.getFontCacheManager());
            if (self.page) self.page->render(self.renderer, self.fontId, self.marginLeft, self.marginTop);
            self.framePrewarm->endScanAndPrewarm();
            if (self.page) self.page->render(self.renderer, self.fontId, self.marginLeft, self.marginTop);
          }};
}
