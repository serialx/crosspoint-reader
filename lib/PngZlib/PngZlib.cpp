#include "PngZlib.h"

#include <cstring>

namespace {
void* arenaAlloc(void* opaque, size_t items, size_t size) {
  if (!items || size > sizeof(PngZlibArena) / items) return nullptr;
  return static_cast<PngZlibArena*>(opaque)->bytes;
}

void arenaFree(void*, void*) {}

bool readBytes(PngZlibStream* decoder, unsigned bytes) {
  auto& stream = decoder->stream;
  while (decoder->count < bytes && stream.avail_in) {
    decoder->value = (decoder->value << 8) | *stream.next_in++;
    --stream.avail_in;
    ++stream.total_in;
    ++decoder->count;
  }
  return decoder->count == bytes;
}
}  // namespace

int pngZlibInit(PngZlibStream* decoder, PngZlibArena* arena) {
  std::memset(decoder, 0, sizeof(*decoder));
  decoder->phase = PngZlibStream::Phase::Header;
  decoder->stream.zalloc = arenaAlloc;
  decoder->stream.zfree = arenaFree;
  decoder->stream.opaque = arena;
  return mz_inflateInit2(&decoder->stream, -15);
}

int pngZlibInflate(PngZlibStream* decoder, int flush, int checkCrc) {
  using Phase = PngZlibStream::Phase;
  auto& stream = decoder->stream;
  if (!stream.state || flush != MZ_NO_FLUSH) return MZ_STREAM_ERROR;
  const auto inputBefore = stream.total_in;
  const auto outputBefore = stream.total_out;
  for (;;) {
    switch (decoder->phase) {
      case Phase::Header:
        if (!readBytes(decoder, 2)) goto needInput;
        if (decoder->value % 31 || (decoder->value & 0x0f00) != 0x0800 || (decoder->value >> 12) > 7) {
          decoder->phase = Phase::Error;
          return MZ_DATA_ERROR;
        }
        decoder->phase = (decoder->value & 0x20) ? Phase::Dictionary : Phase::Deflate;
        decoder->value = decoder->count = 0;
        break;
      case Phase::Dictionary:
        if (!readBytes(decoder, 4)) goto needInput;
        return MZ_NEED_DICT;
      case Phase::Deflate: {
        const int status = mz_inflate_ex(&stream, flush, checkCrc != 0);
        if (status != MZ_STREAM_END) {
          // PNGdec delivers a full row only on OK/END; miniz can report BUF_ERROR
          // after producing bytes from its bit reservoir without fresh input.
          if (status == MZ_BUF_ERROR && (stream.total_in != inputBefore || stream.total_out != outputBefore)) {
            return MZ_OK;
          }
          return status;
        }
        decoder->phase = Phase::Trailer;
        break;
      }
      case Phase::Trailer:
        if (!readBytes(decoder, 4)) goto needInput;
        if (checkCrc && decoder->value != stream.adler) {
          decoder->phase = Phase::Error;
          return MZ_DATA_ERROR;
        }
        decoder->phase = Phase::Done;
        return MZ_STREAM_END;
      case Phase::Done:
        return MZ_STREAM_END;
      case Phase::Error:
        return MZ_DATA_ERROR;
    }
  }
needInput:
  return stream.total_in != inputBefore || stream.total_out != outputBefore ? MZ_OK : MZ_BUF_ERROR;
}

int pngZlibEnd(PngZlibStream* decoder) { return mz_inflateEnd(&decoder->stream); }
