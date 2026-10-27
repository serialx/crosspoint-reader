#include <PngZlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "StreamFixture.h"

#define CHECK(value)                                                                 \
  do {                                                                               \
    if (!(value)) {                                                                  \
      std::fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #value); \
      std::exit(1);                                                                  \
    }                                                                                \
  } while (0)

static int decode(std::vector<unsigned char> input, unsigned inputChunk, unsigned outputChunk, bool checkCrc,
                  bool expectBytes = true) {
  auto arena = std::make_unique<PngZlibArena>();
  PngZlibStream decoder;
  CHECK(pngZlibInit(&decoder, arena.get()) == MZ_OK);
  const auto state = reinterpret_cast<uintptr_t>(decoder.stream.state);
  CHECK(state >= reinterpret_cast<uintptr_t>(arena.get()));
  CHECK(state < reinterpret_cast<uintptr_t>(arena.get()) + sizeof(*arena));
  std::vector<unsigned char> output(70016, 0xa5);
  size_t supplied = 0, produced = 0;
  int status = MZ_OK;
  for (unsigned calls = 0; calls < 200000; ++calls) {
    auto& stream = decoder.stream;
    if (!stream.avail_in && supplied < input.size()) {
      stream.next_in = input.data() + supplied;
      stream.avail_in = std::min<size_t>(inputChunk, input.size() - supplied);
      supplied += stream.avail_in;
    }
    stream.next_out = output.data() + produced;
    stream.avail_out = std::min<size_t>(outputChunk, output.size() - produced);
    const auto before = stream.avail_out;
    status = pngZlibInflate(&decoder, MZ_NO_FLUSH, checkCrc);
    produced += before - stream.avail_out;
    CHECK(produced <= 70000);
    if (status == MZ_STREAM_END || status == MZ_DATA_ERROR || status == MZ_NEED_DICT) break;
    CHECK(status == MZ_OK || status == MZ_BUF_ERROR);
    if (status == MZ_BUF_ERROR && supplied == input.size() && !stream.avail_in) break;
    CHECK(calls != 199999);
  }
  if (expectBytes) {
    CHECK(produced == 70000);
    for (size_t i = 0; i < produced; ++i) CHECK(output[i] == (i * i / 71 + i * 13) % 256);
    for (size_t i = produced; i < output.size(); ++i) CHECK(output[i] == 0xa5);
  }
  CHECK(pngZlibEnd(&decoder) == MZ_OK);
  CHECK(decoder.stream.state == nullptr);
  CHECK(pngZlibEnd(&decoder) == MZ_OK);
  CHECK(pngZlibInit(&decoder, arena.get()) == MZ_OK);
  CHECK(pngZlibEnd(&decoder) == MZ_OK);
  return status;
}

int main() {
  const std::vector<unsigned char> valid(std::begin(COMPRESSED), std::end(COMPRESSED));
  for (unsigned in : {1u, 2u, 7u, 2048u, 65536u}) {
    for (unsigned out : {1u, 37u, 32768u, 70000u}) {
      CHECK(decode(valid, in, out, false) == MZ_STREAM_END);
      CHECK(decode(valid, in, out, true) == MZ_STREAM_END);
    }
  }
  auto corrupt = valid;
  corrupt.back() ^= 1;
  CHECK(decode(corrupt, 7, 37, false) == MZ_STREAM_END);
  CHECK(decode(corrupt, 7, 37, true) == MZ_DATA_ERROR);
  for (size_t length : {0u, 1u, 2u, 17u}) {
    CHECK(decode({valid.begin(), valid.begin() + length}, 1, 37, false, false) == MZ_BUF_ERROR);
  }
  for (unsigned missing = 1; missing <= 4; ++missing) {
    CHECK(decode({valid.begin(), valid.end() - missing}, 7, 37, true) == MZ_BUF_ERROR);
  }
  corrupt = valid;
  corrupt[0] ^= 1;
  CHECK(decode(corrupt, 1, 37, false, false) == MZ_DATA_ERROR);
  CHECK(decode({0x78, 0x20, 0, 0, 0, 1}, 1, 37, false, false) == MZ_NEED_DICT);
  auto arena = std::make_unique<PngZlibArena>();
  PngZlibStream decoder;
  CHECK(pngZlibInit(&decoder, arena.get()) == MZ_OK);
  CHECK(decoder.stream.zalloc(arena.get(), 1, sizeof(*arena) + 1) == nullptr);
  CHECK(decoder.stream.zalloc(arena.get(), SIZE_MAX, 2) == nullptr);
  CHECK(pngZlibInflate(&decoder, MZ_FINISH, false) == MZ_STREAM_ERROR);
  CHECK(pngZlibEnd(&decoder) == MZ_OK);
  CHECK(pngZlibInflate(&decoder, MZ_NO_FLUSH, false) == MZ_STREAM_ERROR);
  CHECK(mz_crc32(0, reinterpret_cast<const unsigned char*>("123456789"), 9) == 0xcbf43926);
  std::printf("PngZlib: chunking, wraparound, CRC, truncation, arena bounds passed; arena=%zu stream=%zu\n",
              sizeof(PngZlibArena), sizeof(PngZlibStream));
}
