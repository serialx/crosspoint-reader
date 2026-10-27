#include "InflateStream.h"

#include <BuildScratch.h>
#include <HalMemory.h>
#include <Logging.h>
#include <xp_ffi.h>

#include <cstdlib>
#include <cstring>

namespace {
constexpr size_t WINDOW_SIZE = 32768;
constexpr int32_t NEEDS_INPUT = 0;
constexpr int32_t DONE = 1;
void logAllocationFailure(const char* allocation, size_t bytes) {
  const auto heap = HalMemory::getDefaultHeap();
  const auto psram = HalMemory::getPsramHeap();
  LOG_ERR("ZIP", "Inflate %s OOM (%zu bytes): heap %zu free/%zu max, PSRAM %zu free/%zu total", allocation, bytes,
          heap.freeBytes, heap.largestBlockBytes, psram.freeBytes, psram.totalBytes);
}
}  // namespace

InflateStream::~InflateStream() { deinit(); }

bool InflateStream::init(const bool streaming) {
  // Every consumer constructs a fresh stream per operation, so acquire storage
  // from scratch each init (releasing any prior backing first).
  deinit();

  const size_t stateSize = xp_inflate_state_size();
  const size_t alignment = xp_inflate_state_align();
  const size_t stateAligned = (stateSize + alignment - 1) & ~(alignment - 1);
  // State + window fit in the lent framebuffer. Absent or occupied scratch
  // falls back to two heap blocks, both released in deinit().
  const size_t needed = stateAligned + (streaming ? WINDOW_SIZE : 0);
  arenaBase = buildscratch::claim(needed);
  if (arenaBase) {
    state = arenaBase;
    window = streaming ? arenaBase + stateAligned : nullptr;
  } else {
    // Opaque Rust state is caller-owned; deinit()/the destructor frees both blocks.
    state = malloc(stateSize);
    if (!state) {
      logAllocationFailure("state", stateSize);
      return false;
    }
    if (streaming) {
      window = static_cast<uint8_t*>(malloc(WINDOW_SIZE));
      if (!window) {
        logAllocationFailure("window", WINDOW_SIZE);
        deinit();
        return false;
      }
    }
  }

  // Rust slices require initialized bytes, including unused ring history.
  if (window) memset(window, 0, WINDOW_SIZE);
  if (xp_inflate_init(state, stateSize, 0) != 0) {
    LOG_ERR("ZIP", "Invalid inflate state storage");
    deinit();
    return false;
  }
  windowPos = 0;
  pendingStart = 0;
  pendingLen = 0;
  inPtr = nullptr;
  inAvail = 0;
  fill = nullptr;
  fillCtx = nullptr;
  inputExhausted = false;
  finished = false;
  oneShotStart = nullptr;
  return true;
}

void InflateStream::deinit() {
  if (arenaBase) {
    buildscratch::release(arenaBase);
    arenaBase = nullptr;
  } else {
    free(state);
    free(window);
  }
  state = nullptr;
  window = nullptr;
}

void InflateStream::setZlibWrapped() {
  if (state) xp_inflate_init(state, xp_inflate_state_size(), 1);
}

void InflateStream::setSource(const uint8_t* src, const size_t len) {
  inPtr = src;
  inAvail = len;
  inputExhausted = true;  // the whole input is present; nothing more will come
}

void InflateStream::setFill(const FillFn fn, void* ctx) {
  fill = fn;
  fillCtx = ctx;
}

InflateStream::Status InflateStream::readAtMost(uint8_t* dest, const size_t maxLen, size_t* produced) {
  *produced = 0;
  if (!state) return Status::Error;

  const bool streaming = window != nullptr;
  if (!streaming) {
    if (!oneShotStart) oneShotStart = dest;
    // Only this new output region is cleared; prior back-reference bytes stay.
    if (maxLen) memset(dest, 0, maxLen);
  }

  for (;;) {
    // Drain window bytes left over from a previous decode call. In ring mode
    // The decoder may produce more than the caller asked for in one shot -- the
    // overshoot stays pending in the window until a later readAtMost.
    if (pendingLen > 0) {
      size_t n = maxLen - *produced;
      if (n > pendingLen) n = pendingLen;
      memcpy(dest + *produced, window + pendingStart, n);
      pendingStart += n;
      pendingLen -= n;
      *produced += n;
    }
    if (*produced == maxLen) {
      return (finished && pendingLen == 0) ? Status::Done : Status::Ok;
    }
    if (finished) return Status::Done;

    if (inAvail == 0 && !inputExhausted && fill) {
      inAvail = fill(fillCtx, &inPtr);
      if (inAvail == 0) inputExhausted = true;
    }

    size_t inBytes = 0;
    int32_t status;
    size_t outBytes = 0;
    if (streaming) {
      // Keep the full ring visible for back-references; drain excess output
      // through pendingLen rather than limiting the decoder's window.
      size_t position = windowPos;
      status = xp_inflate_step(state, inPtr, inAvail, !inputExhausted, window, WINDOW_SIZE, &position, 1, &inBytes,
                               &outBytes);
      pendingStart = windowPos;
      pendingLen = outBytes;
      windowPos += outBytes;
      if (windowPos == WINDOW_SIZE) windowPos = 0;
    } else {
      // One-shot: back-references resolve directly inside the destination buffer.
      size_t position = dest + *produced - oneShotStart;
      const size_t capacity = position + maxLen - *produced;
      status = xp_inflate_step(state, inPtr, inAvail, !inputExhausted, oneShotStart, capacity, &position, 0, &inBytes,
                               &outBytes);
      *produced += outBytes;
    }
    if (inBytes) inPtr += inBytes;
    inAvail -= inBytes;

    if (status == DONE) {
      finished = true;  // drain any pending window bytes on the next pass
      continue;
    }
    if (status < 0) return Status::Error;  // corrupt stream / adler mismatch
    // Refill when needed; exhausted input makes a truncated stream fail.
    if (status == NEEDS_INPUT && inputExhausted && inAvail == 0) {
      return Status::Error;
    }
    if (*produced == maxLen) {
      return (finished && pendingLen == 0) ? Status::Done : Status::Ok;
    }
  }
}

bool InflateStream::read(uint8_t* dest, const size_t len) {
  size_t total = 0;
  while (total < len) {
    size_t produced = 0;
    const Status status = readAtMost(dest + total, len - total, &produced);
    total += produced;
    if (status == Status::Error) return false;
    if (status == Status::Done) return total == len;
    if (produced == 0) return false;  // no progress safeguard
  }
  return true;
}
