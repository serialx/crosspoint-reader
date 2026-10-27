#include <InflateStream.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;
Bytes load(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return Bytes(std::istreambuf_iterator<char>(file), {});
}
void word(Bytes& bytes, uint32_t value) {
  for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
struct Source {
  const Bytes& bytes;
  size_t chunk;
  size_t pos = 0;
  static size_t fill(void* ctx, const uint8_t** data) {
    auto& self = *static_cast<Source*>(ctx);
    *data = self.bytes.empty() ? nullptr : self.bytes.data() + self.pos;
    const size_t n = std::min(self.chunk, self.bytes.size() - self.pos);
    self.pos += n;
    return n;
  }
};
struct Result {
  Bytes trace;
  Bytes output;
};
std::vector<Result> decodeCorpus(const Bytes& corpus) {
  size_t pos = 0;
  auto readWord = [&] {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) result |= uint32_t(corpus.at(pos++)) << (8 * i);
    return result;
  };
  std::map<uint32_t, Bytes> outputs;
  std::vector<Result> results;
  while (pos < corpus.size()) {
    const size_t traceSize = readWord();
    Bytes trace(corpus.begin() + pos, corpus.begin() + pos + traceSize);
    pos += traceSize;
    const auto id = readWord();
    const auto size = readWord();
    if (size != UINT32_MAX) {
      outputs[id] = Bytes(corpus.begin() + pos, corpus.begin() + pos + size);
      pos += size;
    }
    results.push_back({std::move(trace), outputs.at(id)});
  }
  return results;
}

Result run(const Bytes& input, bool wrapped, bool streaming, size_t inChunk, size_t outChunk, size_t limit) {
  InflateStream stream;
  if (!stream.init(streaming)) std::abort();
  if (wrapped) stream.setZlibWrapped();
  Source source{input, inChunk};
  if (inChunk)
    stream.setFill(Source::fill, &source);
  else
    stream.setSource(input.data(), input.size());
  Bytes buffer(110000);
  Result result;
  uint32_t lastStatus = UINT32_MAX, lastSize = 0, repeats = 0;
  auto flush = [&] {
    if (repeats) {
      word(result.trace, lastStatus);
      word(result.trace, lastSize);
      word(result.trace, repeats);
    }
  };
  size_t total = 0;
  for (size_t step = 0; step < 120000; ++step) {
    size_t produced = 0;
    auto* dest = buffer.data() + (streaming ? 0 : total);
    const size_t wanted = std::min(outChunk, limit - total);
    auto status = stream.readAtMost(dest, wanted, &produced);
    result.output.insert(result.output.end(), dest, dest + produced);
    if (lastStatus != static_cast<uint32_t>(status) || lastSize != produced) {
      flush();
      lastStatus = static_cast<uint32_t>(status);
      lastSize = produced;
      repeats = 0;
    }
    ++repeats;
    total += produced;
    if (status != InflateStream::Status::Ok || total == limit || produced == 0) break;
  }
  flush();
  return result;
}

TEST(InflateGolden, BytesAndStatusesMatchOriginalC) {
  const std::string root = INFLATE_FIXTURES;
  Bytes corpus;
  std::map<Bytes, uint32_t> outputs;
  std::vector<bool> invalidDistanceCases;
  auto append = [&](const Result& result, bool invalidDistance) {
    invalidDistanceCases.push_back(invalidDistance);
    word(corpus, result.trace.size());
    corpus.insert(corpus.end(), result.trace.begin(), result.trace.end());
    auto [it, inserted] = outputs.emplace(result.output, outputs.size());
    word(corpus, it->second);
    word(corpus, inserted ? result.output.size() : UINT32_MAX);
    if (inserted) corpus.insert(corpus.end(), result.output.begin(), result.output.end());
  };
  for (const char* name : {"stored", "fixed", "dynamic", "empty"}) {
    for (bool wrapped : {false, true}) {
      auto original = load(root + "/" + name + (wrapped ? ".zlib" : ".raw"));
      ASSERT_FALSE(original.empty());
      std::vector<Bytes> variants{original};
      for (size_t cut : {size_t{0}, size_t{1}, original.size() / 2, original.size() - 1})
        variants.emplace_back(original.begin(), original.begin() + cut);
      auto corrupt = original;
      corrupt[0] = 0xff;
      variants.push_back(corrupt);
      corrupt = original;
      corrupt.back() ^= 0xff;
      variants.push_back(corrupt);
      for (const auto& input : variants) {
        for (size_t chunk : {size_t{0}, size_t{1}, size_t{7}, size_t{512}, size_t{1024}, size_t{8192}}) {
          for (bool streaming : {false, true}) {
            const bool invalidDistance = !wrapped && streaming && chunk != 0 && input == Bytes{3, 255};
            append(run(input, wrapped, streaming, chunk, chunk ? chunk : 8192, 110000), invalidDistance);
            for (size_t limit :
                 {size_t{1}, size_t{32767}, size_t{32768}, size_t{32769}, load(root + "/plain.bin").size()})
              append(run(input, wrapped, streaming, chunk, limit, limit), invalidDistance);
          }
        }
      }
    }
  }
  if (const char* path = std::getenv("RECORD_INFLATE_GOLDENS")) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(corpus.data()), corpus.size());
    ASSERT_TRUE(file.good());
  } else {
    const auto expected = load(INFLATE_GOLDEN);
    ASSERT_FALSE(expected.empty());
    const auto before = decodeCorpus(expected);
    const auto after = decodeCorpus(corpus);
    ASSERT_EQ(before.size(), 4032u);
    ASSERT_EQ(after.size(), before.size());
    for (size_t i = 0; i < before.size(); ++i) {
      SCOPED_TRACE("case " + std::to_string(i));
      // C accepts reserved distance symbols in wrapping mode and copies
      // unwritten history. Rust rejects them; those bytes have no stable value.
      if (invalidDistanceCases[i]) {
        EXPECT_EQ(after[i].trace, (Bytes{2, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}));
        EXPECT_TRUE(after[i].output.empty());
      } else {
        EXPECT_EQ(after[i].trace, before[i].trace);
        EXPECT_EQ(after[i].output, before[i].output);
      }
    }
  }
}
}  // namespace
