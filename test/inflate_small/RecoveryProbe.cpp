#include <Arduino.h>
#include <DictZip.h>
#include <DictionaryIo.h>
#include <FontDecompressor.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// Set the resolved dictionary path without exercising unrelated index discovery.
#define private public
#include <Dictionary.h>
#undef private
#include "DictionaryReadDefinition.inc"

static void require(bool condition) {
  if (!condition) {
    std::fputs("Inflate recovery check failed\n", stderr);
    std::exit(1);
  }
}

static void dictionaryRecovery(const char* path) {
  Dictionary dictionary;
  dictionary.basePath = std::string(path).substr(0, std::strlen(path) - std::strlen(".dict.dz"));
  const DictLocation location{0, 2048, true, false};
  std::string definition = "previous definition";
  Dictionary::LookupResult result{};
  // Skip the header/table and footer so this is a refill error during inflate.
  readFault = {path, 24, 24 + 2053, 24 + 700, 37};
  require(!dictionary.readDefinition(location, definition, &result));
  require(result == Dictionary::LookupResult::ReadError);
  require(readFault.shortReads > 1 && readFault.failures == 1);
  require(Storage.temporary.size() == 512);
  require(Storage.temporaryReads == 0 && definition == "previous definition");
  for (size_t i = 0; i < Storage.temporary.size(); ++i) require(Storage.temporary[i] == uint8_t(i));

  readFault = {};
  require(dictionary.readDefinition(location, definition, &result));
  require(Storage.truncations == 2 && Storage.temporaryReads == 1);
  require(definition.size() == location.size && Storage.temporary.size() == location.size);
  for (size_t i = 0; i < definition.size(); ++i) require(uint8_t(definition[i]) == uint8_t(i));
  std::puts("DictZip: short reads, incomplete slice ReadError, no publication, next read byte-identical");
}

static void inlineIfoAndDefinition() {
  HalFile ifo;
  ifo.markOpen();
  const auto readIfo = [&](const std::string& text) {
    ifo.bytes.assign(text.begin(), text.end());
    ifo.cursor = 0;
    const auto file = dictionaryFile(ifo);
    return xp_dictionary_ifo(&file);
  };
  const std::string prefix = "description=" + std::string(4096, 'z') + "\n";
  require(readIfo("") == 0);
  require(readIfo(prefix + "idxoffsetbits= +064other\r\nsametypesequence=x") ==
          (XP_DICTIONARY_IFO_OFFSETS64 | XP_DICTIONARY_IFO_HTML));
  require(readIfo(prefix + "sametypesequence=h\r\nsametypesequence=mh\n") == 0);
  require(readIfo(prefix + "sametypesequence=\r\n") == (XP_DICTIONARY_IFO_INLINE | XP_DICTIONARY_IFO_HTML));
  require(readIfo(prefix + "other=sametypesequence=x\n") == (XP_DICTIONARY_IFO_INLINE | XP_DICTIONARY_IFO_HTML));
  ifo.path = "/memory.ifo";
  readFault = {ifo.path, 128, 512, 256, 32};
  require(readIfo(prefix + "sametypesequence=h\n") == 0);
  readFault = {};

  Dictionary dictionary;
  dictionary.basePath = "/inline";
  dictionary.hasPlainDict = dictionary.htmlDefinitions = dictionary.inlineTypes = true;
  Storage.temporaryPath = "/inline.dict";
  auto lookup = [&](std::string_view raw, std::string& out, bool append, size_t advertised = 0,
                    bool* exceeds = nullptr) {
    Storage.temporary.assign(raw.begin(), raw.end());
    auto result = Dictionary::LookupResult::Found;
    const DictLocation location{0, static_cast<uint32_t>(advertised ? advertised : raw.size()), true, false};
    dictionary.readDefinition(location, out, &result, append, exceeds);
    return result;
  };
  using Result = Dictionary::LookupResult;
  using namespace std::string_view_literals;
  std::string out = "label:";
  require(lookup("mhi &<>\0Pb\0\0\0q"sv, out, true) == Result::ReadError);
  require(out == "label:");
  require(lookup("mhi &<>\0P\0\0\0\2qw"
                 "h<b>bold</b>\0x<i>ita</i>\0"sv,
                 out, true) == Result::Found);
  require(out == "label:<pre>hi &amp;&lt;&gt;</pre><b>bold</b><i>ita</i>");
  require(lookup("h<b>a</b>\0P\0\0\0\1z"
                 "x<i>b</i>\0"sv,
                 out, false) == Result::Found);
  require(out == "<b>a</b><i>b</i>");
  require(lookup("P\0\0\0\1zm\0"sv, out, false) == Result::NotFound);
  require(lookup("munterminated"sv, out, false) == Result::ReadError && out.empty());
  require(lookup("1invalid"sv, out, false) == Result::ReadError && out.empty());

  const std::string escaped = "m" + std::string(15000, '&') + '\0';
  bool capped = false;
  out = "label";
  require(lookup(escaped, out, true, 0, &capped) == Result::ReadError && capped && out == "label");
  require(lookup(escaped, out, false, 0, &capped) == Result::Found && !capped && out.size() == 75011);
  const std::string clipped = "m" + std::string(65535, 'a');
  require(lookup(clipped, out, false, clipped.size() + 1) == Result::Found);
  require(out.size() == 65546 && out.starts_with("<pre>a") && out.ends_with("a</pre>"));
  std::puts("StarDict: full IFO scan, markup compaction, escaped text, binary skip, cap and malformed fields");
}

static void fontRecovery() {
  // The first stored block ends halfway through its declared 16-byte bitmap.
  const uint8_t bitmap[] = {1,   16, 0, 239, 255, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 1,  16, 0,  239,
                            255, 0,  1, 2,   3,   4,    5,    6,    7,    8,    9,    10,   11,   12, 13, 14, 15};
  const EpdGlyph glyphs[] = {{4, 16, 0, 0, 0, 16, 0}, {4, 16, 0, 0, 0, 16, 0}};
  const EpdFontGroup groups[] = {{0, 13, 16, 1, 0}, {13, 21, 16, 1, 1}};
  EpdFontData font{};
  font.bitmap = bitmap;
  font.glyph = glyphs;
  font.groups = groups;
  font.groupCount = 2;
  font.is2Bit = true;
  FontDecompressor reader;
  require(reader.init());
  const auto checkValid = [&] {
    const uint8_t* output = reader.getBitmap(&font, &glyphs[1], 1);
    require(output && std::memcmp(output, bitmap + 18, 16) == 0);
  };
  checkValid();
  require(reader.getBitmap(&font, &glyphs[0], 0) == nullptr);
  require(reader.getBitmap(&font, &glyphs[0], 0) == nullptr);
  checkValid();
  require(reader.getStats().cacheMisses == 4);
  checkValid();
  require(reader.getStats().cacheHits == 1);
  std::puts("FontDecompressor: failed group unpublished, reused reader valid group byte-identical");
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  dictionaryRecovery(argv[1]);
  inlineIfoAndDefinition();
  fontRecovery();
}
