#pragma once
#include <Arduino.h>
#include <cassert>
#include <string>
namespace obfuscation {
inline String obfuscateToBase64(const std::string&) { assert(false && "unexpected recents encryption"); return {}; }
inline std::string deobfuscateFromBase64(const char*, size_t, bool*, bool*) { assert(false && "unexpected recents decoding"); return {}; }
}
