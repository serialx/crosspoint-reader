#pragma once
#include <xp_ffi.h>

#include <algorithm>
#include <string>

#include "CrossPointPosition.h"
inline XpSyncSpan syncSpan(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
inline XpSyncPosition syncPosition(const CrossPointPosition& p) {
  XpSyncPosition out{p.spineIndex,
                     p.pageNumber,
                     p.totalPages,
                     p.visibleTextOffset,
                     p.hasVisibleTextOffset,
                     p.paragraphIndex,
                     p.hasParagraphIndex,
                     p.liIndex,
                     p.hasLiIndex,
                     {},
                     p.hasResolvedSpineIndex,
                     p.hasMappedPage};
  std::copy_n(p.xpathAnchorId, 64, out.anchor);
  return out;
}
inline CrossPointPosition syncPosition(const XpSyncPosition& p) {
  CrossPointPosition out{
      p.spine, p.page,        p.pages, p.offset,        p.has_offset != 0, p.paragraph, p.has_paragraph != 0,
      p.li,    p.has_li != 0, {},      p.resolved != 0, p.mapped != 0};
  std::copy_n(p.anchor, 64, out.xpathAnchorId);
  return out;
}
