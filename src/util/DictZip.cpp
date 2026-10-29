#include "DictZip.h"

#include <Arduino.h>  // ESP.getMaxAllocHeap for the pre-reserve heap guard
#include <InflateReader.h>
#include <Memory.h>

#include "DictionaryIo.h"

namespace DictZip {
namespace {
static_assert(static_cast<uint8_t>(ExtractError::None) == XP_DICTIONARY_ZIP_OK);
static_assert(static_cast<uint8_t>(ExtractError::LowMemory) == XP_DICTIONARY_ZIP_LOW_MEMORY);
static_assert(static_cast<uint8_t>(ExtractError::ReadError) == XP_DICTIONARY_ZIP_READ_ERROR);
static_assert(static_cast<uint8_t>(ExtractError::Decompress) == XP_DICTIONARY_ZIP_DECOMPRESS);
constexpr size_t CHUNK_TABLE_HEAP_HEADROOM_BYTES = 1024;
constexpr size_t INPUT_BUF_BYTES = 2048;

struct ChunkSource {
  InflateReader reader;
  HalFile* file = nullptr;
  uint32_t remaining = 0;  // compressed bytes left in this chunk
  bool readFailed = false;
  uint8_t buf[INPUT_BUF_BYTES] = {};
};
size_t chunkReadCb(void* context, const uint8_t** data) {
  auto* src = static_cast<ChunkSource*>(context);
  const auto file = dictionaryFile(*src->file);
  return xp_dictzip_refill(&file, &src->remaining, &src->readFailed, src->buf, sizeof(src->buf), data);
}

bool extractChunkSlice(HalFile& file, uint32_t compressedOffset, uint32_t compressedSize, uint32_t discardSize,
                       uint32_t extractSize, HalFile& outFile, ExtractError* outError) {
  const auto fail = [outError](ExtractError e) {
    if (outError) *outError = e;
    return false;
  };
  if (extractSize == 0) return true;
  auto window = makeUniqueNoThrow<uint8_t[]>(InflateReader::RING_BYTES);
  if (!window) return fail(ExtractError::LowMemory);

  auto src = makeUniqueNoThrow<ChunkSource>();
  if (!src) return fail(ExtractError::LowMemory);
  src->file = &file;
  src->remaining = compressedSize;
  if (!file.seekSet(compressedOffset)) return fail(ExtractError::ReadError);
  if (!src->reader.initWithRing(window.get())) return fail(ExtractError::LowMemory);
  src->reader.setFill(&chunkReadCb, src.get());

  auto buf = makeUniqueNoThrow<uint8_t[]>(XP_DICTIONARY_ZIP_OUTPUT_BYTES);
  if (!buf) return fail(ExtractError::LowMemory);

  const auto output = dictionaryFile(outFile);
  const uint8_t status = xp_dictzip_slice(
      src.get(),
      [](void* ctx, uint8_t* data, size_t count) -> uint8_t {
        auto* source = static_cast<ChunkSource*>(ctx);
        if (source->reader.read(data, count)) return 0;
        return static_cast<uint8_t>(source->readFailed ? ExtractError::ReadError : ExtractError::Decompress);
      },
      buf.get(), discardSize, extractSize, &output);
  if (status) return fail(static_cast<ExtractError>(status));

  return true;
}

}  // namespace

bool parse(HalFile& file, Info* info, ExtractError* outError) {
  if (outError) *outError = ExtractError::None;
  if (!info) {
    if (outError) *outError = ExtractError::Decompress;
    return false;
  }
  *info = {};
  XpDictZipInfo result{};
  const auto input = dictionaryFile(file);
  const XpDictZipTable table{
      &info->chunkOffsets,
      [](void* ctx, size_t count) {
        if (ESP.getMaxAllocHeap() < count * sizeof(uint32_t) + CHUNK_TABLE_HEAP_HEADROOM_BYTES) return false;
        static_cast<std::vector<uint32_t>*>(ctx)->reserve(count);
        return true;
      },
      [](void* ctx, uint32_t value) { static_cast<std::vector<uint32_t>*>(ctx)->push_back(value); }};
  const auto status = static_cast<ExtractError>(xp_dictzip_parse(&input, &result, &table));
  info->dataOffset = result.dataOffset;
  info->totalSize = result.totalSize;
  info->chunkLength = result.chunkLength;
  info->valid = result.valid;
  if (outError) *outError = status;
  return status == ExtractError::None;
}

bool extractEntry(const char* path, uint32_t offset, uint32_t size, HalFile& outFile, ExtractError* outError) {
  const auto fail = [outError](ExtractError e) {
    if (outError) *outError = e;
    return false;
  };
  if (outError) *outError = ExtractError::None;
  if (size == 0) return true;

  HalFile file;
  if (!Storage.openFileForRead("DICTZIP", path, file)) return fail(ExtractError::ReadError);

  Info info;
  if (!parse(file, &info, outError)) return false;

  struct Context {
    HalFile& file;
    HalFile& output;
  } context{file, outFile};
  const XpDictZipInfo view{info.dataOffset, info.totalSize, info.chunkLength, info.valid};
  const uint8_t status =
      xp_dictzip_range(&view, info.chunkOffsets.data(), info.chunkOffsets.size(), offset, size, &context,
                       [](void* ctx, uint32_t at, uint32_t compressed, uint32_t discard, uint32_t count) -> uint8_t {
                         auto& context = *static_cast<Context*>(ctx);
                         ExtractError error = ExtractError::None;
                         extractChunkSlice(context.file, at, compressed, discard, count, context.output, &error);
                         return static_cast<uint8_t>(error);
                       });
  if (status) return fail(static_cast<ExtractError>(status));
  return true;
}

}  // namespace DictZip
