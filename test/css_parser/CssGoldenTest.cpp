#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "CssParser.h"
#include "OracleWire.h"

namespace {
namespace fs = std::filesystem;
std::string read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), {}};
}
std::vector<std::string> split(const std::string& text, char delimiter) {
  std::vector<std::string> result;
  size_t begin = 0;
  do {
    const auto end = text.find(delimiter, begin);
    result.push_back(text.substr(begin, end - begin));
    if (end == std::string::npos) break;
    begin = end + 1;
  } while (true);
  return result;
}
void append(std::string& bytes, const CssStyle& style) {
  uint8_t wire[css_oracle::STYLE_WIRE_BYTES];
  css_oracle::encodeStyleWire(style, wire);
  bytes.append(reinterpret_cast<const char*>(wire), sizeof(wire));
}
void check(const fs::path& path, const std::string& bytes) {
  if (std::getenv("CSS_CAPTURE_ORACLE")) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(file.good()) << path;
  } else {
    ASSERT_TRUE(fs::exists(path)) << path;
    EXPECT_EQ(read(path), bytes) << path;
  }
}
TEST(CssGoldenTest, CurrentCppOracle) {
  const char* overridePath = std::getenv("CSS_GOLDEN_DIR");
  const fs::path root = overridePath ? fs::path(overridePath) : fs::path(CSS_GOLDEN_DIR);
  const fs::path scratch = fs::temp_directory_path() / "crosspoint-css-golden";
  fs::create_directories(scratch);
  std::istringstream cases(read(root / "cases.txt"));
  std::string name;
  while (std::getline(cases, name)) {
    SCOPED_TRACE(name);
    const auto dir = root / name;
    CssParser parser(scratch.string());
    std::string metadata;
    bool complete = true;
    for (unsigned i = 0;; ++i) {
      const auto path = dir / ((i < 10 ? "0" : "") + std::to_string(i) + ".css");
      if (!fs::exists(path)) break;
      HalFile source;
      ASSERT_TRUE(source.open(path.c_str(), "rb"));
      const auto result = parser.loadFromStream(source);
      complete = complete && result == CssParser::ParseResult::Complete;
      metadata += std::to_string(static_cast<unsigned>(result)) + " " + std::to_string(parser.ruleCount()) + "\n";
    }
    metadata += std::to_string(parser.hasFirstLetterRules()) + "\n";
    check(dir / "result.txt", metadata);
    ASSERT_TRUE(parser.saveToCache(complete));
    check(dir / "cache.bin", read(scratch / "css_rules.cache"));
    const auto resolve = [&](const CssParser& instance) {
      std::string bytes;
      std::istringstream queries(read(dir / "queries.tsv"));
      std::string line;
      while (std::getline(queries, line)) {
        const auto fields = split(line, '\t');
        EXPECT_EQ(fields.size(), 4u);
        if (fields.size() != 4) continue;
        std::vector<CssAncestor> ancestors;
        if (!fields[3].empty()) {
          for (const auto& ancestor : split(fields[3], '|')) {
            const auto parts = split(ancestor, '~');
            EXPECT_EQ(parts.size(), 3u);
            if (parts.size() == 3) ancestors.push_back(CssParser::makeAncestor(parts[0], parts[1], parts[2]));
          }
        }
        for (const bool first : {false, true}) {
          append(bytes,
                 instance.resolveStyle(fields[0], fields[1], fields[2], ancestors.data(), ancestors.size(), first));
        }
      }
      return bytes;
    };
    const auto resolved = resolve(parser);
    check(dir / "resolved.bin", resolved);
    CssParser loaded(scratch.string());
    ASSERT_EQ(loaded.loadFromCache(), CssParser::CacheLoadResult::Complete);
    EXPECT_EQ(resolve(loaded), resolved);
    std::string inlineBytes;
    std::istringstream declarations(read(dir / "inline.txt"));
    std::string line;
    while (std::getline(declarations, line)) append(inlineBytes, CssParser::parseInlineStyle(line));
    check(dir / "inline.bin", inlineBytes);
  }
  fs::remove_all(scratch);
}
}  // namespace
