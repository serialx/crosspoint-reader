#include <Logging.h>
#include <Memory.h>
#include <xp_ffi.h>

#include <cstring>
#include <mutex>

#include "BidiUtils.h"

namespace {
std::mutex bidiMutex;
struct alignas(uint32_t) BidiWorkspace {
  uint8_t bytes[3072];
};
std::unique_ptr<BidiWorkspace> bidiWorkspace;
BidiWorkspace* getWorkspace() {
  if (!bidiWorkspace) {
    bidiWorkspace = makeUniqueNoThrow<BidiWorkspace>();
    if (!bidiWorkspace) {
      LOG_ERR("BIDI", "Failed to allocate %zu-byte workspace", sizeof(BidiWorkspace));
      return nullptr;
    }
    if (xp_bidi_workspace_init(bidiWorkspace.get(), sizeof(BidiWorkspace)) != 1) {
      LOG_ERR("BIDI", "Incompatible workspace size/alignment");
      bidiWorkspace.reset();
    }
  }
  return bidiWorkspace.get();
}
}  // namespace

namespace BidiUtils {
bool startsWithRtl(const char* text, int depth) { return text && startsWithRtl(std::string_view(text), depth); }
bool startsWithRtl(std::string_view text, int depth) {
  return xp_bidi_starts_rtl(reinterpret_cast<const uint8_t*>(text.data()), text.size(), depth);
}
int detectParagraphLevel(const char* text, int fallback, int depth) {
  return xp_bidi_paragraph_level(reinterpret_cast<const uint8_t*>(text), text ? strlen(text) : 0, fallback, depth);
}
bool isTransparentMark(uint32_t cp) { return xp_bidi_transparent_mark(cp); }
bool applyBidiVisual(const char* text, std::string& out, int direction) {
  if (!text || !*text) return false;
  const std::lock_guard<std::mutex> lock(bidiMutex);
  auto* work = getWorkspace();
  if (!work) return false;
  return xp_bidi_visual_visit(reinterpret_cast<const uint8_t*>(text), strlen(text), direction, work, &out,
                              [](void* ctx, const uint8_t* bytes, size_t len) {
                                static_cast<std::string*>(ctx)->assign(reinterpret_cast<const char*>(bytes), len);
                              });
}
void appendShapedRtlTokens(const char* text, std::string& out) {
  // Avoid allocating the retained workspace for entirely LTR chapters.
  bool rtl = false;
  for (auto* p = reinterpret_cast<const uint8_t*>(text); *p; ++p) rtl |= *p >= 0xd6 && *p <= 0xdb;
  if (!rtl) return;
  const std::lock_guard<std::mutex> lock(bidiMutex);
  auto* work = getWorkspace();
  if (!work) return;
  xp_bidi_rtl_tokens(reinterpret_cast<const uint8_t*>(text), strlen(text), work, &out,
                     [](void* ctx, const uint8_t* bytes, size_t len) {
                       static_cast<std::string*>(ctx)->append(reinterpret_cast<const char*>(bytes), len);
                     });
}
bool computeVisualWordOrder(const std::vector<std::string>& words, bool rtl, std::vector<uint16_t>& order) {
  order.clear();
  if (words.size() <= 1 || words.size() > 128) return false;
  order.reserve(words.size());
  order.resize(words.size());
  const std::lock_guard<std::mutex> lock(bidiMutex);
  auto* work = getWorkspace();
  const bool result = work && xp_bidi_words(
                                  words.size(), &words,
                                  [](const void* ctx, size_t i, size_t* len) {
                                    const auto& word = (*static_cast<const std::vector<std::string>*>(ctx))[i];
                                    *len = word.size();
                                    return reinterpret_cast<const uint8_t*>(word.data());
                                  },
                                  rtl, work, order.data()) == 1;
  if (!result) order.clear();
  return result;
}
bool computeVisualWordOrder(std::span<const char> text, std::span<const uint16_t> offsets, bool rtl,
                            std::span<uint16_t> order) {
  if (order.size() != offsets.size()) return false;
  for (auto offset : offsets)
    if (offset >= text.size() || !memchr(text.data() + offset, 0, text.size() - offset)) return false;
  if (offsets.size() <= 1 || offsets.size() > 128) return false;
  const std::lock_guard<std::mutex> lock(bidiMutex);
  auto* work = getWorkspace();
  return work && xp_bidi_word_order(reinterpret_cast<const uint8_t*>(text.data()), text.size(), offsets.data(),
                                    offsets.size(), rtl, work, sizeof(BidiWorkspace), order.data(), order.size()) == 1;
}
}  // namespace BidiUtils
extern "C" void* xp_native_bidi_acquire() {
  bidiMutex.lock();
  auto* workspace = getWorkspace();
  if (!workspace) bidiMutex.unlock();
  return workspace;
}
extern "C" void xp_native_bidi_release() { bidiMutex.unlock(); }
