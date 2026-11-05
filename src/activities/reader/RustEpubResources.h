#pragma once

#include <xp_epub_session.h>

class GfxRenderer;
class MappedInputManager;

const XpEpubLeaves& rustEpubResources(GfxRenderer& renderer, MappedInputManager& input);
