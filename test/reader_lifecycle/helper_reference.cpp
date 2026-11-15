#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "PageLink.h"

static int lockDepth = 0;
struct RenderLock {
  RenderLock() {
    assert(lockDepth == 0);
    ++lockDepth;
    std::puts("lock");
  }
  ~RenderLock() {
    assert(lockDepth == 1);
    std::puts("unlock");
    --lockDepth;
  }
};
namespace HalDisplay {
constexpr int FAST_REFRESH = 1;
}
struct Renderer {
  bool async = false;
  void discardStoredBwBuffer() { std::printf("discard %d\n", lockDepth); }
  bool supportsAsyncRefresh() {
    std::printf("supports-async %d %d\n", async, lockDepth);
    return async;
  }
  void displayBufferAsync(int mode) { std::printf("async %d %d\n", mode, lockDepth); }
  void displayBuffer(int mode) { std::printf("sync %d %d\n", mode, lockDepth); }
  void cleanupGrayscaleWithFrameBuffer() { std::printf("cleanup %d\n", lockDepth); }
};
struct Popup {
  void render(Renderer&) { std::printf("popup %d\n", lockDepth); }
};
struct EpubReaderActivity {
  Renderer renderer;
  Popup overlayPopup;
  bool overlayPageStored = false;
  bool overlayRefreshPending = false;
  void discardOverlayPage();
  void pushOverlayRefresh();
  void settleOverlayRefresh();
  void paintOverlayPopup();
};
// EXTRACT_OVERLAY

struct {
  uint8_t orientation = 0;
} SETTINGS;
namespace ReaderUtils {
static Renderer* expectedRenderer = nullptr;
void applyOrientation(Renderer& renderer, uint8_t value) {
  assert(&renderer == expectedRenderer);
  std::printf("orientation-forward %u\n", unsigned(value));
}
}  // namespace ReaderUtils
struct ReaderActivity {
  Renderer renderer;
  int counter = 0;
  void setRefreshCounter(int value) {
    std::printf("counter-write %d\n", value);
    counter = value;
  }
  void disableFastInitialRefresh();
  void applyInitialOrientation();
};
// EXTRACT_BASE_HELPERS

namespace EpubReaderUtils {
// EXTRACT_LINK
}
namespace BookmarkUtil {
// EXTRACT_SANITIZER
}

static void overlayCases() {
  for (int operation = 0; operation != 4; ++operation) {
    for (int bits = 0; bits != 8; ++bits) {
      EpubReaderActivity reader;
      reader.overlayPageStored = bits & 1;
      reader.overlayRefreshPending = bits & 2;
      reader.renderer.async = bits & 4;
      std::printf("case %d stored=%d pending=%d async=%d\n", operation, reader.overlayPageStored,
                  reader.overlayRefreshPending, reader.renderer.async);
      if (operation == 3) {
        reader.paintOverlayPopup();
      } else {
        RenderLock lock;
        switch (operation) {
          case 0:
            reader.discardOverlayPage();
            break;
          case 1:
            reader.pushOverlayRefresh();
            break;
          case 2:
            reader.settleOverlayRefresh();
            break;
        }
      }
      std::printf("state stored=%d pending=%d depth=%d\n", reader.overlayPageStored, reader.overlayRefreshPending,
                  lockDepth);
    }
  }
}

static void baseHelperCases() {
  for (int initial : {-19, -1, 0, 1, 7, 100}) {
    ReaderActivity reader;
    reader.counter = initial;
    std::printf("refresh initial=%d\n", initial);
    reader.disableFastInitialRefresh();
    std::printf("refresh final=%d\n", reader.counter);
  }
  for (int value : {0, 1, 2, 3, 4, 7, 127, 255}) {
    ReaderActivity reader;
    SETTINGS.orientation = static_cast<uint8_t>(value);
    ReaderUtils::expectedRenderer = &reader.renderer;
    std::printf("orientation initial=%d\n", value);
    reader.applyInitialOrientation();
    std::printf("orientation final=%u\n", unsigned(SETTINGS.orientation));
  }
}

static PageLink link(int x, int y, int width, int height) {
  PageLink result;
  result.x = static_cast<int16_t>(x);
  result.y = static_cast<int16_t>(y);
  result.width = static_cast<int16_t>(width);
  result.height = static_cast<int16_t>(height);
  return result;
}

static void linkProbe(int set, const std::vector<PageLink>& links, int x, int y, int left, int top) {
  const auto* found = EpubReaderUtils::linkAtPoint(links, x, y, left, top);
  const int index = found ? static_cast<int>(found - links.data()) : -1;
  std::printf("link set=%d xy=%d,%d margin=%d,%d result=%d\n", set, x, y, left, top, index);
}

static void linkCases() {
  int set = 0;
  const std::array<std::array<int, 2>, 3> margins{{{0, 0}, {7, 11}, {-9, -5}}};
  for (const auto& margin : margins) {
    linkProbe(set, {}, -1, -1, margin[0], margin[1]);
    linkProbe(set, {}, 10, 20, margin[0], margin[1]);
  }
  ++set;
  for (int width : {1, 2, 15, 28, 40}) {
    const std::vector<PageLink> links{link(10, 20, width, 5)};
    std::printf("bounds set=%d x=10 y=20 width=%d height=5\n", set, width);
    for (const auto& margin : margins) {
      // Integer sweeps include both sides of every slop/minimum-width edge.
      for (int x = -8; x <= 60; ++x) linkProbe(set, links, x + margin[0], 20 + margin[1], margin[0], margin[1]);
      for (int y = 10; y <= 40; ++y) linkProbe(set, links, 10 + margin[0], y + margin[1], margin[0], margin[1]);
      linkProbe(set, links, -30, -30, margin[0], margin[1]);
    }
    ++set;
  }
  const std::vector<PageLink> overlap{link(10, 20, 2, 5), link(10, 20, 28, 5), link(-12, -8, 4, 3)};
  std::printf("bounds set=%d overlap=10,20,2,5;10,20,28,5;-12,-8,4,3\n", set);
  for (const auto& margin : margins) {
    for (int x = -30; x <= 60; ++x) {
      linkProbe(set, overlap, x + margin[0], 20 + margin[1], margin[0], margin[1]);
      linkProbe(set, overlap, x + margin[0], -8 + margin[1], margin[0], margin[1]);
    }
  }
}

static void hex(const std::string& value) {
  for (unsigned char c : value) std::printf("%02x", unsigned(c));
}

static void sanitizeCases() {
  assert(std::setlocale(LC_CTYPE, "C"));
  std::printf("platform char-signed=%d locale=C\n", std::numeric_limits<char>::is_signed);
  std::vector<std::string> cases{"",
                                 "a",
                                 " ",
                                 "   ",
                                 " a ",
                                 " a  b ",
                                 "a\nb",
                                 "a \nb",
                                 "a\n b",
                                 "\t \na\r\n\v\fb \t\n",
                                 "a\t\t b\n\nc",
                                 "\n\n",
                                 "\r\n\t\v\f "};
  for (unsigned char whitespace : {9, 10, 11, 12, 13, 32}) {
    const std::string w(1, static_cast<char>(whitespace));
    cases.push_back(w);
    cases.push_back(w + w + w);
    cases.push_back(w + "word" + w);
    cases.push_back("a" + w + w + "b");
    cases.push_back("a" + w + "\n" + "b");
    cases.push_back("a\n" + w + "b");
  }
  for (size_t size : {71, 72, 73, 100}) {
    cases.emplace_back(size, 'x');
    cases.push_back("  " + std::string(size, 'y') + " \n");
  }
  // The baseline passes signed char directly to isspace; non-ASCII observations are host-specific.
  cases.push_back(std::string(71, 'a') + "\xc3\xa9" + "z");
  cases.push_back(std::string(70, 'b') + "\xe2\x82\xac" + "z");
  cases.push_back(std::string(71, 'c') + "\xf0\x9f\x93\x96" + "z");
  cases.push_back(" \xc3\xa9 \xe2\x82\xac \xf0\x9f\x93\x96 \n");
  for (size_t i = 0; i < cases.size(); ++i) {
    const auto result = BookmarkUtil::sanitizeBookmarkSummary(cases[i]);
    std::printf("sanitize %zu input=%zu:", i, cases[i].size());
    hex(cases[i]);
    std::printf(" output=%zu:", result.size());
    hex(result);
    std::putchar('\n');
  }
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string mode(argv[1]);
  if (mode == "overlay")
    overlayCases();
  else if (mode == "refresh-orientation")
    baseHelperCases();
  else if (mode == "link-hit")
    linkCases();
  else if (mode == "sanitize")
    sanitizeCases();
  else
    return 2;
}
