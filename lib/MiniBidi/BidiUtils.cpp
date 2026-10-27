#include "BidiUtils.h"

extern "C" {
#include "minibidi.h"
}

#undef when
#undef otherwise

#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <xp_ffi.h>

#include <cstring>
#include <mutex>

// Guards the shared workspace in applyBidiVisual() and
// computeVisualWordOrder().  The bidi+shaping pipeline is not reentrant;
// this mutex serialises access so multi-core callers don't corrupt each
// other's intermediate state.
static std::mutex bidiMutex;

namespace {

bool isNaturalDirectionClass(const uchar cls) {
  switch (cls) {
    case L:
    case R:
    case AL:
    case EN:
    case AN:
      return true;
    default:
      return false;
  }
}

struct BidiWorkspace {
  bidi_char line[BIDI_MAX_LINE];
  bidi_char shaped[BIDI_MAX_LINE];
};
static_assert(sizeof(BidiWorkspace) == 3072);
static_assert(alignof(BidiWorkspace) >= alignof(uint32_t));

// Retained after first use to avoid allocation churn on every RTL token.
std::unique_ptr<BidiWorkspace> bidiWorkspace;

// Caller holds bidiMutex, including during allocation and subsequent use.
BidiWorkspace* getWorkspace() {
  if (!bidiWorkspace) {
    bidiWorkspace = makeUniqueNoThrow<BidiWorkspace>();
    if (!bidiWorkspace) {
      LOG_ERR("BIDI", "Failed to allocate %zu-byte workspace", sizeof(BidiWorkspace));
    }
  }
  return bidiWorkspace.get();
}

}  // namespace

namespace BidiUtils {

bool startsWithRtl(const char* utf8, int maxStrongChars) {
  return utf8 && startsWithRtl(std::string_view(utf8), maxStrongChars);
}
bool startsWithRtl(std::string_view utf8, int maxStrongChars) {
  if (maxStrongChars <= 0) return false;
  size_t position = 0;
  int checked = 0;
  while (position < utf8.size()) {
    size_t consumed = 0;
    const uint32_t cp =
        xp_utf8_next(reinterpret_cast<const uint8_t*>(utf8.data() + position), utf8.size() - position, &consumed);
    if (!cp || cp == REPLACEMENT_GLYPH || !consumed) break;
    position += consumed;
    const uchar cls = bidi_class(cp);
    if (cls == R || cls == AL) return true;
    if (cls == L) return false;
    if (++checked >= maxStrongChars) break;
  }
  return false;
}

int detectParagraphLevel(const char* utf8, const int fallbackLevel, const int maxStrongChars) {
  if (!utf8 || maxStrongChars <= 0) return fallbackLevel & 1;

  auto* p = reinterpret_cast<const unsigned char*>(utf8);
  int checked = 0;
  while (*p) {
    const uint32_t cp = utf8NextCodepoint(&p);
    if (!cp || cp == REPLACEMENT_GLYPH) break;

    const uchar cls = bidi_class(cp);
    if (cls == R || cls == AL) return 1;
    if (cls == L) return 0;
    checked++;
    if (checked >= maxStrongChars) break;
  }

  return fallbackLevel & 1;
}

bool isTransparentMark(const uint32_t cp) {
  // RTL-script combining marks: Hebrew niqqud/cantillation and Arabic
  // harakat/Quranic annotation.  Transparent for Arabic joining (do_shape
  // skips them), zero-advance for measurement, and rendered as overlays on
  // the preceding base glyph when the active font carries their glyphs.
  // The cp >= 0x0591 guard keeps Latin combining marks (U+0300-U+036F, also
  // NSM) on their existing utf8IsCombiningMark() rendering path.
  return cp >= 0x0591 && bidi_class(cp) == NSM;
}

bool applyBidiVisual(const char* utf8, std::string& out, int paragraphLevel) {
  if (!utf8 || !*utf8) return false;
  const std::lock_guard<std::mutex> lock(bidiMutex);

  auto* const workspace = getWorkspace();
  if (!workspace) return false;
  bidi_char* const line = workspace->line;
  bidi_char* const shaped = workspace->shaped;
  int count = 0;
  int lastBase = -1;           // last non-formatter character (mintty's ibase)
  uint8_t pendingJoiners = 0;  // ZWJ/ZWNJ seen since lastBase
  auto* p = reinterpret_cast<const unsigned char*>(utf8);
  while (*p) {
    if (count >= BIDI_MAX_LINE) {
      LOG_DBG("BIDI", "applyBidiVisual: input exceeds BIDI_MAX_LINE (%d chars), returning unprocessed", BIDI_MAX_LINE);
      return false;
    }

    const uint32_t cp = utf8NextCodepoint(&p);
    if (!cp || cp == REPLACEMENT_GLYPH) break;
    line[count].origwc = line[count].wc = cp;
    line[count].index = static_cast<uint16_t>(count);
    line[count].joiners = 0;

    // Flag Arabic joining formatters mintty-style (termline.c): the ZWJ/ZWNJ
    // goes into the low nibble of the character it follows and the high
    // nibble of the character it precedes.  Flags are assigned in logical
    // order here; do_shape() reads them after reordering.
    if (cp == 0x200C || cp == 0x200D) {
      const uint8_t joiner = (cp == 0x200D) ? ZWJ : ZWNJ;
      if (lastBase >= 0) line[lastBase].joiners |= joiner;
      pendingJoiners |= joiner;
    } else {
      line[count].joiners = pendingJoiners << 4;
      pendingJoiners = 0;
      lastBase = count;
    }
    count++;
  }
  if (!count) return false;

  const bool autodir = (paragraphLevel < 0);
  const int level = autodir ? 0 : (paragraphLevel & 1);

  // Order matters (mintty does the same): do_bidi() first to obtain visual
  // order, then do_shape() — contextual forms are resolved from *visual*
  // adjacency, and shaping presentation forms must never be reordered.
  do_bidi(autodir, level, line, count);
  do_shape(line, shaped, count);

  out.clear();
  out.reserve(std::strlen(utf8));
  // Lam-Alef collapse sentinel and zero-width joining formatters have done
  // their job during shaping and have no glyphs to render.
  const auto filtered = [](const uint32_t cp) { return cp == LIGATURE_PLACEHOLDER || cp == 0x200C || cp == 0x200D; };
  for (int i = 0; i < count; i++) {
    const uint32_t cp = shaped[i].wc;
    if (filtered(cp)) continue;
    // Match the name's base letters in visual order, skipping their niqqud.
    int end = cp == 0x05D4 || isTransparentMark(cp) ? i : count, matched = 0;
    for (; end < count && matched < 4; ++end)
      if (!isTransparentMark(shaped[end].wc)) matched = shaped[end].wc == U"הוהי"[matched] ? matched + 1 : 5;
    if (matched == 4) {
      out += "'ה";
      i = end - 1;
      continue;
    }
    if (!isTransparentMark(cp)) {
      utf8AppendCodepoint(cp, out);
      continue;
    }
    // UAX#9 rule L3: reversing an RTL run leaves combining marks *before*
    // their base character. The renderer overlays a mark on the most
    // recently drawn glyph, so emit the base first, then its marks in
    // logical order. `index` is the original logical position: a base
    // following its marks with a *lower* index means the run was reversed.
    int j = i;  // [i, j) = the run of marks (and filtered entries)
    while (j < count && (filtered(shaped[j].wc) || isTransparentMark(shaped[j].wc))) j++;
    if (j < count && shaped[j].index < shaped[i].index) {
      utf8AppendCodepoint(shaped[j].wc, out);
      for (int k = j - 1; k >= i; k--) {
        if (isTransparentMark(shaped[k].wc)) utf8AppendCodepoint(shaped[k].wc, out);
      }
      i = j;  // base already emitted
    } else {
      // Unreversed (or trailing, base-less) marks already follow their base.
      for (int k = i; k < j; k++) {
        if (isTransparentMark(shaped[k].wc)) utf8AppendCodepoint(shaped[k].wc, out);
      }
      i = j - 1;
    }
  }
  return true;
}

static bool computeWordOrder(size_t nWords, const void* words, const char* (*wordAt)(const void*, size_t),
                             bool paragraphIsRtl, std::span<uint16_t> visualOrder) {
  size_t outputCount = 0;
  if (nWords <= 1 || nWords > BIDI_MAX_LINE) return false;
  const std::lock_guard<std::mutex> lock(bidiMutex);

  auto* const workspace = getWorkspace();
  if (!workspace) return false;
  bidi_char* const line = workspace->line;
  int count = 0;
  bool truncated = false;

  for (size_t w = 0; w < nWords && !truncated; w++) {
    auto* p = reinterpret_cast<const unsigned char*>(wordAt(words, w));
    while (*p) {
      if (count >= BIDI_MAX_LINE) {
        truncated = true;
        break;
      }
      const uint32_t cp = utf8NextCodepoint(&p);
      if (!cp || cp == REPLACEMENT_GLYPH) break;
      line[count].origwc = line[count].wc = cp;
      line[count].index = static_cast<uint16_t>(w);
      line[count].joiners = 0;
      count++;
    }

    if (!truncated && w + 1 < nWords) {
      if (count >= BIDI_MAX_LINE) {
        truncated = true;
        break;
      }
      line[count].origwc = line[count].wc = ' ';
      line[count].index = static_cast<uint16_t>(nWords);
      line[count].joiners = 0;
      count++;
    }
  }

  if (truncated || count == 0) return false;

  // Fast-path for homogeneous lines: skip UAX#9 if there's no mixing.
  bool hasL = false, hasR = false;
  for (int i = 0; i < count; i++) {
    uchar bc = bidi_class(line[i].wc);
    if (bc == L || bc == EN || bc == AN)
      hasL = true;
    else if (bc == R || bc == AL)
      hasR = true;
  }

  // Purely LTR line in RTL paragraph: identity order, but we might still need to reorder
  // if some characters are mirrored or neutral resolution differs.
  // Actually, UAX#9 rule L1/L2 says purely LTR in RTL para stays as is (identity).
  // Purely RTL line: just reverse the words.
  if (!hasL && hasR && paragraphIsRtl) {
    for (int i = static_cast<int>(nWords) - 1; i >= 0; i--) {
      visualOrder[outputCount++] = static_cast<uint16_t>(i);
    }
    return true;
  }
  if (!hasR) {
    if (!paragraphIsRtl) {
      // Pure LTR in LTR paragraph: nothing to do.
      return false;
    }
    // Pure LTR in RTL paragraph: no word reordering, but must use the
    // willReorder (left-to-right) positioning path, not the RTL right-to-left path.

    for (size_t i = 0; i < nWords; i++) {
      visualOrder[outputCount++] = static_cast<uint16_t>(i);
    }
    return true;
  }

  do_bidi(/*autodir=*/false, paragraphIsRtl ? 1 : 0, line, count);

  uint16_t firstAny[BIDI_MAX_LINE];
  uint16_t firstNatural[BIDI_MAX_LINE];
  for (size_t w = 0; w < nWords; w++) {
    firstAny[w] = UINT16_MAX;
    firstNatural[w] = UINT16_MAX;
  }

  for (int i = 0; i < count; i++) {
    const uint16_t w = line[i].index;
    if (w >= nWords) continue;

    if (firstAny[w] == UINT16_MAX) {
      firstAny[w] = static_cast<uint16_t>(i);
    }

    if (firstNatural[w] == UINT16_MAX && isNaturalDirectionClass(bidi_class(line[i].wc))) {
      firstNatural[w] = static_cast<uint16_t>(i);
    }
  }

  for (int i = 0; i < count; i++) {
    const uint16_t w = line[i].index;
    if (w >= nWords) continue;

    const uint16_t anchor = firstNatural[w] != UINT16_MAX ? firstNatural[w] : firstAny[w];
    if (anchor == UINT16_MAX) {
      outputCount = 0;
      return false;
    }
    if (anchor == static_cast<uint16_t>(i)) {
      visualOrder[outputCount++] = w;
    }
  }

  if (outputCount != nWords) {
    outputCount = 0;
    return false;
  }

  // Check if the order is exactly the same as the original input
  bool needsReorder = false;
  for (size_t i = 0; i < nWords; i++) {
    if (visualOrder[i] != i) {
      needsReorder = true;
      break;
    }
  }

  if (!needsReorder) {
    outputCount = 0;
    return false;
  }

  return true;
}

bool computeVisualWordOrder(const std::vector<std::string>& words, bool paragraphIsRtl,
                            std::vector<uint16_t>& visualOrder) {
  visualOrder.clear();
  if (words.size() <= 1 || words.size() > BIDI_MAX_LINE) return false;
  visualOrder.reserve(words.size());
  visualOrder.resize(words.size());
  const bool result = computeWordOrder(
      words.size(), &words,
      [](const void* raw, size_t i) { return (*static_cast<const std::vector<std::string>*>(raw))[i].c_str(); },
      paragraphIsRtl, visualOrder);
  if (!result) visualOrder.clear();
  return result;
}
bool computeVisualWordOrder(std::span<const char> text, std::span<const uint16_t> offsets, bool paragraphIsRtl,
                            std::span<uint16_t> visualOrder) {
  if (visualOrder.size() != offsets.size()) return false;
  for (uint16_t offset : offsets) {
    if (offset >= text.size() || !memchr(text.data() + offset, 0, text.size() - offset)) return false;
  }
  struct Words {
    std::span<const char> text;
    std::span<const uint16_t> offsets;
  } words{text, offsets};
  return computeWordOrder(
      offsets.size(), &words,
      [](const void* raw, size_t i) {
        const auto& words = *static_cast<const Words*>(raw);
        return words.text.data() + words.offsets[i];
      },
      paragraphIsRtl, visualOrder);
}

}  // namespace BidiUtils

extern "C" void* xp_native_bidi_acquire() {
  bidiMutex.lock();
  auto* workspace = getWorkspace();
  if (!workspace) bidiMutex.unlock();
  return workspace;
}
extern "C" void xp_native_bidi_release() { bidiMutex.unlock(); }
