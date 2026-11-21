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
    window = Window::Owned;
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
  window = Window::Borrowed;
  memset(ringBuffer, 0, RING_BYTES);
  return true;
}

bool InflateReader::initWithSegments(uint8_t** segments) {
  if (!init(false) || !segments) {
    deinit();
    return false;
  }
  for (size_t i = 0; i < RING_SEGMENTS; ++i) {
    if (!segments[i]) {
      deinit();
      return false;
    }
  }
  for (size_t i = 0; i < RING_SEGMENTS; ++i) memset(segments[i], 0, RING_SEGMENT_BYTES);
  ringBuffer = segments;
  window = Window::Segmented;
  return true;
}

void InflateReader::deinit() {
  if (window == Window::Owned) free(ringBuffer);
  ringBuffer = nullptr;
  input = nullptr;
  available = position = 0;
  fill = nullptr;
  context = nullptr;
  outputStart = nullptr;
  window = Window::None;
  exhausted = ready = finished = false;
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
    size_t used = 0, written = 0;
    const uint8_t* decoded = nullptr;
    int32_t status;
    if (window == Window::Segmented) {
      auto* segments = static_cast<uint8_t**>(ringBuffer);
      status = xp_inflate_small_step_segmented(state, input, available, !exhausted, segments, &position, &used,
                                               &written, maxLen - *produced);
      decoded = segments[start / RING_SEGMENT_BYTES] + start % RING_SEGMENT_BYTES;
    } else {
      const size_t count = ringBuffer ? std::min(maxLen - *produced, RING_BYTES - position) : maxLen - *produced;
      auto* ring = static_cast<uint8_t*>(ringBuffer);
      status = xp_inflate_small_step(state, input, available, !exhausted, ring ? ring : outputStart,
                                     ring ? RING_BYTES : position + count, &position, ring != nullptr, &used, &written,
                                     count);
      if (ring) decoded = ring + start;
    }
    if (used) input += used;
    available -= used;
    if (decoded) {
      memcpy(dest + *produced, decoded, written);
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
