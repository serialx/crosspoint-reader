#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include "DictZip.h"
#include "InflateReader.h"

namespace {
// Model four usable 8 KiB runs with allocation barriers between them.
bool fragmented = false;
size_t segmentsRemaining = 0;
size_t rejected = 0;
}  // namespace

void* operator new[](size_t bytes, const std::nothrow_t&) noexcept {
  if (fragmented) {
    if (bytes > 8192 || (bytes == 8192 && segmentsRemaining == 0)) {
      ++rejected;
      return nullptr;
    }
    if (bytes == 8192) --segmentsRemaining;
  }
  try {
    return ::operator new[](bytes);
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}

namespace {

// Raw deflate (no zlib/gzip wrapper) with the full 32KB window, as dictzip uses.
std::vector<uint8_t> deflateRaw(const std::vector<uint8_t>& in) {
  z_stream zs{};
  EXPECT_EQ(deflateInit2(&zs, 9, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY), Z_OK);
  std::vector<uint8_t> out(deflateBound(&zs, in.size()));
  zs.next_in = const_cast<uint8_t*>(in.data());
  zs.avail_in = static_cast<uInt>(in.size());
  zs.next_out = out.data();
  zs.avail_out = static_cast<uInt>(out.size());
  EXPECT_EQ(deflate(&zs, Z_FINISH), Z_STREAM_END);
  out.resize(zs.total_out);
  deflateEnd(&zs);
  return out;
}

// Incompressible noise interleaved with copies from 1..32767 bytes back, so the
// stream carries back-references at every distance class, including ones that
// straddle each ring-segment boundary and reach the far end of the window.
std::vector<uint8_t> makeLongDistanceData(size_t size) {
  std::vector<uint8_t> data;
  data.reserve(size);
  uint32_t lcg = 12345;
  const auto next = [&lcg] {
    lcg = lcg * 1103515245u + 12345u;
    return lcg >> 16;
  };
  while (data.size() < size) {
    for (int i = 0; i < 64 && data.size() < size; i++) data.push_back(static_cast<uint8_t>(next()));
    if (data.size() < 40000) continue;
    const size_t distance = 1 + next() % 32767;
    const size_t len = 3 + next() % 255;
    for (size_t i = 0; i < len && data.size() < size; i++) data.push_back(data[data.size() - distance]);
  }
  return data;
}

struct StreamCtx {
  InflateReader reader;
  const uint8_t* src = nullptr;
  size_t remaining = 0;
};

// Feeds input 2KB at a time, the way DictZip pulls compressed bytes from SD.
size_t feedCb(void* context, const uint8_t** data) {
  auto* ctx = static_cast<StreamCtx*>(context);
  const size_t n = std::min<size_t>(ctx->remaining, 2048);
  *data = ctx->src;
  ctx->src += n;
  ctx->remaining -= n;
  return n;
}

TEST(InflateReader, SegmentedRingDecodesFullWindowBackReferences) {
  const std::vector<uint8_t> plain = makeLongDistanceData(160 * 1024);
  const std::vector<uint8_t> packed = deflateRaw(plain);

  // Separate allocations: the decoder must never assume the segments are adjacent.
  std::unique_ptr<uint8_t[]> owned[InflateReader::RING_SEGMENTS];
  uint8_t* segments[InflateReader::RING_SEGMENTS];
  for (size_t i = 0; i < InflateReader::RING_SEGMENTS; i++) {
    owned[i] = std::make_unique<uint8_t[]>(InflateReader::RING_SEGMENT_BYTES);
    segments[i] = owned[i].get();
  }

  auto ctx = std::make_unique<StreamCtx>();
  ctx->src = packed.data();
  ctx->remaining = packed.size();
  ASSERT_TRUE(ctx->reader.initWithSegments(segments));
  ctx->reader.setFill(&feedCb, ctx.get());

  std::vector<uint8_t> out(plain.size());
  for (size_t pos = 0; pos < out.size(); pos += 512) {
    const size_t batch = std::min<size_t>(512, out.size() - pos);
    ASSERT_TRUE(ctx->reader.read(out.data() + pos, batch)) << "at " << pos;
  }
  EXPECT_EQ(out, plain);
}

TEST(InflateReader, InitWithSegmentsRejectsMissingSegment) {
  uint8_t a[InflateReader::RING_SEGMENT_BYTES];
  uint8_t* segments[InflateReader::RING_SEGMENTS] = {a, a, nullptr, a};
  InflateReader reader;
  EXPECT_FALSE(reader.initWithSegments(segments));
}

TEST(InflateReader, OneShotModeStillDecodes) {
  const std::vector<uint8_t> plain = makeLongDistanceData(48 * 1024);
  const std::vector<uint8_t> packed = deflateRaw(plain);

  InflateReader reader;
  ASSERT_TRUE(reader.init(false));
  reader.setSource(packed.data(), packed.size());
  std::vector<uint8_t> out(plain.size());
  ASSERT_TRUE(reader.read(out.data(), out.size()));
  EXPECT_EQ(out, plain);
}

TEST(InflateReader, ContiguousStreamingRingStillDecodes) {
  const std::vector<uint8_t> plain = makeLongDistanceData(96 * 1024);
  const std::vector<uint8_t> packed = deflateRaw(plain);

  auto ctx = std::make_unique<StreamCtx>();
  ctx->src = packed.data();
  ctx->remaining = packed.size();
  ASSERT_TRUE(ctx->reader.init(true));
  ctx->reader.setFill(&feedCb, ctx.get());

  std::vector<uint8_t> out(plain.size());
  for (size_t pos = 0; pos < out.size(); pos += 512) {
    const size_t batch = std::min<size_t>(512, out.size() - pos);
    ASSERT_TRUE(ctx->reader.read(out.data() + pos, batch)) << "at " << pos;
  }
  EXPECT_EQ(out, plain);
}

TEST(InflateReader, DictZipExtractsWithOnlyEightKiBRuns) {
  const auto plain = makeLongDistanceData(55 * 1024);
  const auto compressed = deflateRaw(plain);
  ASSERT_LT(compressed.size(), 65536u);

  // One random-access chunk. Tests the actual parse, refill and slice bridge.
  Storage.temporaryPath = "dictionary.dz";
  auto& encoded = Storage.temporary;
  encoded = {0x1f, 0x8b, 8, 4, 0, 0, 0, 0, 0, 0, 12, 0, 'R', 'A', 8, 0, 1, 0};
  const auto le16 = [&](size_t value) {
    encoded.push_back(static_cast<uint8_t>(value));
    encoded.push_back(static_cast<uint8_t>(value >> 8));
  };
  le16(plain.size());
  le16(1);
  le16(compressed.size());
  encoded.insert(encoded.end(), compressed.begin(), compressed.end());
  const uint32_t crc = crc32(0, plain.data(), static_cast<uInt>(plain.size()));
  le16(crc);
  le16(crc >> 16);
  le16(plain.size());
  le16(0);

  HalFile output;
  output.markOpen();
  DictZip::ExtractError error{};
  constexpr size_t offset = 31 * 1024;
  constexpr size_t size = 22 * 1024;
  segmentsRemaining = 4;
  rejected = 0;
  fragmented = true;
  const bool ok = DictZip::extractEntry(Storage.temporaryPath.c_str(), offset, size, output, &error);
  fragmented = false;

  EXPECT_TRUE(ok);
  EXPECT_EQ(error, DictZip::ExtractError::None);
  EXPECT_EQ(segmentsRemaining, 0u);
  EXPECT_EQ(rejected, 0u);
  EXPECT_EQ(output.bytes, (std::vector<uint8_t>{plain.begin() + offset, plain.begin() + offset + size}));
}

}  // namespace
