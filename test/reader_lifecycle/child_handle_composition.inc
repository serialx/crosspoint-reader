// Physical child implementations; production factories and legacy callbacks are extracted unchanged.
struct GfxRenderer {};
struct MappedInputManager { bool hasRemoteClick() const { return false; } };
struct RenderLock {};
class ReaderActivity;
struct Activity {
  ActivityResult result;
  ActivityResultHandler resultHandler;
  std::string kind, name;
  std::vector<ResultPage*> pages;
  bool entered = false;
  explicit Activity(std::string value = "unused");
  virtual ~Activity();
  virtual void onEnter();
  virtual void loop();
  virtual void render(RenderLock&&);
  virtual void onExit();
  virtual void prepareForSleep();
  virtual bool isReaderActivity() const { return false; }
  // ACTUAL_BASE_CAPABILITY
  virtual bool acceptsRemoteInput() const { return false; }
  virtual bool preventAutoSleep() { return false; }
  virtual bool requiresExclusiveStorageLoop() const { return false; }
  virtual bool skipLoopDelay() { return false; }
  virtual bool handleHomeGesture() { return false; }
  virtual bool handleForcedRefresh() { return false; }
  virtual ScreenshotInfo getScreenshotInfo() const { return {}; }
  void startActivityForResult(std::unique_ptr<Activity>&&, ActivityResultHandler);
};
struct RustActivityAdapter : Activity { using Activity::Activity; };
struct ReaderChromeListActivity : RustActivityAdapter {
  using RustActivityAdapter::RustActivityAdapter;
  bool routingReady() const { return entered; } // Physical publication surrogate.
};
struct UtilityActivity : RustActivityAdapter { using RustActivityAdapter::RustActivityAdapter; };
struct KOReaderUiActivity : RustActivityAdapter { using RustActivityAdapter::RustActivityAdapter; };
struct ReaderActivity : RustActivityAdapter {
  using RustActivityAdapter::RustActivityAdapter;
  // ACTUAL_READER_CAPABILITY
  bool isReaderActivity() const final { return true; }
  bool acceptsRemoteInput() const final { return true; }
  virtual bool inputOverPage() const { return false; }
  virtual bool pageTurn(bool) { ++turns; return true; }
  virtual bool skipPages(int) { ++skips; return true; }
  unsigned turns = 0, skips = 0;
};
struct IntervalSelectionActivity : UtilityActivity {
  using UtilityActivity::UtilityActivity;
  bool readerActivity = true;
  // ACTUAL_INTERVAL_IDENTITY
};
// ACTUAL_XTC_BASE
// CHILD_TYPES
