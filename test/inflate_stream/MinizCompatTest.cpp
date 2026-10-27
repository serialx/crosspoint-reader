#include <MinizConfig.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

extern const size_t SDK_MINIZ_LAYOUT[];

namespace {
using Bytes = std::vector<uint8_t>;
void word(Bytes& data, uint32_t value) {
  for (int i = 0; i < 4; ++i) data.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
Bytes load(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(file), {});
}

TEST(MinizCompat, SdkStreamingPatternMatchesC) {
  Bytes corpus;
  std::map<Bytes, uint32_t> outputs;
  for (const char* name : {"stored", "fixed", "dynamic", "empty"}) {
    for (bool wrapped : {false, true}) {
      auto original = load(std::string(INFLATE_FIXTURES) + "/" + name + (wrapped ? ".zlib" : ".raw"));
      ASSERT_FALSE(original.empty());
      std::vector<Bytes> variants{original};
      variants.emplace_back(original.begin(), original.end() - 1);
      auto bad = original;
      bad.front() = 0xff;
      variants.push_back(bad);
      if (wrapped) {
        bad = original;
        bad.back() ^= 0xff;
        variants.push_back(bad);
      }
      for (size_t variant = 0; variant < variants.size(); ++variant) {
        const auto& input = variants[variant];
        for (size_t inputChunk : {1u, 7u, 512u, 1024u, 2048u, 8192u}) {
          for (size_t outputChunk : {1u, 7u, 512u, 1024u, 2048u, 4096u, 8192u}) {
            mz_stream stream{};
            ASSERT_EQ(mz_inflateInit2(&stream, wrapped ? 15 : -15), MZ_OK);
            Bytes output, trace;
            std::array<uint8_t, 8192> buffer{};
            size_t offset = 0, totalIn = 0, totalOut = 0, previousProduced = 0;
            std::array<uint32_t, 3> previous{UINT32_MAX, 0, 0};
            uint32_t repeats = 0;
            auto flush = [&] {
              if (!repeats) return;
              for (auto value : previous) word(trace, value);
              word(trace, repeats);
            };
            int status = MZ_OK;
            // Mirrors the SDK's input refill and full-output drain loop.
            for (size_t calls = 0; calls < 300000; ++calls) {
              if (!stream.avail_in && previousProduced != outputChunk && offset < input.size()) {
                stream.next_in = input.data() + offset;
                stream.avail_in = std::min(inputChunk, input.size() - offset);
                offset += stream.avail_in;
              }
              const unsigned before = stream.avail_in;
              stream.next_out = buffer.data();
              stream.avail_out = outputChunk;
              status = mz_inflate(&stream, MZ_NO_FLUSH);
              const size_t produced = outputChunk - stream.avail_out;
              const size_t consumed = before - stream.avail_in;
              previousProduced = produced;
              totalIn += consumed;
              totalOut += produced;
              EXPECT_EQ(stream.total_in, totalIn);
              EXPECT_EQ(stream.total_out, totalOut);
              EXPECT_EQ(stream.next_out, buffer.data() + produced);
              output.insert(output.end(), buffer.begin(), buffer.begin() + produced);
              std::array<uint32_t, 3> row{static_cast<uint32_t>(status), static_cast<uint32_t>(consumed),
                                          static_cast<uint32_t>(produced)};
              if (row != previous) {
                flush();
                previous = row;
                repeats = 0;
              }
              ++repeats;
              if (status == MZ_STREAM_END || (status != MZ_OK && status != MZ_BUF_ERROR)) break;
              if (status == MZ_BUF_ERROR && produced == 0 && consumed == 0 && offset == input.size()) break;
            }
            flush();
            if (variant == 0) EXPECT_EQ(status, MZ_STREAM_END);
            if (variant == 1) EXPECT_EQ(status, MZ_BUF_ERROR);
            if (variant >= 2) EXPECT_EQ(status, MZ_DATA_ERROR);
            word(corpus, trace.size());
            corpus.insert(corpus.end(), trace.begin(), trace.end());
            auto [it, inserted] = outputs.emplace(output, outputs.size());
            word(corpus, it->second);
            word(corpus, inserted ? output.size() : UINT32_MAX);
            if (inserted) corpus.insert(corpus.end(), output.begin(), output.end());
            word(corpus, stream.adler);
            EXPECT_EQ(mz_inflateEnd(&stream), MZ_OK);
            EXPECT_EQ(stream.state, nullptr);
          }
        }
      }
    }
  }
  if (const char* path = std::getenv("RECORD_MINIZ_COMPAT_GOLDENS")) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(corpus.data()), corpus.size());
    ASSERT_TRUE(file.good());
  } else {
    auto expected = load(MINIZ_COMPAT_GOLDEN);
    ASSERT_EQ(corpus.size(), expected.size());
    const auto mismatch = std::mismatch(corpus.begin(), corpus.end(), expected.begin());
    EXPECT_EQ(mismatch.first, corpus.end()) << "golden byte offset " << (mismatch.first - corpus.begin());
  }
}

TEST(MinizCompat, InvalidHistoryRejectedAcrossChunkSchedules) {
  const std::vector<Bytes> inputs{{3, 2, 0}, {0x73, 4, 0x42, 0}, {3, 0x3e, 0}, {3, 0x7e, 0}};
  for (size_t index = 0; index < inputs.size(); ++index) {
    // Padding exercises the fast loop as well as the partial-input path.
    for (size_t padding : {0u, 64u}) {
      auto input = inputs[index];
      input.resize(input.size() + padding);
      for (size_t inputChunk : {1u, 7u, 4096u}) {
        for (size_t outputChunk : {1u, 257u, 4096u}) {
          mz_stream stream{};
          ASSERT_EQ(mz_inflateInit2(&stream, -15), MZ_OK);
          std::array<uint8_t, 4096> output{};
          size_t supplied = 0;
          int status = MZ_OK;
          for (unsigned calls = 0; calls < 128 && status >= 0; ++calls) {
            if (!stream.avail_in && supplied < input.size()) {
              stream.next_in = input.data() + supplied;
              stream.avail_in = std::min(inputChunk, input.size() - supplied);
              supplied += stream.avail_in;
            }
            stream.next_out = output.data();
            stream.avail_out = outputChunk;
            status = mz_inflate(&stream, MZ_NO_FLUSH);
          }
          EXPECT_EQ(status, MZ_DATA_ERROR);
          EXPECT_LE(stream.total_out, index == 1 ? 1u : 0u);
          EXPECT_EQ(mz_inflateEnd(&stream), MZ_OK);
        }
      }
    }
  }
}

TEST(MinizCompat, MatchesPinnedSdkStreamLayout) {
  const size_t layout[] = {sizeof(mz_stream),
                           offsetof(mz_stream, next_in),
                           offsetof(mz_stream, avail_in),
                           offsetof(mz_stream, total_in),
                           offsetof(mz_stream, next_out),
                           offsetof(mz_stream, avail_out),
                           offsetof(mz_stream, total_out),
                           offsetof(mz_stream, msg),
                           offsetof(mz_stream, state),
                           offsetof(mz_stream, zalloc),
                           offsetof(mz_stream, zfree),
                           offsetof(mz_stream, opaque),
                           offsetof(mz_stream, data_type),
                           offsetof(mz_stream, adler),
                           offsetof(mz_stream, reserved)};
  for (size_t i = 0; i < std::size(layout); ++i) EXPECT_EQ(layout[i], SDK_MINIZ_LAYOUT[i]);
}

struct Allocation {
  size_t calls = 0;
  size_t frees = 0;
  size_t bytes = 0;
  bool fail = false;
  static void* alloc(void* ctx, size_t items, size_t size) {
    auto& self = *static_cast<Allocation*>(ctx);
    ++self.calls;
    self.bytes = items * size;
    return self.fail ? nullptr : std::malloc(self.bytes);
  }
  static void release(void* ctx, void* ptr) {
    ++static_cast<Allocation*>(ctx)->frees;
    std::free(ptr);
  }
};
TEST(MinizCompat, AllocatorHooksAndFailureCleanup) {
  Allocation allocation;
  mz_stream stream{};
  stream.opaque = &allocation;
  stream.zalloc = Allocation::alloc;
  stream.zfree = Allocation::release;
  allocation.fail = true;
  EXPECT_EQ(mz_inflateInit2(&stream, -15), MZ_MEM_ERROR);
  EXPECT_EQ(stream.state, nullptr);
  EXPECT_EQ(allocation.frees, 0u);
  allocation.fail = false;
  EXPECT_EQ(mz_inflateInit2(&stream, -15), MZ_OK);
  EXPECT_EQ(allocation.calls, 2u);
  EXPECT_GT(allocation.bytes, 32768u);
  EXPECT_EQ(mz_inflateEnd(&stream), MZ_OK);
  EXPECT_EQ(mz_inflateEnd(&stream), MZ_OK);
  EXPECT_EQ(allocation.frees, 1u);
}
}  // namespace
