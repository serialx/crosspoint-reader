#pragma once

#include <Epub/Page.h>
#include <FontCacheManager.h>
#include <I18n.h>
#include <xp_ui_dictionary.h>

#include <memory>
#include <optional>
#include <string>

#include "activities/RustActivityAdapter.h"
#include "util/Dictionary.h"

// Controller for dictionary lookup: renders the background page, runs the
// lookup against the active StarDict dictionary, and either opens
// DictionaryDefinitionActivity or displays a status popup (Looking up,
// Indexing, Not found, Error).
class DictionaryWordSelectActivity final : public RustActivityAdapter {
 public:
  explicit DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        std::unique_ptr<Page> page, int marginLeft, int marginTop,
                                        std::string lookupText = {})
      : RustActivityAdapter("DictionaryWordSelect", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop),
        lookupText(std::move(lookupText)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void performLookup();

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  int fontId = 0;

  std::string lookupText;
  XpUiLookupState state{};
  Dictionary dict;
  bool dictOpenAttempted = false;
  bool dictOpenOk = false;
  bool dictNeedsIndex = false;

  XpUiCommand commands[3]{};
  XpUiPaint rustResources() override;
  // Keep the cache through overlays, footer and refresh, then release it for this frame.
  std::optional<FontCacheManager::PrewarmScope> framePrewarm;
  StrId popupMsg = StrId::STR_DICT_NOT_FOUND;
};
