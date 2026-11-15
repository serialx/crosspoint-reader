#include <xp_ffi.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

#include "MinizConfig.h"

namespace {
constexpr size_t WINDOW_SIZE = 32768;
constexpr int32_t NEEDS_INPUT = 0;
constexpr int32_t DONE = 1;

struct CompatState {
  size_t cursor;
  size_t pending;
  size_t decoderOffset;
  int32_t status;
  uint32_t adler;
  bool wrapped;
};

void* allocate(void*, size_t items, size_t size) { return malloc(items * size); }
void release(void*, void* address) { free(address); }

uint8_t* decoder(CompatState* state) { return reinterpret_cast<uint8_t*>(state) + state->decoderOffset; }
uint8_t* window(CompatState* state) { return decoder(state) + xp_inflate_state_size(); }

void drain(mz_streamp stream, CompatState* state) {
  const size_t count = std::min(state->pending, size_t{stream->avail_out});
  if (count) {
    memcpy(stream->next_out, window(state) + state->cursor, count);
    stream->next_out += count;
  }
  stream->avail_out -= count;
  stream->total_out += count;
  state->pending -= count;
  state->cursor = (state->cursor + count) & (WINDOW_SIZE - 1);
}
}  // namespace

extern "C" int mz_inflateInit2(mz_streamp stream, int windowBits) {
  if (!stream) return MZ_STREAM_ERROR;
  if (windowBits != 15 && windowBits != -15) return MZ_PARAM_ERROR;
  stream->data_type = 0;
  stream->adler = 0;
  stream->msg = nullptr;
  stream->total_in = 0;
  stream->total_out = 0;
  stream->reserved = 0;
  if (!stream->zalloc) stream->zalloc = allocate;
  if (!stream->zfree) stream->zfree = release;

  const size_t alignment = xp_inflate_state_align();
  const size_t offset = (sizeof(CompatState) + alignment - 1) & ~(alignment - 1);
  const size_t bytes = offset + xp_inflate_state_size() + WINDOW_SIZE;
  // The mz_stream owns one contiguous block through its allocation hooks,
  // exactly as the SDK expects. mz_inflateEnd pairs it with the same zfree.
  void* storage = stream->zalloc(stream->opaque, 1, bytes);
  if (!storage) return MZ_MEM_ERROR;
  auto* state = new (storage) CompatState{0, 0, offset, NEEDS_INPUT, 1, windowBits > 0};
  if (xp_inflate_init(decoder(state), xp_inflate_state_size(), windowBits > 0) != 0) {
    stream->zfree(stream->opaque, storage);
    return MZ_STREAM_ERROR;
  }
  memset(window(state), 0, WINDOW_SIZE);
  stream->state = reinterpret_cast<mz_internal_state*>(state);
  return MZ_OK;
}

extern "C" int mz_inflateInit(mz_streamp stream) { return mz_inflateInit2(stream, 15); }

extern "C" int mz_inflate(mz_streamp stream, int flush) { return mz_inflate_ex(stream, flush, 1); }

extern "C" int mz_inflate_ex(mz_streamp stream, int flush, int computeAdler32) {
  // The pinned SDK only uses NO_FLUSH; this is not a general zlib replacement.
  if (!stream || !stream->state || flush != MZ_NO_FLUSH) return MZ_STREAM_ERROR;
  if ((!stream->next_in && stream->avail_in) || (!stream->next_out && stream->avail_out)) return MZ_STREAM_ERROR;
  auto* state = reinterpret_cast<CompatState*>(stream->state);
  if (state->status < 0) return MZ_DATA_ERROR;
  if (state->pending) {
    drain(stream, state);
    return state->status == DONE && !state->pending ? MZ_STREAM_END : MZ_OK;
  }

  const unsigned initialInput = stream->avail_in;
  for (;;) {
    size_t consumed = 0, produced = 0, position = state->cursor;
    state->status = xp_inflate_step(decoder(state), stream->next_in, stream->avail_in, 1, window(state), WINDOW_SIZE,
                                    &position, 1, &consumed, &produced);
    if (consumed) stream->next_in += consumed;
    stream->avail_in -= consumed;
    stream->total_in += consumed;
    // A checksum mismatch still includes this call; other errors retain the previous Adler.
    if ((computeAdler32 || state->wrapped) && (state->status >= 0 || state->status == -2)) {
      state->adler = mz_adler32(state->adler, window(state) + state->cursor, produced);
    }
    stream->adler = state->adler;
    state->pending = produced;
    drain(stream, state);

    if (state->status < 0) return MZ_DATA_ERROR;
    if (state->status == NEEDS_INPUT && !initialInput) return MZ_BUF_ERROR;
    if (state->status == DONE || !stream->avail_in || !stream->avail_out || state->pending) {
      return state->status == DONE && !state->pending ? MZ_STREAM_END : MZ_OK;
    }
  }
}

extern "C" int mz_inflateEnd(mz_streamp stream) {
  if (!stream) return MZ_STREAM_ERROR;
  if (stream->state) {
    stream->zfree(stream->opaque, stream->state);
    stream->state = nullptr;
  }
  return MZ_OK;
}
