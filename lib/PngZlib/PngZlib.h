#pragma once

#include <MinizConfig.h>

#include <cstddef>
#include <cstdint>

// One miniz allocation: inflater state, 32 KiB dictionary and bookkeeping.
// The allocator checks the backend's request against this fixed budget.
union PngZlibArena {
  uint64_t alignment;
  uint8_t bytes[41200];
};

struct PngZlibStream {
  enum class Phase : uint8_t { Header, Dictionary, Deflate, Trailer, Done, Error };
  mz_stream stream;
  Phase phase;
  uint32_t value;
  unsigned count;
};

int pngZlibInit(PngZlibStream* stream, PngZlibArena* arena);
int pngZlibInflate(PngZlibStream* stream, int flush, int checkCrc);
int pngZlibEnd(PngZlibStream* stream);
