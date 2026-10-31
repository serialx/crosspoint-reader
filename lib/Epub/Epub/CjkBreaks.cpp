#include "CjkBreaks.h"

#include <xp_ffi.h>

namespace CjkBreaks {
bool isNoBreakBeforeCjkPunctuation(const uint32_t cp) { return xp_text_no_break_before(cp); }
bool isNoBreakAfterCjkPunctuation(const uint32_t cp) { return xp_text_no_break_after(cp); }
bool boundaryAllowsBreak(const uint32_t leftCp, const uint32_t rightCp) { return xp_text_boundary(leftCp, rightCp); }
bool hasBreakOpportunityBetween(const uint32_t leftCp, const uint32_t rightCp) {
  return xp_text_opportunity(leftCp, rightCp);
}
std::vector<size_t> characterBreakByteOffsets(const std::string_view text) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
  const auto count = xp_text_offsets(bytes, text.size(), nullptr, 0);
  if (!count) return {};
  std::vector<size_t> offsets(count);
  xp_text_offsets(bytes, text.size(), offsets.data(), offsets.size());
  return offsets;
}
}  // namespace CjkBreaks
