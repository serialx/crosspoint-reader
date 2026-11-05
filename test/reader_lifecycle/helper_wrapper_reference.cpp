#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>

struct Input {};
struct Manager {};
static Manager activityManager;
struct ReaderActivity;
namespace ReaderUtils {
struct BackNavCallback {
  void* ctx;
  void (*fn)(void*);
};
static const Input* expectedInput;
static const Manager* expectedManager;
static const char* expectedPath;
static void* expectedContext;
static bool returnValue;
static bool invokeHome;
static unsigned calls;
bool handleBackNavigation(const Input& input, Manager& manager, const char* path, BackNavCallback callback) {
  ++calls;
  assert(&input == expectedInput && &manager == expectedManager);
  assert(path == expectedPath && std::string(path) == expectedPath);
  assert(callback.ctx == expectedContext && callback.fn);
  if (invokeHome) callback.fn(callback.ctx);
  return returnValue;
}
}  // namespace ReaderUtils
struct ReaderActivity {
  Input mappedInput;
  std::string bookPath;
  unsigned homeCalls = 0;
  mutable unsigned endCalls = 0, screenshotCalls = 0;
  bool endValue = false;
  int progress = 0;
  struct Screenshot {
    int progressPercent;
  };
  void onGoHome() { ++homeCalls; }
  bool endOfBookMenuActive() const {
    ++endCalls;
    return endValue;
  }
  Screenshot getScreenshotInfo() const {
    ++screenshotCalls;
    return {progress};
  }
  bool handleBackNavigation();
  // EXTRACT_INLINE_WRAPPERS
};
// EXTRACT_BACK_WRAPPER
// EXTRACT_CLAMP

int main() {
  for (const std::string path :
       {std::string{}, std::string("/synthetic/book.epub"), std::string("/synthetic/\xc3\xa9 book.epub")}) {
    for (bool value : {false, true}) {
      for (bool home : {false, true}) {
        ReaderActivity reader;
        reader.bookPath = path;
        ReaderUtils::expectedInput = &reader.mappedInput;
        ReaderUtils::expectedManager = &activityManager;
        ReaderUtils::expectedPath = reader.bookPath.c_str();
        ReaderUtils::expectedContext = &reader;
        ReaderUtils::returnValue = value;
        ReaderUtils::invokeHome = home;
        ReaderUtils::calls = 0;
        assert(reader.handleBackNavigation() == value);
        assert(ReaderUtils::calls == 1 && reader.homeCalls == unsigned(home));
        assert(reader.bookPath == path);
      }
    }
  }
  for (bool value : {false, true}) {
    ReaderActivity reader;
    reader.endValue = value;
    assert(reader.inputOverPage() == value && reader.endCalls == 1);
  }
  for (int value : {std::numeric_limits<int>::min(), -101, -1, 0, 1, 99, 100, 101, std::numeric_limits<int>::max()}) {
    ReaderActivity reader;
    reader.progress = value;
    assert(reader.getProgressPercent() == value && reader.screenshotCalls == 1);
  }
  constexpr struct {
    int input;
    int expected;
  } cases[]{
      {std::numeric_limits<int32_t>::min(), 0},  {-101, 0}, {-1, 0}, {0, 0}, {1, 1}, {99, 99}, {100, 100}, {101, 100},
      {std::numeric_limits<int32_t>::max(), 100}};
  static_assert(sizeof(int) == sizeof(int32_t));
  for (const auto& item : cases) assert(clampPercent(item.input) == item.expected);
}
