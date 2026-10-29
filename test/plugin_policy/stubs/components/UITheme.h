#pragma once
#include "Oracle.h"
class GfxRenderer {};
struct Theme { void drawPopup(GfxRenderer&, const char* s) { oracle::record("popup %s",s); } };
inline Theme GUI;
