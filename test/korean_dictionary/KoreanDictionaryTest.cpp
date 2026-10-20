#include <gtest/gtest.h>

#include "KoreanDictionary.h"

TEST(KoreanDictionary, RegularPastTense) {
  const struct {
    const char* word;
    const char* expected;
  } cases[] = {{"먹었다", "먹다"}, {"찾았다", "찾다"}, {"말했다", "말하다"},
               {"했다", "하다"},   {"있었다", "있다"}, {"쳐다보았다", "쳐다보다"}};
  for (const auto& c : cases) {
    SCOPED_TRACE(c.word);
    EXPECT_EQ(KoreanDictionary::baseForm(c.word), c.expected);
  }
}

TEST(KoreanDictionary, RejectsAmbiguousOrUnsupportedForms) {
  const char* const words[] = {
      // Ambiguous base form: ㄹ-final stem, or a stem syllable that can hide ㅅ.
      "들었다",
      "알았다",
      "열었다",
      "지었다",
      "줄지었다",
      // Not a handled past-tense form.
      "먹다",
      "먹어요",
      "학생은",
      "었다",
      "",
      // Not a single precomposed-Hangul word.
      "Decomposing",
      "ABC먹었다",
      "밥을 먹었다",
      "\xE1\x84\x86\xE1\x85\xA5\xE1\x86\xA8었다",  // decomposed jamo 먹
      "\xEA\xB0\x7F었다",                          // bad continuation byte
      "\xED\xA0\x80었다",                          // surrogate
  };
  for (const char* word : words) {
    SCOPED_TRACE(word);
    EXPECT_TRUE(KoreanDictionary::baseForm(word).empty());
  }
}
