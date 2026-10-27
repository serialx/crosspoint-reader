#include <MinizConfig.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;
Bytes load(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(stream), {});
}
void word(Bytes& out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) out.push_back(value >> (i * 8));
}

TEST(PngInflate, IdatRowsAndPartialBuffersMatchC) {
  const auto fixtures = load(PNG_IDAT_FIXTURES);
  ASSERT_FALSE(fixtures.empty());
  size_t cursor = 0;
  auto readWord = [&] {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= uint32_t(fixtures.at(cursor++)) << (8 * i);
    return value;
  };
  auto blob = [&] {
    const auto size = readWord();
    Bytes value(fixtures.begin() + cursor, fixtures.begin() + cursor + size);
    cursor += size;
    return value;
  };
  Bytes trace;
  while (cursor < fixtures.size()) {
    const auto name = blob(), input = blob(), expected = blob();
    const auto pitch = readWord(), crc = readWord();
    SCOPED_TRACE(std::string(name.begin(), name.end()));
    EXPECT_EQ(mz_crc32(0, input.data(), input.size()), crc);
    const auto split = input.size() / 2;
    EXPECT_EQ(mz_crc32(mz_crc32(0, input.data(), split), input.data() + split, input.size() - split), crc);
    for (size_t inputChunk : {size_t{1}, size_t{7}, size_t{512}, size_t{2048}}) {
      for (size_t outputChunk : {size_t{1}, size_t{7}, size_t{pitch}, size_t{4096}}) {
        for (bool corrupt : {false, true}) {
          auto compressed = input;
          if (corrupt) compressed.back() ^= 1;
          mz_stream stream{};
          ASSERT_EQ(mz_inflateInit(&stream), MZ_OK);
          Bytes output, buffer(outputChunk);
          size_t offset = 0;
          int status = MZ_OK;
          std::array<uint32_t, 3> previous{UINT32_MAX, 0, 0};
          uint32_t repeats = 0;
          auto flush = [&] {
            if (!repeats) return;
            for (auto value : previous) word(trace, value);
            word(trace, repeats);
          };
          for (size_t calls = 0; calls <= expected.size() + input.size() + 100; ++calls) {
            if (!stream.avail_in && offset < compressed.size()) {
              stream.next_in = compressed.data() + offset;
              stream.avail_in = std::min(inputChunk, compressed.size() - offset);
              offset += stream.avail_in;
            }
            const auto before = stream.avail_in;
            stream.next_out = buffer.data();
            stream.avail_out = outputChunk;
            status = mz_inflate(&stream, MZ_NO_FLUSH);
            const auto produced = outputChunk - stream.avail_out;
            std::array<uint32_t, 3> row{static_cast<uint32_t>(status), before - stream.avail_in,
                                       static_cast<uint32_t>(produced)};
            if (row != previous) {
              flush();
              previous = row;
              repeats = 0;
            }
            ++repeats;
            output.insert(output.end(), buffer.begin(), buffer.begin() + produced);
            EXPECT_EQ(stream.total_out, output.size());
            if (status != MZ_OK && status != MZ_BUF_ERROR) break;
            if (!produced && before == stream.avail_in && offset == compressed.size()) break;
          }
          flush();
          EXPECT_EQ(status, corrupt ? MZ_DATA_ERROR : MZ_STREAM_END);
          if (!corrupt) EXPECT_EQ(output, expected);
          word(trace, stream.adler);
          EXPECT_EQ(mz_inflateEnd(&stream), MZ_OK);
        }
      }
    }
  }
  if (const auto* record = std::getenv("RECORD_PNG_INFLATE_GOLDENS")) {
    std::ofstream file(record, std::ios::binary);
    file.write(reinterpret_cast<const char*>(trace.data()), trace.size());
    ASSERT_TRUE(file.good());
  } else {
    const auto expected = load(PNG_INFLATE_GOLDEN);
    ASSERT_EQ(trace.size(), expected.size());
    const auto mismatch = std::mismatch(trace.begin(), trace.end(), expected.begin());
    EXPECT_EQ(mismatch.first, trace.end()) << "trace byte " << (mismatch.first - trace.begin());
  }
}
}  // namespace
