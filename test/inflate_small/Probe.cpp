#include <InflateReader.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Binary host protocol: six u32 fields, input bytes; reply i32, u32, u64, output.
struct Source {
  InflateReader reader;
  const uint8_t* data;
  size_t left;
  size_t chunk;
};

#ifdef UZLIB_ORACLE
static int fill(uzlib_uncomp* u) {
  auto* s = reinterpret_cast<Source*>(u);
  const size_t n = s->left < s->chunk ? s->left : s->chunk;
  if (!n) return -1;
  u->source = s->data + 1;
  u->source_limit = s->data + n;
  const int byte = *s->data;
  s->data += n;
  s->left -= n;
  return byte;
}
#else
static size_t fill(void* context, const uint8_t** data) {
  auto* s = static_cast<Source*>(context);
  const size_t n = s->left < s->chunk ? s->left : s->chunk;
  *data = s->data;
  s->data += n;
  s->left -= n;
  return n;
}
#endif

int main() {
  uint32_t header[6];
  while (fread(header, sizeof(header), 1, stdin) == 1) {
    const auto [inputSize, capacity, streaming, inputChunk, outputChunk, iterations] =
        std::array<uint32_t, 6>{header[0], header[1], header[2], header[3], header[4], header[5]};
    if (inputSize > 1000000 || capacity > 1000000 || !iterations || !inputChunk || !outputChunk) return 2;
    std::vector<uint8_t> input(inputSize), output(capacity + 16, 0xa5), ring(32768);
    if (fread(input.data(), 1, inputSize, stdin) != inputSize) return 3;
    Source source{};
    size_t total = 0;
    InflateStatus status = InflateStatus::Ok;
    const auto start = std::chrono::steady_clock::now();
    for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
      total = 0;
      source.data = input.data();
      source.left = input.size();
      source.chunk = inputChunk;
      if (streaming) {
        if (!source.reader.initWithRing(ring.data())) return 4;
#ifdef UZLIB_ORACLE
        source.reader.setReadCallback(fill);
#else
        source.reader.setFill(fill, &source);
#endif
      } else {
        if (!source.reader.init(false)) return 4;
        source.reader.setSource(input.data(), input.size());
      }
      do {
        size_t produced = 0;
        const size_t count = streaming ? std::min<size_t>(outputChunk, capacity - total) : capacity;
        status = source.reader.readAtMost(output.data() + total, count, &produced);
        total += produced;
        if (status != InflateStatus::Ok || !produced) break;
      } while (total < capacity);
    }
    const uint64_t elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - start).count();
    if (total > capacity) status = InflateStatus::Error;
    for (size_t i = capacity; i < output.size(); ++i) {
      if (output[i] != 0xa5) return 5;
    }
    const int32_t code = status == InflateStatus::Error ? -1 : status == InflateStatus::Done ? 1 : 0;
    const uint32_t produced = static_cast<uint32_t>(total);
    fwrite(&code, 4, 1, stdout);
    fwrite(&produced, 4, 1, stdout);
    fwrite(&elapsed, 8, 1, stdout);
    fwrite(output.data(), 1, total, stdout);
  }
}
