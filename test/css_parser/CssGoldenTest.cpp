#include <gtest/gtest.h>
#include <xp_ffi.h>

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

TEST(CssGoldenTest, ValidatedInlineViewMatchesNativeFields) {
  const std::string text =
      "text-align:right;font-style:italic;font-weight:bold;text-decoration:underline line-through;"
      "direction:rtl;text-indent:2px;margin:1px 2em 3rem 4pt;padding:5px 6em 7rem 8pt;"
      "height:12px;width:25%;font-size:2em;display:none;vertical-align:sub;list-style-type:none;"
      "font-variant:small-caps;page-break-before:always;page-break-after:always;border:2px dashed;"
      "background-color:gray;float:left;initial-letter:3;white-space:pre-wrap";
  XpCssStyleView view{};
  ASSERT_EQ(xp_css_inline(reinterpret_cast<const uint8_t*>(text.data()), text.size(), &view), 1);
  const auto native = CssParser::parseInlineStyle(text);
  uint8_t wire[css_oracle::STYLE_WIRE_BYTES];
  css_oracle::encodeStyleWire(native, wire);
  uint32_t flags;
  memcpy(&flags, wire + 84, sizeof(flags));
  EXPECT_EQ(view.defined_bits, flags);
  for (size_t i = 0; i < css_oracle::STYLE_LENGTH_FIELDS.size(); ++i) {
    const auto& length = native.*css_oracle::STYLE_LENGTH_FIELDS[i];
    EXPECT_EQ(view.lengths[i], length.value);
    EXPECT_EQ(view.units[i], static_cast<uint8_t>(length.unit));
  }
  EXPECT_EQ(view.text_align, static_cast<uint8_t>(native.textAlign));
  EXPECT_EQ(view.font_style, static_cast<uint8_t>(native.fontStyle));
  EXPECT_EQ(view.font_weight, static_cast<uint8_t>(native.fontWeight));
  EXPECT_EQ(view.text_decoration, static_cast<uint8_t>(native.textDecoration));
  EXPECT_EQ(view.direction, static_cast<uint8_t>(native.direction));
  EXPECT_EQ(view.display, static_cast<uint8_t>(native.display));
  EXPECT_EQ(view.vertical_align, static_cast<uint8_t>(native.verticalAlign));
  EXPECT_EQ(view.list_style_type, static_cast<uint8_t>(native.listStyleType));
  EXPECT_EQ(view.small_caps, native.smallCaps);
  EXPECT_EQ(view.page_break_before, native.pageBreakBefore);
  EXPECT_EQ(view.page_break_after, native.pageBreakAfter);
  for (size_t i = 0; i < 8; ++i) EXPECT_EQ(view.borders[i], wire[71 + i]);
  EXPECT_EQ(view.border_components, wire[79]);
  EXPECT_EQ(view.shaded, native.shaded);
  EXPECT_EQ(view.float_left, native.floatLeft);
  EXPECT_EQ(view.initial_letter, native.initialLetter);
  EXPECT_EQ(view.preserve_whitespace, native.preserveWhitespace);
}
