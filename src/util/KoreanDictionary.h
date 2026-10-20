#pragma once

#include <string>
#include <string_view>

namespace KoreanDictionary {

// Regular past-tense base form of one precomposed-Hangul word (먹었다 -> 먹다,
// 말했다 -> 말하다), or empty when no rule applies. Stems whose base form is
// ambiguous are left alone: ㄹ-final stems (들었다: 들다 or 듣다) and the
// syllables that can hide a dropped ㅅ (지었다: 짓다 or 지다). Particles,
// other endings, and irregular verbs are not handled.
std::string baseForm(std::string_view word);

}  // namespace KoreanDictionary
