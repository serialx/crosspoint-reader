#pragma once

#include <xp_ffi.h>

#include <cstdint>

// ParsedText stores each boundary in two bit-packed vectors. Together the bits distinguish
// ordinary word gaps, CJK-style stretchable zero-width gaps, unbreakable attachment, and
// breakable attachment after visible hyphens/dashes.
namespace TokenBoundary {

// Soft hyphens become visible only when their conditional break is taken. U+2011 exists to forbid
// a break. Every other character classified by isExplicitHyphen is a visible break opportunity.
inline bool allowsBreakAfterExplicitHyphen(const uint32_t cp) { return xp_text_break_after_hyphen(cp); }

}  // namespace TokenBoundary
