#include <HalStorage.h>
#include <Xtc/XtcParser.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

#include "reference/XtcParser.h"

static size_t cases = 0;
static std::string context;
static void check(bool ok, const char* what) {
  if (!ok) {
    std::cerr << context << ": " << what << '\n';
    std::exit(1);
  }
}

// Same source interpretation and plane predicates as XtcReaderActivity::renderPage.
static std::vector<uint8_t> pixels(const std::vector<uint8_t>& data, uint16_t w, uint16_t h, uint8_t depth) {
  std::vector<uint8_t> out;
  const size_t required = depth == 2 ? size_t(w) * ((h + 7) / 8) * 2 : size_t((w + 7) / 8) * h;
  if (data.size() < required || size_t(w) * h > 1000000) return out;
  out.reserve(size_t(w) * h * 3);
  for (uint16_t y = 0; y < h; ++y) {
    for (uint16_t x = 0; x < w; ++x) {
      if (depth == 1) {
        out.push_back(!((data[size_t(y) * ((w + 7) / 8) + x / 8] >> (7 - x % 8)) & 1));
      } else {
        const size_t offset = size_t(w - 1 - x) * ((h + 7) / 8) + y / 8;
        const auto pv = (((data[offset] >> (7 - y % 8)) & 1) << 1) | ((data[offset + required / 2] >> (7 - y % 8)) & 1);
        out.push_back(pv >= 1);
        out.push_back(pv == 1);
        out.push_back(pv == 1 || pv == 2);
      }
    }
  }
  return out;
}

static void compare() {
  ++cases;
  xtc::XtcParser cand;
  reference_xtc::XtcParser base;
  check(cand.isValidXtcFile("file") == base.isValidXtcFile("file"), "magic probe");
  const auto a = cand.open("file");
  const auto b = base.open("file");
  check(int(a) == int(b), "open error");
  check(int(cand.getLastError()) == int(base.getLastError()), "last error");
  check(memcmp(&cand.getHeader(), &base.getHeader(), sizeof(xtc::XtcHeader)) == 0, "header");
  check(cand.getWidth() == base.getWidth() && cand.getHeight() == base.getHeight(), "dimensions");
  check(cand.getBitDepth() == base.getBitDepth(), "depth");
  if (a != xtc::XtcError::OK) return;
  check(cand.getTitle() == base.getTitle() && cand.getAuthor() == base.getAuthor(), "metadata");
  check(cand.hasChapters() == base.hasChapters(), "chapter flag before load");
  const auto& ac = cand.getChapters();
  const auto& bc = base.getChapters();
  check(ac.size() == bc.size(), "chapter count");
  for (size_t i = 0; i < ac.size(); ++i) {
    check(ac[i].name == bc[i].name && ac[i].startPage == bc[i].startPage && ac[i].endPage == bc[i].endPage,
          "chapter record");
  }
  check(cand.hasChapters() == base.hasChapters(), "chapter flag after load");
  // Bound test work for mutated counts; always probe the last and out-of-range entries too.
  std::vector<uint32_t> indices{0, 1, 2, uint32_t(cand.getPageCount() - 1), cand.getPageCount(), UINT32_MAX};
  for (auto index : indices) {
    xtc::PageInfo ai{};
    reference_xtc::PageInfo bi{};
    const auto av = cand.getPageInfo(index, ai), bv = base.getPageInfo(index, bi);
    check(av == bv, "page entry error");
    if (av)
      check(ai.offset == bi.offset && ai.size == bi.size && ai.width == bi.width && ai.height == bi.height &&
                ai.bitDepth == bi.bitDepth,
            "page entry");
    // Caller capacity is fixed, including for malicious dimensions.
    std::vector<uint8_t> abuf(100000, 0xa5), bbuf(abuf);
    const size_t an = cand.loadPage(index, abuf.data(), abuf.size());
    const size_t bn = base.loadPage(index, bbuf.data(), bbuf.size());
    check(an == bn && int(cand.getLastError()) == int(base.getLastError()), "page load/error");
    check(abuf == bbuf, "page bytes including partial reads");
    if (an) {
      abuf.resize(an);
      bbuf.resize(bn);
      check(pixels(abuf, ai.width, ai.height, ai.bitDepth) == pixels(bbuf, bi.width, bi.height, bi.bitDepth),
            "framebuffer planes");
    }
    std::vector<uint8_t> as, bs;
    const auto ae = cand.loadPageStreaming(
        index,
        [&](const uint8_t* p, size_t n, size_t offset) {
          check(offset == as.size(), "stream offset");
          as.insert(as.end(), p, p + n);
        },
        17);
    const auto be = base.loadPageStreaming(
        index,
        [&](const uint8_t* p, size_t n, size_t offset) {
          check(offset == bs.size(), "reference stream offset");
          bs.insert(bs.end(), p, p + n);
        },
        17);
    check(int(ae) == int(be) && as == bs, "stream bytes/error");
  }
  cand.close();
  base.close();
  check(!cand.isOpen() && !base.isOpen(), "close");
}

static void corpus(const std::filesystem::path& path, bool mutate) {
  std::ifstream file(path, std::ios::binary);
  const std::vector<uint8_t> input((std::istreambuf_iterator<char>(file)), {});
  check(!input.empty(), "empty input");
  xtcTestFile = input;
  context = path.string();
  compare();
  if (!mutate) return;
  for (size_t n = 0; n < input.size(); ++n) {
    xtcTestFile.assign(input.begin(), input.begin() + n);
    context = path.string() + " truncate " + std::to_string(n);
    compare();
  }
  for (size_t i = 0; i < input.size(); ++i) {
    for (uint8_t bit = 1; bit; bit <<= 1) {
      xtcTestFile = input;
      xtcTestFile[i] ^= bit;
      context = path.string() + " flip " + std::to_string(i) + "/" + std::to_string(bit);
      compare();
    }
  }
  for (size_t offset : {size_t(6), size_t(16), size_t(24), size_t(32), size_t(48)}) {
    for (uint64_t value :
         {uint64_t(0), uint64_t(1), uint64_t(47), uint64_t(48), uint64_t(55), uint64_t(56), uint64_t(input.size() - 1),
          uint64_t(input.size()), uint64_t(input.size() + 1), UINT64_MAX}) {
      xtcTestFile = input;
      const size_t bytes = offset == 6 ? 2 : 8;
      for (size_t j = 0; j < bytes && offset + j < input.size(); ++j) xtcTestFile[offset + j] = value >> (8 * j);
      context = path.string() + " boundary " + std::to_string(offset) + "/" + std::to_string(value);
      compare();
    }
  }
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  for (const auto& file : std::filesystem::directory_iterator(argv[1])) corpus(file.path(), true);
  for (int i = 2; i < argc; ++i) corpus(argv[i], false);
  std::cout << cases << " differential cases passed\n";
}
