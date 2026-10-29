#include "Dictionary.h"

#include <Arduino.h>
#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "../../lib/Epub/Epub/CjkBreaks.h"
#include "DictZip.h"
#include "DictionaryIo.h"
#include "DictionaryRegistry.h"
#include "StringUtils.h"
#include "TxtToHtml.h"

namespace {
constexpr const char* DICT_TMP_FILE = "/.crosspoint/dict.tmp";
constexpr uint32_t DEFINITION_HEAP_HEADROOM_BYTES = 8 * 1024;
constexpr uint32_t QIDX_MAGIC = 0x58444951;  // "QIDX" little-endian
constexpr uint32_t SIDX_MAGIC = 0x58444953;  // "SIDX" little-endian

using SidecarHeader = XpDictionaryHeader;

SidecarHeader readSidecarHeader(HalFile& file, uint32_t magic, uint32_t sampleInterval) {
  SidecarHeader result{};
  const auto io = dictionaryFile(file);
  xp_dictionary_header(&io, magic, sampleInterval, &result);
  return result;
}
struct IfoFacts {
  bool offsets64 = false;        // idxoffsetbits=64 (unsupported)
  bool htmlDefinitions = false;  // sametypesequence=h or x (HTML or XDXF)
};

IfoFacts readIfoFacts(const std::string& ifoPath) {
  IfoFacts facts;
  HalFile ifo;
  if (!Storage.openFileForRead("DICT", ifoPath, ifo)) return facts;
  char buf[2048];
  const int n = ifo.read(buf, sizeof(buf) - 1);
  if (n <= 0) return facts;
  buf[n] = '\0';
  const uint8_t flags = xp_dictionary_ifo(reinterpret_cast<const uint8_t*>(buf), n);
  facts.offsets64 = flags & 1;
  facts.htmlDefinitions = flags & 2;
  return facts;
}

}  // namespace

bool DictEntryList::appendUnique(const char* headword, const DictLocation& location) {
  struct Context {
    DictEntryList& list;
    const char* headword;
    const DictLocation& location;
  } context{*this, headword, location};
  const auto heap = HalMemory::getDefaultHeap();
  const size_t bytes = strlen(headword) + 1;
  const XpDictionaryAppend io{&context,
                              count_,
                              count_ < kMaxEntries ? entries_[count_].headwordCapacity : 0,
                              bytes,
                              heap.freeBytes,
                              heap.largestBlockBytes,
                              [](void* ctx, size_t index) -> XpDictionaryLocation {
                                const auto& l = static_cast<Context*>(ctx)->list.entries_[index].location;
                                return {l.offset, l.size, l.found, l.readError};
                              },
                              [](void* ctx, size_t bytes) {
                                auto& list = static_cast<Context*>(ctx)->list;
                                auto buffer = makeUniqueNoThrow<char[]>(bytes);
                                if (!buffer) return false;
                                auto& entry = list.entries_[list.count_];
                                entry.headword = std::move(buffer);
                                entry.headwordCapacity = bytes;
                                return true;
                              },
                              [](void* ctx) {
                                auto& c = *static_cast<Context*>(ctx);
                                auto& entry = c.list.entries_[c.list.count_++];
                                memcpy(entry.headword.get(), c.headword, strlen(c.headword) + 1);
                                entry.location = c.location;
                              }};
  const uint8_t status = xp_dictionary_append(&io, location.offset, location.size);
  if (status == 1) LOG_ERR("DICT", "Entry list full at %u entries", static_cast<unsigned>(count_));
  if (status == 2) {
    LOG_ERR("DICT", "Low heap for %u byte headword", static_cast<unsigned>(bytes));
    lowMemory_ = true;
  }
  return status == 0;
}

bool Dictionary::open(const char* folderName) {
  struct Context {
    Dictionary& dictionary;
    const char* folder;
    std::string resolved;
  } context{*this, folderName, {}};
  const XpDictionaryOpen io{
      &context,
      &hasPlainDict,
      &hasSyn,
      &htmlDefinitions,
      [](void* ctx) { static_cast<Context*>(ctx)->dictionary.basePath.clear(); },
      [](void* ctx) {
        auto& c = *static_cast<Context*>(ctx);
        return DictionaryRegistry::resolveBasePath(c.folder, c.resolved);
      },
      [](void* ctx, uint8_t kind) {
        static constexpr const char* suffixes[] = {".idx", ".dict", ".dict.dz", ".syn"};
        return Storage.exists((static_cast<Context*>(ctx)->resolved + suffixes[kind]).c_str());
      },
      [](void* ctx) -> uint8_t {
        const auto facts = readIfoFacts(static_cast<Context*>(ctx)->resolved + ".ifo");
        return static_cast<uint8_t>(facts.offsets64 | (facts.htmlDefinitions << 1));
      },
      [](void* ctx) { return static_cast<Context*>(ctx)->resolved.size(); },
      [](void* ctx) {
        auto& c = *static_cast<Context*>(ctx);
        c.dictionary.entryList = makeUniqueNoThrow<DictEntryList>();
        return c.dictionary.entryList != nullptr;
      },
      [](void* ctx) {
        auto& c = *static_cast<Context*>(ctx);
        c.dictionary.basePath = std::move(c.resolved);
      },
      [](void* ctx, uint8_t event, size_t n) {
        const auto& c = *static_cast<Context*>(ctx);
        switch (static_cast<DictionaryOpenEvent>(event)) {
          case DictionaryOpenEvent::MissingFolder:
            LOG_ERR("DICT", "No dictionary found in folder '%s'", c.folder ? c.folder : "");
            break;
          case DictionaryOpenEvent::MissingIndex:
            LOG_ERR("DICT", "%s.idx missing (compressed .idx.gz is not supported)", c.resolved.c_str());
            break;
          case DictionaryOpenEvent::MissingData:
            LOG_ERR("DICT", "%s has no .dict or .dict.dz", c.resolved.c_str());
            break;
          case DictionaryOpenEvent::Offsets64:
            LOG_ERR("DICT", "%s uses 64-bit index offsets (unsupported)", c.resolved.c_str());
            break;
          case DictionaryOpenEvent::PathLength:
            LOG_ERR("DICT", "Dictionary path too long (%u chars, max %u)", static_cast<unsigned>(n),
                    static_cast<unsigned>(PATH_BUF_BYTES - LONGEST_SUFFIX_LEN - 1));
            break;
          case DictionaryOpenEvent::Allocation:
            LOG_ERR("DICT", "Out of memory for the %u byte entry list", static_cast<unsigned>(sizeof(DictEntryList)));
            break;
        }
      }};
  return xp_dictionary_open(&io);
}

bool Dictionary::buildPath(char* buf, size_t bufSize, const char* suffix) const {
  const int n = snprintf(buf, bufSize, "%s%s", basePath.c_str(), suffix);
  if (n < 0 || static_cast<size_t>(n) >= bufSize) {
    LOG_ERR("DICT", "Path too long: %s%s", basePath.c_str(), suffix);
    return false;
  }
  return true;
}
bool Dictionary::sidecarIsStale(const std::string& sourcePath, const std::string& sidecarPath, uint32_t magic) {
  HalFile src;
  if (!Storage.openFileForRead("DICT", sourcePath, src)) return false;
  const uint32_t srcSize = static_cast<uint32_t>(src.fileSize());

  HalFile sidecar;
  if (!Storage.openFileForRead("DICT", sidecarPath, sidecar)) return true;
  const SidecarHeader header = readSidecarHeader(sidecar, magic, SAMPLE_INTERVAL);
  return !header.valid || header.sourceFileSize != srcSize;
}

bool Dictionary::needsIndex() { return indexOperation(false, nullptr, nullptr) != 0; }

bool Dictionary::buildIndex(void (*yieldFn)(void*), void* ctx, IndexResult* outResult) {
  const auto result = static_cast<IndexResult>(indexOperation(true, yieldFn, ctx));
  if (outResult) *outResult = result;
  return result == IndexResult::Ok;
}

uint8_t Dictionary::indexOperation(bool build, void (*yieldFn)(void*), void* ctx) {
  struct Context {
    Dictionary& dictionary;
    void (*yieldFn)(void*);
    void* yieldCtx;
  } context{*this, yieldFn, ctx};
  const XpDictionaryIndex io{&context,
                             isOpen(),
                             hasSyn,
                             [](void* ctx, bool syn) {
                               const auto& base = static_cast<Context*>(ctx)->dictionary.basePath;
                               return sidecarIsStale(base + (syn ? ".syn" : ".idx"), base + (syn ? ".sidx" : ".qidx"),
                                                     syn ? SIDX_MAGIC : QIDX_MAGIC);
                             },
                             [](void* ctx, bool syn) -> uint8_t {
                               const auto& c = *static_cast<Context*>(ctx);
                               const auto& base = c.dictionary.basePath;
                               IndexResult result = IndexResult::Ok;
                               buildSidecar(base + (syn ? ".syn" : ".idx"), base + (syn ? ".sidx" : ".qidx"),
                                            syn ? SIDX_MAGIC : QIDX_MAGIC, syn ? 4 : 8, c.yieldFn, c.yieldCtx, &result);
                               return static_cast<uint8_t>(result);
                             },
                             [](void* ctx) {
                               LOG_ERR("DICT", "Synonym index build failed; synonyms disabled for %s",
                                       static_cast<Context*>(ctx)->dictionary.basePath.c_str());
                             }};
  return xp_dictionary_index(&io, build);
}

bool Dictionary::buildSidecar(const std::string& sourcePath, const std::string& sidecarPath, uint32_t magic,
                              uint32_t suffixBytes, void (*yieldFn)(void*), void* ctx, IndexResult* outResult) {
  const auto fail = [outResult](IndexResult r) {
    if (outResult) *outResult = r;
    return false;
  };

  HalFile src;
  if (!Storage.openFileForRead("DICT", sourcePath, src)) return fail(IndexResult::ReadError);

  constexpr size_t CHUNK_BYTES = 4096;
  auto buf = makeUniqueNoThrow<uint8_t[]>(CHUNK_BYTES);
  if (!buf) {
    LOG_ERR("DICT", "OOM: %u byte index scan buffer", CHUNK_BYTES);
    return fail(IndexResult::LowMemory);
  }
  HalFile out;
  if (!Storage.openFileForWrite("DICT", sidecarPath, out)) return fail(IndexResult::ReadError);
  const unsigned long startMs = millis();
  const auto input = dictionaryFile(src);
  const auto output = dictionaryFile(out);
  XpDictionaryScan scan{};
  const uint8_t status = xp_dictionary_scan(&input, &output, magic, suffixBytes, buf.get(), ctx, yieldFn, &scan);
  const bool ok = status == 0;
  if (status == 1) LOG_ERR("DICT", "Index scan read failed at %lu", static_cast<unsigned long>(scan.position));
  if (!ok) {
    LOG_ERR("DICT", "Index build failed, removing %s", sidecarPath.c_str());
    out.close();  // close before remove of the same path
    Storage.remove(sidecarPath.c_str());
    return fail(IndexResult::ReadError);
  }

  LOG_INF("DICT", "Indexed %lu entries (%lu samples) from %s in %lu ms", static_cast<unsigned long>(scan.entryCount),
          static_cast<unsigned long>(scan.sampleCount), sourcePath.c_str(), millis() - startMs);
  return true;
}

bool Dictionary::openSession(LookupSession& session) { return sessionOperation(session, false); }

bool Dictionary::openSynonyms(LookupSession& session) { return sessionOperation(session, true); }

bool Dictionary::sessionOperation(LookupSession& session, bool synonyms) {
  struct Context {
    Dictionary& dictionary;
    LookupSession& session;
    HalFile* files[4];
  } context{*this, session, {&session.idx, &session.qidx, &session.syn, &session.sidx}};
  const XpDictionarySessionIo io{
      &context,
      &session.state,
      isOpen(),
      hasSyn,
      [](void* ctx, uint8_t slot) {
        auto& c = *static_cast<Context*>(ctx);
        static constexpr const char* suffixes[] = {".idx", ".qidx", ".syn", ".sidx"};
        char path[PATH_BUF_BYTES];
        return c.dictionary.buildPath(path, sizeof(path), suffixes[slot]) &&
               Storage.openFileForRead("DICT", path, *c.files[slot]);
      },
      [](void* ctx, uint8_t slot) -> uint32_t { return static_cast<Context*>(ctx)->files[slot]->fileSize(); },
      [](void* ctx, uint8_t slot) {
        return readSidecarHeader(*static_cast<Context*>(ctx)->files[slot], slot == 1 ? QIDX_MAGIC : SIDX_MAGIC,
                                 SAMPLE_INTERVAL);
      },
      [](void* ctx, uint8_t slot) { static_cast<Context*>(ctx)->files[slot]->close(); },
      [](void* ctx) {
        LOG_ERR("DICT", "Synonym index unusable for %s; synonyms skipped",
                static_cast<Context*>(ctx)->dictionary.basePath.c_str());
      }};
  return xp_dictionary_session(&io, synonyms);
}
void Dictionary::locateAll(LookupSession& session, const char* target, DictEntryList& out, bool* readError) {
  const bool failed = locateEntries(session, target, out, false);
  if (readError) *readError = failed;
}

bool Dictionary::locateEntries(LookupSession& session, const char* target, DictEntryList& out, bool synonyms) {
  const auto idx = dictionaryFile(session.idx);
  const auto qidx = dictionaryFile(session.qidx);
  const auto syn = dictionaryFile(session.syn);
  const auto sidx = dictionaryFile(session.sidx);
  const XpDictionarySearch search{&idx,
                                  &qidx,
                                  &syn,
                                  &sidx,
                                  session.state.idxSize,
                                  session.state.sampleCount,
                                  session.state.entryCount,
                                  session.state.synSize,
                                  session.state.synSampleCount,
                                  reinterpret_cast<uint8_t*>(wordBuf),
                                  &out,
                                  [](void* ctx, const uint8_t* word, XpDictionaryLocation location) {
                                    return static_cast<DictEntryList*>(ctx)->appendUnique(
                                        reinterpret_cast<const char*>(word),
                                        {location.offset, location.size, location.found, location.readError});
                                  },
                                  [](void*, bool synonym, uint32_t at) {
                                    if (synonym) {
                                      LOG_ERR("DICT", "Synonym seek to %lu failed", static_cast<unsigned long>(at));
                                    } else {
                                      LOG_ERR("DICT", "Index seek to %lu failed", static_cast<unsigned long>(at));
                                    }
                                  }};
  return xp_dictionary_search(&search, reinterpret_cast<const uint8_t*>(target), strlen(target), synonyms);
}

bool Dictionary::locateSynonyms(LookupSession& session, const char* target, DictEntryList& out, bool* readError) {
  if (readError) *readError = false;
  if (!openSynonyms(session)) {
    if (readError) *readError = session.state.synFailed;
    return false;
  }
  const bool failed = locateEntries(session, target, out, true);
  if (readError) *readError = failed;
  return !out.empty();
}

bool Dictionary::readDefinition(const DictLocation& location, std::string& out, LookupResult* outResult, bool append) {
  struct Context {
    Dictionary& dictionary;
    std::string& out;
    HalFile data, tmp;
    char path[PATH_BUF_BYTES]{};
  } context{*this, out, {}, {}, {}};
  const auto file = dictionaryFile(context.data);
  const XpDictionaryDefinition io{
      &context,
      &file,
      location.offset,
      location.size,
      location.found,
      hasPlainDict,
      htmlDefinitions,
      append,
      out.size(),
      out.capacity(),
      []() -> size_t { return ESP.getMaxAllocHeap(); },
      [](void* ctx, uint8_t kind) {
        auto& c = *static_cast<Context*>(ctx);
        if (static_cast<DictionaryDataOpen>(kind) == DictionaryDataOpen::Temporary)
          return Storage.openFileForRead("DICT", DICT_TMP_FILE, c.data);
        if (!c.dictionary.buildPath(
                c.path, sizeof(c.path),
                static_cast<DictionaryDataOpen>(kind) == DictionaryDataOpen::Plain ? ".dict" : ".dict.dz"))
          return false;
        if (static_cast<DictionaryDataOpen>(kind) == DictionaryDataOpen::Plain)
          return Storage.openFileForRead("DICT", c.path, c.data);
        c.tmp = Storage.open(DICT_TMP_FILE, O_WRITE | O_CREAT | O_TRUNC);
        if (!c.tmp) {
          LOG_ERR("DICT", "Failed to open %s", DICT_TMP_FILE);
          return false;
        }
        return true;
      },
      [](void* ctx, uint32_t offset, uint32_t size) -> uint8_t {
        auto& c = *static_cast<Context*>(ctx);
        DictZip::ExtractError error = DictZip::ExtractError::None;
        DictZip::extractEntry(c.path, offset, size, c.tmp, &error);
        return static_cast<uint8_t>(error);
      },
      [](void* ctx) { static_cast<Context*>(ctx)->tmp = HalFile{}; },
      [](void* ctx, size_t size, bool assign) -> uint8_t* {
        auto& out = static_cast<Context*>(ctx)->out;
        if (assign)
          out.assign(size, '\0');
        else
          out.resize(size);
        return reinterpret_cast<uint8_t*>(out.data());
      },
      [](void* ctx, uint8_t event, uint32_t a, uint32_t b, uint32_t c) {
        switch (static_cast<DictionaryDefinitionEvent>(event)) {
          case DictionaryDefinitionEvent::Empty:
            LOG_ERR("DICT", "Zero-length definition entry");
            break;
          case DictionaryDefinitionEvent::CombinedCap:
            LOG_ERR("DICT", "Combined definition exceeds %lu bytes", static_cast<unsigned long>(a));
            break;
          case DictionaryDefinitionEvent::LowHeap:
            LOG_ERR("DICT", "Low heap for %lu byte definition", static_cast<unsigned long>(a));
            break;
          case DictionaryDefinitionEvent::Temporary:
            break;
          case DictionaryDefinitionEvent::Extract:
            LOG_ERR("DICT", "dictzip extraction failed for %s (error %d)",
                    static_cast<Context*>(ctx)->dictionary.basePath.c_str(), static_cast<int>(a));
            break;
          case DictionaryDefinitionEvent::Bounds:
            LOG_ERR("DICT", "Definition out of bounds (%lu+%lu > %lu)", static_cast<unsigned long>(a),
                    static_cast<unsigned long>(b), static_cast<unsigned long>(c));
            break;
          case DictionaryDefinitionEvent::Seek:
            LOG_ERR("DICT", "Seek to %lu failed", static_cast<unsigned long>(a));
            break;
          case DictionaryDefinitionEvent::Fallback:
            LOG_ERR("DICT", "Low heap for %lu byte definition fallback", static_cast<unsigned long>(a));
            break;
        }
      }};
  const uint8_t result = xp_dictionary_definition(&io);
  if (result && outResult) *outResult = static_cast<LookupResult>(result);
  return result == 0;
}

std::string Dictionary::cleanWord(const char* word) {
  if (!word) return "";
  size_t start = 0, end = 0;
  xp_dictionary_clean_bounds(reinterpret_cast<const uint8_t*>(word), strlen(word), &start, &end);
  std::string result(word + start, end - start);
  xp_dictionary_lower(reinterpret_cast<uint8_t*>(result.data()), result.size());
  return result;
}

void Dictionary::stemVariants(const std::string& word, std::vector<std::string>& out) {
  out.clear();
  out.reserve(6);
  struct Context {
    const std::string& word;
    std::vector<std::string>& out;
  } context{word, out};
  xp_dictionary_stems(reinterpret_cast<const uint8_t*>(word.data()), word.size(), &context,
                      [](void* ctx, size_t length, uint8_t suffix) {
                        auto& context = *static_cast<Context*>(ctx);
                        auto variant = context.word.substr(0, length);
                        if (suffix) variant.push_back(static_cast<char>(suffix));
                        if (std::find(context.out.begin(), context.out.end(), variant) == context.out.end())
                          context.out.push_back(std::move(variant));
                      });
}

bool Dictionary::lookup(const char* word, std::string& definitionOut, std::string& matchedHeadwordOut,
                        LookupResult* outResult) {
  struct Context {
    Dictionary& dictionary;
    const char* word;
    std::string& definition;
    std::string& matched;
    std::string cleaned;
    std::vector<std::string> variants;
    LookupSession session;
  } context{*this, word, definitionOut, matchedHeadwordOut, {}, {}, {}};
  const auto heap = HalMemory::getDefaultHeap();
  const XpDictionaryLookup io{
      &context,
      word ? strlen(word) + 1 : 1,
      sizeof(std::string),
      heap.freeBytes,
      heap.largestBlockBytes,
      isOpen(),
      entryList != nullptr,
      hasSyn,
      htmlDefinitions,
      [](void* ctx, uint8_t kind) {
        auto& c = *static_cast<Context*>(ctx);
        switch (static_cast<DictionaryPrepare>(kind)) {
          case DictionaryPrepare::Clean:
            c.cleaned = cleanWord(c.word);
            return true;
          case DictionaryPrepare::Clear:
            c.dictionary.entryList->clear();
            return true;
          case DictionaryPrepare::Open:
            return c.dictionary.openSession(c.session);
          case DictionaryPrepare::Stem:
            stemVariants(c.cleaned, c.variants);
            return true;
        }
        return false;
      },
      [](void* ctx, uint8_t kind, size_t index) -> XpDictionaryView {
        auto& c = *static_cast<Context*>(ctx);
        if (static_cast<DictionaryView>(kind) == DictionaryView::Headword) {
          const char* head = c.dictionary.entryList->at(index).headword.get();
          return {reinterpret_cast<const uint8_t*>(head), strlen(head)};
        }
        if (static_cast<DictionaryView>(kind) == DictionaryView::Variant && index >= c.variants.size())
          return {nullptr, 0};
        const auto& text = static_cast<DictionaryView>(kind) == DictionaryView::Cleaned ? c.cleaned : c.variants[index];
        return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
      },
      [](void* ctx) -> XpDictionaryGroup {
        const auto& group = *static_cast<Context*>(ctx)->dictionary.entryList;
        return {group.size(), group.lowMemory()};
      },
      [](void* ctx, const uint8_t* text, size_t, bool synonym) {
        auto& c = *static_cast<Context*>(ctx);
        bool readError = false;
        if (synonym)
          c.dictionary.locateSynonyms(c.session, reinterpret_cast<const char*>(text), *c.dictionary.entryList,
                                      &readError);
        else
          c.dictionary.locateAll(c.session, reinterpret_cast<const char*>(text), *c.dictionary.entryList, &readError);
        return readError;
      },
      [](void* ctx) {
        auto& c = *static_cast<Context*>(ctx);
        std::vector<std::string>{}.swap(c.variants);
        c.session.sidx = HalFile{};
        c.session.syn = HalFile{};
        c.session.qidx = HalFile{};
        c.session.idx = HalFile{};
      },
      [](void* ctx, bool synonym) {
        auto& c = *static_cast<Context*>(ctx);
        const char* title = synonym ? c.cleaned.c_str() : c.dictionary.entryList->front().headword.get();
        c.matched = title;
      },
      [](void* ctx, size_t index) -> XpDictionaryLocation {
        const auto& location = static_cast<Context*>(ctx)->dictionary.entryList->at(index).location;
        return {location.offset, location.size, location.found, location.readError};
      },
      [](void* ctx, size_t index, bool append) -> uint8_t {
        auto& c = *static_cast<Context*>(ctx);
        LookupResult result = LookupResult::Found;
        c.dictionary.readDefinition(c.dictionary.entryList->at(index).location, c.definition, &result, append);
        return static_cast<uint8_t>(result);
      },
      [](void* ctx, uint8_t operation, const uint8_t* data, size_t size) {
        auto& out = static_cast<Context*>(ctx)->definition;
        if (static_cast<DictionaryOutput>(operation) == DictionaryOutput::Reserve) {
          out.clear();
          out.reserve(size);
        } else
          out.append(reinterpret_cast<const char*>(data), size);
      },
      []() -> size_t { return ESP.getMaxAllocHeap(); },
      [](void*, uint8_t event, uint32_t a, uint32_t b) {
        switch (static_cast<DictionaryLookupEvent>(event)) {
          case DictionaryLookupEvent::WorkingSet:
            LOG_ERR("DICT", "Low heap for lookup (%u byte working set)", static_cast<unsigned>(a));
            break;
          case DictionaryLookupEvent::MissingEntries:
            LOG_ERR("DICT", "No entry list for the open dictionary");
            break;
          case DictionaryLookupEvent::FirstCap:
            LOG_INF("DICT", "First definition fills the cap; omitting labels and later entries");
            break;
          case DictionaryLookupEvent::CombinedCap:
            LOG_INF("DICT", "Combined definitions stop at the %lu byte cap: %u of %u entries shown",
                    static_cast<unsigned long>(MAX_DEFINITION_BYTES), static_cast<unsigned>(a),
                    static_cast<unsigned>(b));
            break;
          case DictionaryLookupEvent::LowHeap:
            LOG_ERR("DICT", "Low heap for combined %lu byte definition", static_cast<unsigned long>(a));
            break;
        }
      }};
  const auto result = static_cast<LookupResult>(xp_dictionary_lookup(&io));
  if (outResult) *outResult = result;
  return result == LookupResult::Found;
}
