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
enum class DictionaryPrepare : uint8_t { Clean, Clear, Open, Stem };
enum class DictionaryView : uint8_t { Cleaned, Variant, Headword };
enum class DictionaryOutput : uint8_t { Reserve, Unused, Append };
enum class DictionaryDataOpen : uint8_t { Plain, Compressed, Temporary };
enum class DictionaryDefinitionEvent : uint8_t {
  Empty,
  CombinedCap,
  LowHeap,
  Temporary,
  Extract,
  Bounds,
  Seek,
  Fallback
};
enum class DictionaryOpenEvent : uint8_t {
  MissingFolder,
  MissingIndex,
  MissingData,
  Offsets64,
  PathLength,
  Allocation
};
enum class DictionaryLookupEvent : uint8_t { WorkingSet, MissingEntries, FirstCap, CombinedCap, LowHeap };
enum class DictionaryRegistryEvent : uint8_t { Ambiguous, MissingData, MissingRoot, Found };
