#include <Arduino.h>
#include <DictZip.h>
#include <Dictionary.h>
#include <DictionaryRegistry.h>
#include <HalStorage.h>
#include <Memory.h>

#include <cstdio>
#include <string>
#include <vector>

bool normalize(const std::string&, HalFile&);
void put(const std::string& path, const std::string& data) { files[path].bytes.assign(data.begin(), data.end()); }
void dirs(const std::string& folder) {
  files["/dictionaries"].directory = true;
  files["/dictionaries/" + folder].directory = true;
}
void be(std::string& out, uint32_t n) {
  for (int i = 3; i >= 0; --i) out += char(n >> (8 * i));
}
void entry(std::string& out, const std::string& word, uint32_t offset, uint32_t size) {
  out += word;
  out += '\0';
  be(out, offset);
  be(out, size);
}
void synonym(std::string& out, const std::string& word, uint32_t ordinal) {
  out += word;
  out += '\0';
  be(out, ordinal);
}
void bytes(const std::string& label, const std::string& value) {
  printf("%s %zu ", label.c_str(), value.size());
  // FNV preserves a compact oracle for large outputs; short bytes are also printed.
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : value) hash = (hash ^ c) * 1099511628211ULL;
  printf("%016llx", static_cast<unsigned long long>(hash));
  if (value.size() < 300)
    for (unsigned char c : value) printf("%02x", c);
  puts("");
}
void stored(const char* path) {
  auto it = files.find(path);
  if (it == files.end()) {
    printf("absent %s\n", path);
    return;
  }
  const auto& b = it->second.bytes;
  bytes(path, std::string(b.begin(), b.end()));
}
void lookup(Dictionary& d, const char* word) {
  std::string text = "old", matched = "old";
  Dictionary::LookupResult r{};
  bool ok = d.lookup(word, text, matched, &r);
  printf("lookup %s %d %d\n", word, ok, int(r));
  bytes("matched", matched);
  bytes("definition", text);
}
void fixture(bool html = false) {
  files.clear();
  failOpen.clear();
  failSeek.clear();
  writeLimit = readLimit = allocationLimit = -1;
  heapFree = heapMax = 1000000;
  dirs("Demo");
  std::string idx, syn;
  entry(idx, "apple", 0, 5);
  entry(idx, "apple", 5, 4);
  entry(idx, "box", 9, 3);
  entry(idx, "dog", 12, 3);
  entry(idx, "love", 15, 4);
  entry(idx, "make", 19, 4);
  entry(idx, "run", 23, 3);
  entry(idx, "stop", 26, 4);
  entry(idx, "story", 30, 5);
  synonym(syn, "alias", 0);
  synonym(syn, "alias", 1);
  synonym(syn, "alias", 0);
  synonym(syn, "alias", 8);
  synonym(syn, "broken", 999);
  put("/dictionaries/Demo/test.idx", idx);
  put("/dictionaries/Demo/test.syn", syn);
  put("/dictionaries/Demo/test.dict", "firstlastboxdoglovemakerunstopstory");
  put("/dictionaries/Demo/test.ifo", html ? "sametypesequence=h\n" : "sametypesequence=m\n");
}
void index(Dictionary& d) {
  Dictionary::IndexResult r{};
  printf("index %d %d\n", d.buildIndex(nullptr, nullptr, &r), int(r));
}
void le(std::string& out, uint32_t n, int count) {
  for (int i = 0; i < count; ++i) out += char(n >> (8 * i));
}
std::string dictzip() {
  std::string out("\x1f\x8b\x08\x04", 4);
  out.append(6, '\0');
  le(out, 16, 2);
  out += "RA";
  le(out, 12, 2);
  le(out, 1, 2);
  le(out, 512, 2);
  le(out, 3, 2);
  for (int i = 0; i < 3; ++i) le(out, 517, 2);
  for (int i = 0; i < 3; ++i) {
    out += char(1);
    le(out, 512, 2);
    le(out, 65535 - 512, 2);
    out.append(512, char('a' + i));
  }
  le(out, 0, 4);
  le(out, 1536, 4);
  return out;
}
void zipcase(const std::string& input) {
  put("/zip", input);
  auto f = Storage.open("/zip");
  DictZip::Info info;
  DictZip::ExtractError error;
  bool ok = DictZip::parse(f, &info, &error);
  printf("zip-parse %d %d %u %u %u %d %zu", ok, int(error), info.dataOffset, info.totalSize, info.chunkLength,
         info.valid, info.chunkOffsets.size());
  for (auto n : info.chunkOffsets) printf(" %u", n);
  puts("");
  for (auto range : {std::pair<uint32_t, uint32_t>{0, 0}, {0, 1}, {500, 40}, {1024, 512}, {1535, 2}, {0xffffffff, 2}}) {
    auto out = Storage.open("/zipout", O_CREAT | O_TRUNC);
    ok = DictZip::extractEntry("/zip", range.first, range.second, out, &error);
    printf("zip-extract %u %u %d %d\n", range.first, range.second, ok, int(error));
    stored("/zipout");
  }
}
int main() {
  const auto encoded = dictzip();
  zipcase(encoded);
  for (size_t n = 0; n < 35; ++n) {
    printf("zip-truncate %zu\n", n);
    zipcase(encoded.substr(0, n));
  }
  for (size_t at : {0u, 2u, 3u, 10u, 14u, 16u, 18u, 20u, 22u, 28u, 29u, 30u, 31u, 32u}) {
    auto corrupt = encoded;
    corrupt[at] ^= 0xff;
    printf("zip-mutate %zu\n", at);
    zipcase(corrupt);
  }
  for (int limit : {0, 1, 2}) {
    allocationLimit = limit;
    printf("zip-alloc %d\n", limit);
    zipcase(encoded);
    allocationLimit = -1;
  }
  heapMax = 0;
  zipcase(encoded);
  heapMax = 1000000;

  for (bool html : {false, true}) {
    fixture(html);
    Dictionary d;
    printf("open %d\n", d.open("Demo"));
    printf("needs %d\n", d.needsIndex());
    index(d);
    stored("/dictionaries/Demo/test.qidx");
    stored("/dictionaries/Demo/test.sidx");
    printf("needs %d\n", d.needsIndex());
    for (auto w : {"APPLE", "apple", "app", "absent", "alias", "broken", "boxes", "dogs", "loved", "making", "running",
                   "stopped", "stories", "dog's", "dog’s", "「APPLE。」", "—apple!”"})
      lookup(d, w);
    files.erase("/dictionaries/Demo/test.sidx");
    lookup(d, "alias");
    lookup(d, "dogs");
    files.erase("/dictionaries/Demo/test.qidx");
    lookup(d, "apple");
    failSeek = "/dictionaries/Demo/test.idx";
    lookup(d, "apple");
    failSeek.clear();
    failOpen = "/dictionaries/Demo/test.idx";
    lookup(d, "apple");
    failOpen.clear();
    heapFree = 0;
    lookup(d, "apple");
    heapFree = 1000000;
    heapMax = 0;
    lookup(d, "apple");
    heapMax = 1000000;
  }
  // Sample boundary crossed by equal headwords and .syn ordinal resolution.
  fixture();
  std::string idx, data;
  for (unsigned i = 0; i < 600; ++i) {
    char word[32];
    snprintf(word, sizeof(word), "word%04u", i >= 250 && i < 270 ? 250 : i);
    entry(idx, word, i, 1);
    data += char('A' + i % 26);
  }
  put("/dictionaries/Demo/test.idx", idx);
  put("/dictionaries/Demo/test.dict", data);
  std::string syn;
  synonym(syn, "alias", 512);
  put("/dictionaries/Demo/test.syn", syn);
  {
    Dictionary d;
    d.open("Demo");
    index(d);
    stored("/dictionaries/Demo/test.qidx");
    lookup(d, "word0250");
    lookup(d, "word0599");
    lookup(d, "alias");
  }
  // Interrupted writes remove the output; a retry produces a valid byte-identical sidecar.
  for (int limit : {0, 1, 23, 24, 27, 28, 35, 51}) {
    fixture();
    Dictionary d;
    d.open("Demo");
    writeLimit = limit;
    printf("write-limit %d\n", limit);
    index(d);
    writeLimit = -1;
    stored("/dictionaries/Demo/test.qidx");
    printf("needs %d\n", d.needsIndex());
    index(d);
  }
  for (int limit : {0, 1, 9, 20}) {
    fixture();
    Dictionary d;
    d.open("Demo");
    readLimit = limit;
    printf("read-limit %d\n", limit);
    index(d);
    readLimit = -1;
    stored("/dictionaries/Demo/test.qidx");
  }
  for (int limit : {0, 1, 2}) {
    fixture();
    Dictionary d;
    allocationLimit = limit;
    printf("alloc-limit %d open %d\n", limit, d.open("Demo"));
    index(d);
    lookup(d, "apple");
    allocationLimit = -1;
  }
  for (size_t length = 0; length <= 28; ++length) {
    fixture();
    Dictionary d;
    d.open("Demo");
    index(d);
    auto& sidecar = files["/dictionaries/Demo/test.qidx"].bytes;
    sidecar[24] = 1;
    sidecar[25] = 0x42;
    sidecar.resize(length);
    printf("sidecar-truncate %zu\n", length);
    printf("needs %d\n", d.needsIndex());
    lookup(d, "apple");
  }
  // Short tails, stale/wrong-version headers, zero and oversized entries.
  for (size_t tail = 0; tail < 14; ++tail) {
    fixture();
    auto& b = files["/dictionaries/Demo/test.idx"].bytes;
    b.resize(tail);
    Dictionary d;
    d.open("Demo");
    index(d);
    lookup(d, "apple");
    stored("/dictionaries/Demo/test.qidx");
  }
  for (unsigned size : {0u, 65535u, 65536u, 65537u, 0xffffffffu}) {
    fixture();
    idx.clear();
    entry(idx, "apple", 0, size);
    put("/dictionaries/Demo/test.idx", idx);
    put("/dictionaries/Demo/test.dict", std::string(65536, 'x'));
    Dictionary d;
    d.open("Demo");
    lookup(d, "apple");
  }
  for (auto ifo : {"idxoffsetbits=64\n", "idxoffsetbits = 64\n", "sametypesequence=x\r\n", "sametypesequence=hm\n"}) {
    fixture();
    put("/dictionaries/Demo/test.ifo", ifo);
    Dictionary d;
    printf("ifo %s %d %d\n", ifo, d.open("Demo"), d.definitionsAreHtml());
  }
  fixture();
  dirs("Empty");
  dirs("Ambiguous");
  put("/dictionaries/Ambiguous/a.idx", "");
  put("/dictionaries/Ambiguous/b.idx", "");
  dirs("Metadata");
  put("/dictionaries/Metadata/._a.idx", "");
  files["/.dictionaries"].directory = true;
  files["/.dictionaries/hidden"].directory = true;
  put("/.dictionaries/hidden/x.idx", "");
  put("/.dictionaries/hidden/x.dict.dz", "");
  std::vector<DictionaryEntry> found;
  DictionaryRegistry::discover(found);
  for (const auto& e : found) printf("registry %s %s\n", e.name.c_str(), e.stem.c_str());
  for (auto folder : {"Demo", "hidden", ".Demo", "../Demo", "a/b", "a\\b", "", "Missing"}) {
    std::string out = "old";
    bool ok = DictionaryRegistry::resolveBasePath(folder, out);
    printf("resolve %s %d %s\n", folder, ok, out.c_str());
  }
  // Pure XHTML output, including tolerated malformed entities and long tag names.
  for (auto html :
       {"", "<P>Hello & world<BR>after</br></P>", "<!-- a --><?pi?><!doctype>text", "x < y &copy; &#ab; &#xFF; &#; &;",
        "<IMG alt='>'>hi", "<ABCDEFGHIJKLMNOPQRST X='1'>z</ABCDEFGHIJKLMNOPQRST>", "<p", "<!-- unfinished",
        "<b>unclosed", "<br />"}) {
    auto f = Storage.open("/html", O_CREAT | O_TRUNC);
    printf("html %d\n", normalize(html, f));
    stored("/html");
  }
  for (int limit : {0, 1, 127, 128, 129, 200}) {
    writeLimit = limit;
    auto f = Storage.open("/html", O_CREAT | O_TRUNC);
    printf("html-write %d %d\n", limit, normalize(std::string(500, '&'), f));
    stored("/html");
  }
}
