#include <BidiUtils.h>
#include <Utf8.h>
#include <xp_ffi.h>
extern "C" {
#include <minibidi.h>
}
#undef when
#undef otherwise

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::vector<uint32_t> workspace;
uint64_t cases = 0;
uint64_t state = 0x5a17b1d1c9330408ULL;
uint32_t random32() {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return static_cast<uint32_t>(state);
}
[[noreturn]] void fail(const char* what, const std::string& text) {
  std::cerr << "Mismatch " << what << " at case " << cases << " seed-state " << std::hex << state << " input ";
  for (unsigned char c : text) std::cerr << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
  std::cerr << '\n';
  std::exit(1);
}
void require(bool condition, const char* what, const std::string& text = {}) {
  if (!condition) fail(what, text);
}
void check(const std::string& text, int level, int depth, bool shortBuffer = false) {
  ++cases;
  const auto* data = reinterpret_cast<const uint8_t*>(text.data());
  require(BidiUtils::startsWithRtl(std::string_view(text), depth) == bool(xp_bidi_starts_rtl(data, text.size(), depth)),
          "starts", text);
  require(BidiUtils::detectParagraphLevel(text.c_str(), level, depth) ==
              xp_bidi_paragraph_level(data, text.size(), level, depth),
          "paragraph", text);
  std::string expected = "unchanged";
  const bool applied = BidiUtils::applyBidiVisual(text.c_str(), expected, level);
  std::array<uint8_t, 520> output;
  output.fill(0xa5);
  size_t size = 999;
  const int status = xp_bidi_apply_visual(data, text.size(), level, workspace.data(), workspace.size() * 4,
                                          output.data(), output.size(), &size);
  require(status == int(applied), "visual status", text);
  if (applied) {
    require(std::string(reinterpret_cast<char*>(output.data()), size) == expected, "visual bytes", text);
    require(std::all_of(output.begin() + size, output.end(), [](uint8_t v) { return v == 0xa5; }), "visual tail", text);
  } else {
    require(size == 0 && std::all_of(output.begin(), output.end(), [](uint8_t v) { return v == 0xa5; }),
            "visual failure mutation", text);
  }
  if (shortBuffer && applied && !expected.empty()) {
    output.fill(0xa5);
    const int shortStatus = xp_bidi_apply_visual(data, text.size(), level, workspace.data(), workspace.size() * 4,
                                                 output.data(), expected.size() - 1, &size);
    require(shortStatus == -1 && size == expected.size() &&
                std::all_of(output.begin(), output.end(), [](uint8_t v) { return v == 0xa5; }),
            "short buffer", text);
  }

  // Packed words preserve invalid bytes and empty words; separators become NUL.
  std::string packed = text;
  std::vector<uint16_t> offsets{0};
  for (size_t i = 0; i < packed.size() && i < 65534; ++i) {
    if (packed[i] == ' ' || packed[i] == '\t' || packed[i] == 0) {
      packed[i] = 0;
      offsets.push_back(static_cast<uint16_t>(i + 1));
    }
  }
  packed.push_back(0);
  std::vector<uint16_t> cpp(offsets.size(), 0xabcd), rust = cpp;
  const bool ordered = BidiUtils::computeVisualWordOrder(std::span<const char>(packed), offsets, level & 1, cpp);
  const int orderStatus =
      xp_bidi_word_order(reinterpret_cast<const uint8_t*>(packed.data()), packed.size(), offsets.data(), offsets.size(),
                         level & 1, workspace.data(), workspace.size() * 4, rust.data(), rust.size());
  require(orderStatus == int(ordered), "order status", text);
  require(cpp == rust, "order bytes including declined partial output", text);
}
void boundaries() {
  size_t n = 123;
  uint8_t out = 0xa5;
  require(xp_bidi_workspace_init(nullptr, workspace.size() * 4) == -2, "null workspace");
  require(xp_bidi_workspace_init(workspace.data(), 1) == -2, "short workspace");
  require(xp_bidi_apply_visual(nullptr, 1, 0, workspace.data(), workspace.size() * 4, &out, 1, &n) == -2 && n == 0 &&
              out == 0xa5,
          "null input");
  require(xp_bidi_apply_visual(nullptr, 0, 0, workspace.data(), workspace.size() * 4, nullptr, 0, &n) == 0,
          "empty input");
  uint16_t offset = 5, order = 0xabcd;
  require(xp_bidi_word_order(reinterpret_cast<const uint8_t*>("x"), 2, &offset, 1, 0, workspace.data(),
                             workspace.size() * 4, &order, 1) == -2 &&
              order == 0xabcd,
          "bad offset");
  offset = 0;
  require(xp_bidi_word_order(reinterpret_cast<const uint8_t*>("x"), 1, &offset, 1, 0, workspace.data(),
                             workspace.size() * 4, &order, 1) == -2,
          "missing terminator");
  require(xp_bidi_word_order(nullptr, 0, nullptr, 0, 0, workspace.data(), workspace.size() * 4, &order, 1) == -1,
          "order capacity");
  for (int count : {0, 1, 2, 63, 64, 126, 127, 128, 129, 256}) {
    for (uint32_t cp : {0x61u, 0x628u, 0x202au, 0x202bu, 0x2067u, 0x28u}) {
      std::string text;
      for (int i = 0; i < count; ++i) utf8AppendCodepoint(cp, text);
      for (int level : {-3, -1, 0, 1, 2, 3}) {
        check(text, level, 64, true);
        check(text + "\xff", level, 5, true);
        check(text + " \xd7\x90 )", level, 0, true);
      }
    }
  }
  check("יהוה", -1, 64, true);
  check("יְהוָה", -1, 64, true);
}
}  // namespace
int main(int argc, char** argv) {
  workspace.resize((xp_bidi_workspace_size() + 3) / 4);
  require(xp_bidi_workspace_align() <= alignof(uint32_t), "workspace alignment");
  require(xp_bidi_workspace_init(workspace.data(), workspace.size() * 4) == 1, "workspace init");
  for (uint32_t cp = 0; cp <= 0x110000; ++cp) {
    require(bidi_class(cp) == xp_bidi_class(cp), "class");
    require(mirror(cp) == xp_bidi_mirror(cp), "mirror");
    require(BidiUtils::isTransparentMark(cp) == bool(xp_bidi_transparent_mark(cp)), "mark");
  }
  boundaries();
  if (argc > 1) {
    std::ifstream corpus(argv[1]);
    require(corpus.good(), "corpus open");
    std::string hex;
    while (std::getline(corpus, hex)) {
      std::string text;
      for (size_t i = 0; i < hex.size(); i += 2) text += static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16));
      for (int level : {-1, 0, 1}) check(text, level, 64, true);
    }
  }
  const uint64_t corpusCases = cases;
  const uint32_t pool[] = {
      0x41,   0x61,   0x410,  0x5d0,  0x5d4,  0x5d5,  0x5d9,  0x5b0,   0x628,  0x644,  0x622,  0x623,  0x625,  0x627,
      0x651,  0x64e,  0x67e,  0x6ba,  0x6cc,  0x200c, 0x200d, 0x20,    0x20,   0x20,   0x30,   0x31,   0x661,  0x6f1,
      0x2b,   0x2d,   0x24,   0x2c,   0x2f,   0x300,  0xad,   0xa,     0x9,    0xc,    0x2028, 0x2029, 0x28,   0x29,
      0x3c,   0x3e,   0x5b,   0x5d,   0x7b,   0x7d,   0xab,   0xbb,    0x2018, 0x2019, 0x202a, 0x202b, 0x202c, 0x202d,
      0x202e, 0x2066, 0x2067, 0x2068, 0x2069, 0x4e00, 0xac00, 0x1f600, 0xfffd, 0xffff, 0};
  for (uint32_t run = 0; run < 1000000; ++run) {
    if (run == 500000) state = 0xd1ff202610270001ULL;
    std::string text;
    const size_t length = random32() % (run % 32 == 0 ? 140 : 40);
    for (size_t i = 0; i < length; ++i) {
      const uint32_t choice = random32();
      if (choice % 31 == 0)
        text += static_cast<char>(random32());
      else if (choice % 17 == 0)
        utf8AppendCodepoint(random32() % 0x110000, text);
      else
        utf8AppendCodepoint(pool[choice % std::size(pool)], text);
    }
    check(text, int(random32() % 5) - 2, int(random32() % 70) - 3, run % 128 == 0);
  }
  std::cout << "PASS " << cases << " cases (" << corpusCases
            << " corpus/boundary, 1000000 fuzz), exhaustive classes/mirrors/marks; workspace "
            << xp_bidi_workspace_size() << " bytes\n";
}
