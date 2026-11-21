#include "util/ScreenshotInfo.h"
namespace FsHelpers {
static void sanitizePathComponentForFat32(const char* in, char* out, size_t size) { snprintf(out, size, "%s", in); }
}  // namespace FsHelpers
struct ScreenshotUtil {
  static void buildFilename(const ScreenshotInfo&, char*, size_t);
};
// ACTUAL_SCREENSHOT_FILENAME
static XpAppScreenshot boundaryView;
struct ActivityManager {
  struct {
    void (*screenshot_info)(XpAppScreenshotSink, void*);
  } compatibility{[](XpAppScreenshotSink sink, void* context) { sink(context, &boundaryView); }};
  ScreenshotInfo getScreenshotInfo() const;
} activityManager;
// ACTUAL_SCREENSHOT_INFO
struct XtcReaderActivity {
  struct {
    uint32_t page;
  } state;
  struct Book {
    std::string getTitle() const { return "XTC"; }
    uint32_t getPageCount() const { return 200; }
    uint8_t calculateProgress(uint32_t page) const { return xp_book_xtc_progress(1, page, getPageCount()); }
  };
  Book* xtc;
  ScreenshotInfo getScreenshotInfo() const;
};
// ACTUAL_XTC_PROGRESS
namespace pluginevents {
enum class Event { ReaderExit, SleepEnter };
struct Var {
  const char* key;
  const char* value;
};
static int sleepPercent = -1, sleepEndPercent = -1, exitPercent = -1;
static void emit(Event event, const Var* vars, size_t count) {
  bool found = false;
  for (size_t i = 0; i < count; ++i) {
    const std::string key = vars[i].key;
    if (key == "percent") {
      const int value = std::stoi(vars[i].value);
      assert(value >= 0 && value <= 100);
      (event == Event::ReaderExit ? exitPercent : sleepPercent) = value;
      found = true;
    } else if (event == Event::SleepEnter && key == "end_percent") {
      sleepEndPercent = std::stoi(vars[i].value);
    }
  }
  assert(found);
}
}  // namespace pluginevents
struct {
  std::string openEpubPath = "/book.epub";
} APP_STATE;
// ACTUAL_EXIT
static void emitSleep() {
  // ACTUAL_SLEEP
}
static void checkBoundaryConsumers(const XpAppScreenshot& view, int32_t exit) {
  boundaryView = view;
  const auto info = activityManager.getScreenshotInfo();
  assert(info.progressPercent == view.progress_percent);
  char filename[256];
  ScreenshotUtil::buildFilename(info, filename, sizeof(filename));
  const std::string percent = std::to_string(info.progressPercent);
  assert(info.progressPercent >= 0 && info.progressPercent <= 100);
  assert(std::string(filename).find("_" + percent + "pct_") != std::string::npos);
  emitSleep();
  emitExit(text(APP_STATE.openEpubPath), exit);
  assert(pluginevents::sleepPercent == info.progressPercent && pluginevents::exitPercent == info.progressPercent);
  assert(pluginevents::sleepEndPercent == view.end_progress_percent);
  XtcReaderActivity::Book book;
  XtcReaderActivity xtc{{198}, &book};
  assert(xtc.getScreenshotInfo().endProgressPercent == 99);
  xtc.state.page = 199;
  assert(xtc.getScreenshotInfo().endProgressPercent == 100);
  xtc.state.page = 200;
  assert(xtc.getScreenshotInfo().endProgressPercent == 100);
  printf("screenshot filename, sleep.enter and reader.exit: PASS (%s)\n", percent.c_str());
}
