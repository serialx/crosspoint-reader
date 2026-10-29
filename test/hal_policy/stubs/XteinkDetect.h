#pragma once
#include <BoardConfig.h>
namespace freeink {
enum class XteinkVerdict { Inconclusive, X3Confirmed, X4Confirmed };
inline XteinkVerdict verdict = XteinkVerdict::Inconclusive;
inline XteinkVerdict detectXteinkVerdict(uint8_t* a, uint8_t* b) { puts("probe"); *a = 2; *b = 3; return verdict; }
inline void applyXteinkDisplayController() { puts("controller"); }
}
