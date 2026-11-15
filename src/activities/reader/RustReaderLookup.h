#pragma once
#include <xp_epub_session.h>

class GfxRenderer;
class MappedInputManager;

namespace readerLookup {
void init(XpEpubLookupStorage* storage, XpAppText query);
void drop(XpEpubLookupStorage* storage);
uint8_t open(XpEpubLookupStorage* storage, uint8_t* needsIndex);
uint8_t index(XpEpubLookupStorage* storage, uint8_t* result);
uint8_t find(XpEpubLookupStorage* storage, uint8_t* result);
void discard(XpEpubLookupStorage* storage);
uint8_t payloadLookup(XpDefinitionPayloadStorage*, XpEpubLookupStorage*, XpAppTextSink, void*, uint8_t*);
uint8_t payloadFind(XpDefinitionPayloadStorage*, uint8_t, XpEpubLookupStorage*, XpAppText, uint8_t*);
void directionalHints(GfxRenderer& renderer, MappedInputManager& input, const XpAppText* labels);
void popupLayout(const GfxRenderer& renderer, XpUiDictionaryPopup* out);
}  // namespace readerLookup
