#include <gtest/gtest.h>
#include <xp_ffi.h>
#include <zlib.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>

#include "Epub/hyphenation/Hyphenator.h"

TEST(HyphenationGolden, LanguageTags) {
  std::ifstream input(std::string(HYPHENATION_GOLDEN_DIR) + "/golden-tags.txt");
  ASSERT_TRUE(input);
  std::string line;
  size_t count = 0;
  while (std::getline(input, line)) {
    if (line.starts_with('#')) continue;
    std::istringstream fields(line);
    std::string tag;
    int id;
    ASSERT_TRUE(fields >> tag >> id);
    if (tag == "-") tag.clear();
    EXPECT_EQ(xp_hyphen_language(reinterpret_cast<const uint8_t*>(tag.data()), tag.size()), id) << tag;
    ++count;
  }
  EXPECT_EQ(count, 31u);
}

TEST(HyphenationGolden, ExactCppOutputs) {
  const char* full = std::getenv("XP_HYPHEN_GOLDEN_FULL");
  const std::string root = full ? full : HYPHENATION_GOLDEN_DIR;
  std::ifstream input(root + "/golden-words.txt");
  ASSERT_TRUE(input);
  std::vector<std::string> words;
  std::string hex;
  while (std::getline(input, hex)) {
    if (hex.starts_with('#')) continue;
    ASSERT_TRUE(hex == "-" || hex.size() % 2 == 0);
    std::string word;
    if (hex != "-") {
      for (size_t i = 0; i < hex.size(); i += 2) {
        word.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
      }
    }
    words.push_back(word);
  }
  ASSERT_FALSE(words.empty());
  const auto closeGolden = [](gzFile file) { gzclose(file); };
  const std::unique_ptr<gzFile_s, decltype(closeGolden)> golden(
      gzopen((root + (full ? "/golden-breaks.txt.gz" : "/golden-breaks.txt")).c_str(), "rb"), closeGolden);
  ASSERT_TRUE(golden);
  std::string line;
  size_t count = 0;
  // The longest captured row is below 2 KB; fail rather than truncate a future row.
  char buffer[4096];
  while (gzgets(golden.get(), buffer, sizeof(buffer))) {
    line = buffer;
    ASSERT_TRUE(line.ends_with('\n'));
    line.pop_back();
    if (line.starts_with('#')) continue;
    std::istringstream fields(line);
    std::string tag;
    int fallback;
    size_t index;
    ASSERT_TRUE(fields >> tag >> fallback >> index);
    ASSERT_LT(index, words.size());
    ASSERT_EQ(index, count % words.size());
    Hyphenator::setPreferredLanguage(tag == "-" ? "" : tag);
    const auto actual = Hyphenator::breakOffsets(words[index], fallback != 0);
    std::ostringstream result;
    result << tag << ' ' << fallback << ' ' << index;
    for (const auto& br : actual) result << ' ' << br.byteOffset << ':' << br.requiresInsertedHyphen;
    ASSERT_EQ(result.str(), line);
    ++count;
  }
  EXPECT_TRUE(gzeof(golden.get()));
  EXPECT_EQ(count, words.size() * 2 * (full ? 31u : 12u));
}

TEST(HyphenationAbi, CapacityAndInvalidArguments) {
  const std::string word(200, 'a');
  XpBreak output[200];
  for (auto& br : output) br = {123, 42, {7, 8, 9}};
  size_t needed = 777;
  const auto* bytes = reinterpret_cast<const uint8_t*>(word.data());
  ASSERT_EQ(xp_hyphen_breaks(-1, bytes, word.size(), 1, output, 1, &needed), 1);
  EXPECT_EQ(needed, 197u);
  for (const auto& br : output) {
    EXPECT_EQ(br.byte_offset, 123u);
    EXPECT_EQ(br.insert_hyphen, 42u);
    EXPECT_EQ(br.pad[2], 9u);
  }
  EXPECT_EQ(xp_hyphen_breaks(-1, bytes, word.size(), 1, nullptr, 0, &needed), 1);
  EXPECT_EQ(xp_hyphen_breaks(-1, bytes, word.size(), 1, output, 200, &needed), 0);
  EXPECT_EQ(needed, 197u);
  EXPECT_EQ(output[0].byte_offset, 2u);
  EXPECT_EQ(output[196].byte_offset, 198u);
  EXPECT_EQ(output[0].pad[0], 0u);
  EXPECT_EQ(xp_hyphen_breaks(11, bytes, word.size(), 1, output, 200, &needed), -1);
  EXPECT_EQ(xp_hyphen_breaks(-1, nullptr, 1, 1, output, 200, &needed), -1);
  EXPECT_EQ(xp_hyphen_breaks(-1, bytes, word.size(), 2, output, 200, &needed), -1);
  EXPECT_EQ(xp_hyphen_breaks(-1, bytes, word.size(), 1, output, 200, nullptr), -1);
  EXPECT_EQ(xp_hyphen_breaks(-1, nullptr, 0, 0, nullptr, 0, &needed), 0);
  EXPECT_EQ(needed, 0u);
  if (sizeof(size_t) > sizeof(uint32_t)) {
    EXPECT_EQ(xp_hyphen_breaks(-1, bytes, static_cast<size_t>(UINT32_MAX) + 1, 1, output, 200, &needed), -1);
  }
}
