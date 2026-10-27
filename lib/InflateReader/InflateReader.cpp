#include "InflateReader.h"

#include <xp_ffi.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

InflateReader::~InflateReader() { deinit(); }

bool InflateReader::init(bool streaming) {
  deinit();
  if (streaming) {
    ringBuffer = static_cast<uint8_t*>(malloc(RING_BYTES));
    if (!ringBuffer) return false;
    ownsRing = true;
    memset(ringBuffer, 0, RING_BYTES);
  }
  // Changing the facade layout requires deliberate heap remeasurement.
  static_assert(sizeof(InflateReader) == (sizeof(void*) == 4 ? 1292 : 1328));
  static_assert(alignof(InflateReader) == (sizeof(void*) == 4 ? 4 : 8));
  ready = xp_inflate_small_state_size() <= sizeof(state) && xp_inflate_small_state_align() <= alignof(InflateReader) &&
          xp_inflate_small_init(state, sizeof(state), 0) == 0;
  if (!ready) deinit();
  return ready;
}

bool InflateReader::initWithRing(uint8_t* ring) {
  if (!init(false) || !ring) {
    deinit();
    return false;
  }
  ringBuffer = ring;
  memset(ringBuffer, 0, RING_BYTES);
  return true;
}

void InflateReader::deinit() {
  if (ownsRing) free(ringBuffer);
  ringBuffer = nullptr;
  input = nullptr;
  available = position = 0;
  fill = nullptr;
  context = nullptr;
  outputStart = nullptr;
  ownsRing = exhausted = ready = finished = false;
}

void InflateReader::setSource(const uint8_t* src, size_t len) {
  input = src;
  available = len;
  exhausted = true;
}

void InflateReader::setFill(FillFn fn, void* ctx) {
  fill = fn;
  context = ctx;
}

InflateStatus InflateReader::readAtMost(uint8_t* dest, size_t maxLen, size_t* produced) {
  *produced = 0;
  if (!ready || (!dest && maxLen)) return InflateStatus::Error;
  if (!maxLen) return finished ? InflateStatus::Done : InflateStatus::Ok;
  if (!ringBuffer) {
    if (!outputStart) outputStart = dest;
    if (dest != outputStart + position) return InflateStatus::Error;
    if (maxLen) memset(dest, 0, maxLen);  // Rust output slices require initialized bytes.
  }
  while (*produced < maxLen && !finished) {
    if (!available && !exhausted) {
      available = fill ? fill(context, &input) : 0;
      if (!available) exhausted = true;
    }
    const size_t start = position;
    const size_t count = ringBuffer ? std::min(maxLen - *produced, RING_BYTES - position) : maxLen - *produced;
    size_t used = 0, written = 0;
    const int32_t status = xp_inflate_small_step(
        state, input, available, !exhausted, ringBuffer ? ringBuffer : outputStart,
        ringBuffer ? RING_BYTES : position + count, &position, ringBuffer != nullptr, &used, &written, count);
    if (used) input += used;
    available -= used;
    if (ringBuffer) {
      memcpy(dest + *produced, ringBuffer + start, written);
      if (position == RING_BYTES) position = 0;
    }
    *produced += written;
    if (status < 0) {
      ready = false;
      return InflateStatus::Error;
    }
    if (status == 1) finished = true;
    if (!finished && !used && !written && (available || exhausted)) {
      ready = false;
      return InflateStatus::Error;
    }
  }
  return finished ? InflateStatus::Done : InflateStatus::Ok;
}

bool InflateReader::read(uint8_t* dest, size_t len) {
  size_t produced = 0;
  return readAtMost(dest, len, &produced) != InflateStatus::Error && produced == len;
}
