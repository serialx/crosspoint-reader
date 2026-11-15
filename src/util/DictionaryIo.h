#pragma once
#include <HalStorage.h>
#include <xp_ffi.h>

inline XpDictionaryFile dictionaryFile(HalFile& file) {
  return {&file,
          [](void* ctx, uint8_t* out, size_t n) -> int32_t { return static_cast<HalFile*>(ctx)->read(out, n); },
          [](void* ctx, const uint8_t* data, size_t n) { return static_cast<HalFile*>(ctx)->write(data, n); },
          [](void* ctx, size_t at) { return static_cast<HalFile*>(ctx)->seekSet(at); },
          [](void* ctx) { return static_cast<HalFile*>(ctx)->position(); },
          [](void* ctx) { return static_cast<HalFile*>(ctx)->fileSize(); }};
}

// Internal callback discriminants; the ABI transports their uint8_t values.
enum class DictionaryPrepare : uint8_t {
  Clean = XP_DICTIONARY_PREPARE_CLEAN,
  Clear = XP_DICTIONARY_PREPARE_CLEAR,
  Open = XP_DICTIONARY_PREPARE_OPEN,
  Stem = XP_DICTIONARY_PREPARE_STEM,
};
enum class DictionaryView : uint8_t {
  Cleaned = XP_DICTIONARY_VIEW_CLEANED,
  Variant = XP_DICTIONARY_VIEW_VARIANT,
  Headword = XP_DICTIONARY_VIEW_HEADWORD,
};
enum class DictionaryOutput : uint8_t {
  Reserve = XP_DICTIONARY_OUTPUT_RESERVE,
  Append = XP_DICTIONARY_OUTPUT_APPEND,
};
enum class DictionaryDataOpen : uint8_t {
  Plain = XP_DICTIONARY_DATA_OPEN_PLAIN,
  Compressed = XP_DICTIONARY_DATA_OPEN_COMPRESSED,
  Temporary = XP_DICTIONARY_DATA_OPEN_TEMPORARY,
};
enum class DictionaryDefinitionEvent : uint8_t {
  Empty = XP_DICTIONARY_DEFINITION_EVENT_EMPTY,
  CombinedCap = XP_DICTIONARY_DEFINITION_EVENT_COMBINED_CAP,
  LowHeap = XP_DICTIONARY_DEFINITION_EVENT_LOW_HEAP,
  Temporary = XP_DICTIONARY_DEFINITION_EVENT_TEMPORARY,
  Extract = XP_DICTIONARY_DEFINITION_EVENT_EXTRACT,
  Bounds = XP_DICTIONARY_DEFINITION_EVENT_BOUNDS,
  Seek = XP_DICTIONARY_DEFINITION_EVENT_SEEK,
  Fallback = XP_DICTIONARY_DEFINITION_EVENT_FALLBACK,
};
enum class DictionaryOpenEvent : uint8_t {
  MissingFolder = XP_DICTIONARY_OPEN_EVENT_MISSING_FOLDER,
  MissingIndex = XP_DICTIONARY_OPEN_EVENT_MISSING_INDEX,
  MissingData = XP_DICTIONARY_OPEN_EVENT_MISSING_DATA,
  Offsets64 = XP_DICTIONARY_OPEN_EVENT_OFFSETS64,
  PathLength = XP_DICTIONARY_OPEN_EVENT_PATH_LENGTH,
  Allocation = XP_DICTIONARY_OPEN_EVENT_ALLOCATION,
};
enum class DictionaryLookupEvent : uint8_t {
  WorkingSet = XP_DICTIONARY_LOOKUP_EVENT_WORKING_SET,
  MissingEntries = XP_DICTIONARY_LOOKUP_EVENT_MISSING_ENTRIES,
  FirstCap = XP_DICTIONARY_LOOKUP_EVENT_FIRST_CAP,
  CombinedCap = XP_DICTIONARY_LOOKUP_EVENT_COMBINED_CAP,
  LowHeap = XP_DICTIONARY_LOOKUP_EVENT_LOW_HEAP,
};
enum class DictionaryRegistryEvent : uint8_t {
  Ambiguous = XP_DICTIONARY_REGISTRY_EVENT_AMBIGUOUS,
  MissingData = XP_DICTIONARY_REGISTRY_EVENT_MISSING_DATA,
  MissingRoot = XP_DICTIONARY_REGISTRY_EVENT_MISSING_ROOT,
  Found = XP_DICTIONARY_REGISTRY_EVENT_FOUND,
};
