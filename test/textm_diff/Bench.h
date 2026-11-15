#pragma once
#include <chrono>
#include <cstring>
#include <iostream>
#include <numeric>

#include "Callback.h"
inline int benchmark(bool rust, GfxRenderer& renderer, int loops) {
  Callback callback{&renderer.fontMap.at(1), nullptr};
  auto face = callback.face();
  constexpr const char* word = "office";
  auto cps = codepoints(word);
  volatile uint64_t checksum = 0;
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < loops; ++i) {
    int value = rust ? xp_textm_advance(&face, cps.data(), cps.size(), 0, 0, 1)
                     : renderer.getTextAdvanceX(1, word, EpdFontFamily::REGULAR, 0, BidiUtils::BidiBaseDir::LTR,
                                                GfxRenderer::TextMeasureMode::Layout);
    checksum = checksum + value;
  }
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
  std::cout << (rust ? "rust" : "cpp") << " word=office loops=" << loops << " ns/word=" << double(ns) / loops
            << " checksum=" << checksum << " callbacks/word="
            << double(std::accumulate(callback.calls.begin(), callback.calls.end(), uint64_t(0))) / loops << '\n';
  for (auto n : callback.calls) std::cout << double(n) / loops << ' ';
  std::cout << '\n';
  return 0;
}
