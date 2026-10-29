#pragma once
#include <xp_ui_dictionary.h>
class GfxRenderer;
class MappedInputManager;
XpUiInput dictionaryUiInput(const MappedInputManager&);
XpUiDefinitionLayout dictionaryUiDefinitionLayout(const GfxRenderer&);
bool dictionaryUiPopup(const GfxRenderer&, const char*, XpUiCommand*, uint16_t, XpUiScene&);
