#include <Utf8.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "Epub/Epub/CjkBreaks.h"
#include "Epub/Epub/TokenBoundary.h"

static void hex(const std::string& s) {
  constexpr char digits[] = "0123456789abcdef";
  for (unsigned char c : s) std::cout << digits[c >> 4] << digits[c & 15];
  std::cout << ':';
}
static bool absorbed(uint32_t n, void* p) {
  auto& offsets = *static_cast<std::vector<uint32_t>*>(p);
  offsets.push_back(n);
  return true;
}
static bool reject(uint32_t, void*) { return false; }
static std::array<int64_t, 4> properties(uint32_t cp) {
  uint32_t flags = utf8IsCjkBreakable(cp) | (utf8IsHangul(cp) << 1) |
      (utf8IsCombiningMark(cp) << 2) | (TokenBoundary::allowsBreakAfterExplicitHyphen(cp) << 3);
  for (uint32_t other : {0x41u, 0x4e00u, 0xac00u, 0x2du, 0x3008u, 0x3009u, 0x0301u}) {
    flags = (flags << 4) | CjkBreaks::boundaryAllowsBreak(cp, other) |
        (CjkBreaks::boundaryAllowsBreak(other, cp) << 1) |
        (CjkBreaks::hasBreakOpportunityBetween(cp, other) << 2) |
        (CjkBreaks::hasBreakOpportunityBetween(other, cp) << 3);
  }
  return {flags, int64_t(utf8SmallCapsUpper(cp)) - cp, utf8DecomposedBase(cp), 0};
}
int main(int argc, char**) {
  if (argc == 1) {
    uint32_t start = 0;
    auto previous = properties(0);
    for (uint32_t cp = 1; cp <= 0x110000; ++cp) {
      auto current = cp == 0x110000 ? std::array<int64_t,4>{-1,-1,-1,-1} : properties(cp);
      if (current != previous) {
        std::cout << start << ' ' << cp - 1;
        for (auto v : previous) std::cout << ' ' << v;
        std::cout << '\n';
        start = cp;
        previous = current;
      }
    }
    uint64_t hash = 14695981039346656037ULL;
    auto mix = [&](uint32_t value) {
      for (int i = 0; i < 4; ++i) { hash ^= (value >> (8*i)) & 255; hash *= 1099511628211ULL; }
    };
    for (uint32_t cp = 0; cp <= 0x10ffff; ++cp) {
      char bytes[5]{};
      auto end = utf8AppendCodepoint(bytes, cp);
      std::string out;
      utf8AppendCodepoint(cp, out);
      if (out != std::string(bytes, end)) return 2;
      for (auto c : out) mix(static_cast<unsigned char>(c));
      auto ptr = reinterpret_cast<const unsigned char*>(bytes);
      mix(utf8NextCodepoint(&ptr));
      mix(ptr - reinterpret_cast<const unsigned char*>(bytes));
    }
    std::cout << "ENCODE " << hash << '\n';
    return 0;
  }
  std::string line;
  while (std::getline(std::cin, line)) {
    std::string input;
    for (size_t i = 0; i < line.size(); i += 2)
      input.push_back(static_cast<char>(std::stoul(line.substr(i, 2), nullptr, 16)));
    std::vector<uint32_t> offsets;
    hex(utf8ComposeNfc(input, absorbed, &offsets));
    for (auto n : offsets) std::cout << n << ',';
    std::cout << ':';
    hex(utf8ComposeNfc(input, reject));
    auto inplace = input;
    inplace.push_back(0);
    utf8ComposeNfcInPlace(inplace.data());
    hex(std::string(inplace.c_str()));
    auto p = reinterpret_cast<const unsigned char*>(input.c_str());
    auto begin = p;
    uint32_t previous = 0;
    while (*p) {
      auto cp = utf8NextCodepoint(&p);
      std::cout << cp << ',' << p - begin << ',' << CjkBreaks::boundaryAllowsBreak(previous, cp)
                << CjkBreaks::hasBreakOpportunityBetween(previous, cp) << ';';
      previous = cp;
    }
    std::cout << ':';
    for (auto n : CjkBreaks::characterBreakByteOffsets(input)) std::cout << n << ',';
    std::cout << ':';
    // Exercise every prefix, including incomplete sequences at byte zero.
    for (size_t n = 0; n <= input.size(); ++n) std::cout << utf8SafeTruncateBuffer(input.data(), n) << ',';
    std::cout << ':';
    auto removed = input;
    while (!removed.empty()) std::cout << utf8RemoveLastChar(removed) << ',';
    std::cout << ':';
    auto truncated = input;
    utf8TruncateChars(truncated, 2);
    hex(truncated);
    std::cout << '\n';
  }
}
