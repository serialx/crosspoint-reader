#pragma once

#include <xp_app.h>

class GfxRenderer;
class MappedInputManager;

const XpAppResources& rustAppResources(GfxRenderer& renderer, MappedInputManager& input);
