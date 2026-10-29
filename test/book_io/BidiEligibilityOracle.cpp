#include <BidiUtils.h>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

void* operator new(size_t bytes, const std::nothrow_t&) noexcept {
  printf("workspace %zu\n", bytes);
  return std::malloc(bytes);
}
int main() {
  for (unsigned byte = 1; byte < 256; ++byte) {
    const char text[] = {static_cast<char>(byte), 'a', 0};
    std::string output = "prefix";
    BidiUtils::appendShapedRtlTokens(text, output);
    printf("token %u", byte);
    for (unsigned char c : output) printf(" %02x", c);
    puts("");
  }
  for (unsigned count : {0u, 1u, 2u, 127u, 128u, 129u}) {
    std::vector<std::string> words(count, "word");
    std::vector<uint16_t> order{444};
    for (bool rtl : {false, true}) {
      const bool ok = BidiUtils::computeVisualWordOrder(words, rtl, order);
      printf("words %u %u %u %zu", count, rtl, ok, order.size());
      for (auto value : order) printf(" %u", value);
      puts("");
    }
  }
  for (std::string text : {std::string{}, std::string("a\0b\0", 4), std::string("ab\0", 3), std::string("ab")}) {
    for (uint16_t a = 0; a < 6; ++a) {
      for (uint16_t b = 0; b < 6; ++b) {
        for (size_t capacity : {size_t(0), size_t(1), size_t(2), size_t(3)}) {
          const uint16_t offsets[] = {a, b};
          uint16_t output[] = {444, 555, 666};
          const bool ok = BidiUtils::computeVisualWordOrder(text, offsets, true, {output, capacity});
          printf("span %zu %u %u %zu %u %u %u %u\n", text.size(), a, b, capacity, ok, output[0], output[1], output[2]);
        }
      }
    }
  }
}
