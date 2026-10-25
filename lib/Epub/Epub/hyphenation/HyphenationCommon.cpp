#include "HyphenationCommon.h"

#include <xp_ffi.h>

bool isExplicitHyphen(const uint32_t cp) { return xp_text_explicit_hyphen(cp); }
bool isSoftHyphen(const uint32_t cp) { return cp == 0x00AD; }
