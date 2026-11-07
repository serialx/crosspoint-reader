#pragma once
#include <xp_clippings.h>

#include <cassert>
#include <vector>

extern "C" uint8_t xp_epub_test_clipping_seed_metadata(void*, const XpClippingRecord*, size_t, void (*)(uint32_t),
                                                       void (*)(uint32_t, const XpClippingRecord*));
extern "C" {
uint8_t xp_epub_test_clipping_load(void*, XpAppText);
uint8_t xp_epub_test_clipping_insert(void*, const XpClippingRecord*, XpAppText, XpAppNativeText);
uint8_t xp_epub_test_clipping_remove(void*, uint32_t);
void xp_epub_test_clipping_reset(void*);
void xp_epub_test_clipping_results(void*, void (*)(const XpClippingRecord*, XpAppText, XpAppNativeText, uint8_t),
                                   void (*)(uint32_t, uint8_t));
}

namespace clipping_fixture {
inline void seedCount(void* owner, size_t count, void (*observe)(uint32_t) = nullptr) {
  std::vector<XpClippingRecord> records(count);
  assert(xp_epub_test_clipping_seed_metadata(owner, records.data(), records.size(), observe, nullptr));
}

template <typename Row>
void seed(void* owner, size_t count, Row row, void (*observe)(uint32_t) = nullptr,
          void (*observeRow)(uint32_t, const XpClippingRecord*) = nullptr) {
  std::vector<XpClippingRecord> records(count);
  for (size_t i = 0; i < count; ++i) {
    const auto& source = *row(i);
    auto& target = records[i];
    target.spine = source.spineIndex;
    target.start_page = source.startPage;
    target.end_page = source.endPage;
    target.page_count = source.pageCount;
    target.start_word = source.startWordIndex;
    target.end_word = source.endWordIndex;
    target.signature = source.layoutSignature;
    target.start_offset = source.startOffset;
    target.end_offset = source.endOffset;
  }
  assert(xp_epub_test_clipping_seed_metadata(owner, records.data(), records.size(), observe, observeRow));
}
}  // namespace clipping_fixture
