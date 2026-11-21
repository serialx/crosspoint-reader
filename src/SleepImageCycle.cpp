#include "SleepImageCycle.h"

#include <Arduino.h>

#include <algorithm>

uint16_t SleepImageCycle::next(const uint16_t fileCount) {
  constexpr uint8_t LOOKBACK = 5;
  uint16_t randomFileIndex = 0;

  // Hull-Dobell theorem conditions for full period over mod 2^k: odd increment c and multiplier a = 5
  // (a - 1 divisible by 4) give period m for any seed; m = 1 is trivial.
  // https://en.wikipedia.org/wiki/Linear_congruential_generator#Period_length
  // Cycle-walking skips states >= fileCount, visiting every image once per cycle and finding a valid index
  // within at most lcgM steps.
  uint16_t lcgIndexesLeft = count >> 16;
  uint16_t lcgIndexesTotal = count & 0xffff;
  uint32_t lcgSeed = state >> 16;
  uint32_t lcgState = state & 0xffff;
  uint32_t lcgM = 1;

  while (lcgM < fileCount) {
    lcgM <<= 1;
  }

  uint32_t lcgC = (lcgSeed * 2u + 1u) % lcgM;

  if (lcgIndexesLeft == 0 || lcgIndexesTotal != fileCount || lcgIndexesLeft > fileCount || lcgState >= lcgM) {
    uint16_t previousIndexes[LOOKBACK];
    uint8_t previousCount = 0;
    const uint8_t window = static_cast<uint8_t>(std::min<uint16_t>(fileCount / 2, LOOKBACK));
    if (lcgIndexesLeft == 0 && lcgIndexesTotal == fileCount && lcgState < fileCount) {
      // Invert the completed cycle (5 * 0xCCCCCCCD = 1 mod 2^32) to obtain recently shown image indexes
      static constexpr uint32_t LCG_MULTIPLIER_INVERSE = 0xCCCCCCCDu;
      uint32_t previousState = lcgState;
      for (uint32_t step = 0; step < lcgM && previousCount < window; ++step) {
        if (previousState < fileCount) previousIndexes[previousCount++] = static_cast<uint16_t>(previousState);
        previousState = (LCG_MULTIPLIER_INVERSE * (previousState - lcgC)) & (lcgM - 1u);
      }
    }

    lcgIndexesLeft = lcgIndexesTotal = fileCount;
    const auto isPreviousIndex = [&](uint32_t index) {
      return std::find(previousIndexes, previousIndexes + previousCount, index) != previousIndexes + previousCount;
    };

    // Find a new seed that
    // 1. doesn't immediately repeat an image
    // 2. has the fewest overlaps with previousIndexes in the remaining first "window" generations
    // 3. ideally isn't the same seed as the previous one
    uint8_t bestOverlapCount = window + 1;
    const uint32_t prevLcgSeed = lcgSeed;
    static constexpr uint8_t MAX_SEED_ATTEMPTS = 16;
    for (uint8_t attempt = 0; attempt < MAX_SEED_ATTEMPTS; ++attempt) {
      const uint32_t candidateSeed = static_cast<uint32_t>(random(lcgM));
      const uint32_t candidateC = (candidateSeed * 2u + 1u) % lcgM;
      uint32_t candidateState = candidateSeed;
      for (uint32_t step = 0; step < lcgM; ++step) {
        candidateState = (5u * candidateState + candidateC) % lcgM;
        if (candidateState < fileCount && !isPreviousIndex(candidateState)) break;
      }

      const uint32_t firstState = candidateState;
      uint8_t overlapCount = 0;

      if (previousCount > 0 && candidateSeed == prevLcgSeed) {
        // assume some overlaps for the previous seed to penalize it for selection.
        overlapCount = window > 1 ? window / 2 : 1;
      } else {
        uint8_t generatedCount = 1;
        for (uint32_t step = 0; step < lcgM && generatedCount < window && previousCount > 0; ++step) {
          candidateState = (5u * candidateState + candidateC) % lcgM;
          if (candidateState >= fileCount) continue;
          ++generatedCount;
          if (isPreviousIndex(candidateState)) ++overlapCount;
        }
      }

      if (overlapCount < bestOverlapCount) {
        bestOverlapCount = overlapCount;
        lcgSeed = candidateSeed;
        lcgState = firstState;
      }
      if (overlapCount == 0) break;
    }
    randomFileIndex = static_cast<uint16_t>(lcgState);
  } else {
    for (uint32_t attempt = 0; attempt < lcgM; ++attempt) {
      lcgState = (5u * lcgState + lcgC) % lcgM;
      if (lcgState < fileCount) {
        randomFileIndex = lcgState;
        break;
      }
    }
  }

  count = (static_cast<uint32_t>(lcgIndexesLeft - 1) << 16) | lcgIndexesTotal;
  state = (lcgSeed << 16) | (lcgState & 0xffff);
  return randomFileIndex;
}
