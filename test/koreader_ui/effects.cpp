#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <variant>
#include "CrossPointPosition.h"
#include "KOReaderSyncClient.h"
#include "ProgressComparison.h"

void trace(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  puts("");
}
#define LOG_DBG(origin, ...) trace("DBG " origin " " __VA_ARGS__)
#define LOG_ERR(origin, ...) trace("ERR " origin " " __VA_ARGS__)
#define tr(id) #id
struct Scenario {
  bool credentials = true, smart = false, plugin = false, connected = true, wifi = true;
  bool hash = true, altHash = true, sameHash = false, clippings = false, clipOk = true, load = true, save = true;
  bool metadata = false, rich = true, extended = false, alternate = false;
  int primary = 0, secondary = 6, upload = 0, auth = 0, comparison = 3;
  uint32_t now = 100;
} scenario;
enum class DocumentMatchMethod : uint8_t { FILENAME, BINARY };
enum class KOReaderSyncBehavior : uint8_t { ASK_EVERY_TIME, SMART };
enum class KOReaderServerType : uint8_t { CROSSPOINT, KOSYNC, OTHER };
struct HttpHeader { std::string name, value; };
void parseHttpHeaderLine(const std::string& line, HttpHeader& out) {
  auto colon=line.find(':'); out.name=line.substr(0,colon); out.value=colon==std::string::npos?"":line.substr(colon+1);
}
std::string formatHttpHeaderLine(const HttpHeader& h, bool=false) { return h.name.empty()?"":h.name+": "+h.value; }
struct {
  std::string username="user", password="secret", url;
  std::array<HttpHeader,2> headers{};
  KOReaderServerType server=KOReaderServerType::CROSSPOINT;
  const std::string& getUsername() { return username; }
  const std::string& getPassword() { return password; }
  const std::string& getServerUrl() { return url; }
  KOReaderServerType getServerType() { return server; }
  const auto& getCustomHeaders() { return headers; }
  void setCredentials(const std::string& u,const std::string& p) {trace("credentials [%s] [%s]",u.c_str(),p.c_str());username=u;password=p;}
  void setServerUrl(const std::string& v) {trace("server-url [%s]",v.c_str());url=v;}
  void setServerType(KOReaderServerType v) {trace("server-type %u",unsigned(v));server=v;}
  void setCustomHeader(size_t i,const std::string& n,const std::string& v) {trace("header %zu [%s] [%s]",i,n.c_str(),v.c_str());headers[i]={n,v};}
  void setMatchMethod(DocumentMatchMethod v) {trace("matching %u",unsigned(v));}
  void setSendMetadata(bool v) {trace("send-metadata %d",v);}
  void setSyncBehavior(KOReaderSyncBehavior v) {trace("behavior %u",unsigned(v));}
  void setSyncClippings(bool v) {trace("send-clippings %d",v);}
  void saveToFile() {trace("credentials-save");}
  bool hasCredentials() const { return scenario.credentials; }
  bool getSyncClippings() const { return scenario.clippings; }
  bool getSendMetadata() const { return scenario.metadata; }
  bool supportsRichProgress() const { return scenario.rich; }
  bool supportsExtendedMetadata() const { return scenario.extended; }
  DocumentMatchMethod getMatchMethod() const { return DocumentMatchMethod::FILENAME; }
  KOReaderSyncBehavior getSyncBehavior() const {
    return scenario.smart ? KOReaderSyncBehavior::SMART : KOReaderSyncBehavior::ASK_EVERY_TIME;
  }
} KOREADER_STORE;
struct GfxRenderer {
  struct FrameBufferLoan {
    explicit FrameBufferLoan(GfxRenderer&) { trace("loan"); }
    ~FrameBufferLoan() { trace("unloan"); }
  };
};
struct MappedInputManager {
  enum class Button { Back, Confirm, Up, Down, Left, Right };
  unsigned pressed = 0, released = 0;
  bool tapped = false;
  bool wasPressed(Button b) { return pressed & (1u << unsigned(b)); }
  bool wasReleased(Button b) { return released & (1u << unsigned(b)); }
  bool wasScreenTapped(int&, int&) { return tapped; }
};
struct RenderLock { RenderLock() {} template <typename T> explicit RenderLock(T&) {} };
struct KeyboardResult { std::string text; };
struct ActivityResult { bool isCancelled = false; std::variant<KeyboardResult> data; };
struct Activity {
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  std::function<void(const ActivityResult&)> wifiCallback;
  virtual ~Activity() = default;
  void onEnter() { trace("enter"); }
  void onExit() { trace("exit"); }
  void requestUpdate(bool immediate = false) { trace("repaint %d", immediate); }
  void requestUpdateAndWait() { trace("repaint-wait"); }
  void finish() { trace("finish"); }
  template <typename T, typename F> void startActivityForResult(std::unique_ptr<T>, F f) {
    trace("wifi-select"); wifiCallback = f;
  }
};
struct WifiSelectionActivity { WifiSelectionActivity(GfxRenderer&, MappedInputManager&) {} };
constexpr int WL_CONNECTED = 1, WIFI_MODE_NULL = 0;
struct {
  int status() { return scenario.connected; }
  int getMode() { return scenario.connected; }
  void setSleep(bool value) { trace("wifi-sleep %d", value); }
  void disconnect(bool value) { trace("wifi-disconnect %d", value); }
} WiFi;
void esp_wifi_stop() { trace("wifi-stop"); }
void delay(int value) { trace("delay %d", value); }
void silentRestart() { trace("restart"); }
void silentRestartToReader() { trace("restart-reader"); }
uint32_t millis() { return scenario.now; }
struct { unsigned getFreeHeap() { return 123456; } } ESP;
struct { int orientation = 0; } SETTINGS;
namespace ReaderUtils { void applyOrientation(GfxRenderer&, int value) { trace("orientation %d", value); } }
struct { void goToReader(const std::string& path) { trace("reader %s", path.c_str()); } } activityManager;
struct WifiPowerSaveGuard {
  WifiPowerSaveGuard() { trace("power-guard"); }
  ~WifiPowerSaveGuard() { trace("power-restore"); }
};
namespace pluginevents { void drain(GfxRenderer*) { trace("plugin-drain"); } }
namespace clippingSync {
bool run(const std::string& path, const std::string& hash) { trace("clippings %s %s", path.c_str(), hash.c_str()); return scenario.clipOk; }
}
struct {
  void readFileToString(const char*, const std::string& path, int limit, std::string& out) {
    trace("read %s %d", path.c_str(), limit); out = "{}";
  }
} Storage;
struct Epub {
  struct SyncMetadata { std::string isbn, asin, series; std::optional<float> seriesIndex; };
  Epub(const std::string& path, const char* cache) { trace("book-open %s %s", path.c_str(), cache); }
  ~Epub() { trace("book-close"); }
  void setupCacheDir() { trace("cache-dir"); }
  bool load(bool css, bool cache) { trace("book-load %d %d", css, cache); return scenario.load; }
  std::string getTitle() { return "Title"; }
  std::string getAuthor() { return "Author"; }
  bool loadSyncMetadata(SyncMetadata&) { trace("metadata"); return true; }
};
namespace EpubReaderUtils {
bool saveProgress(Epub&, int spine, int page, int zero, std::optional<uint32_t> offset) {
  trace("save %d %d %d %d %u", spine, page, zero, bool(offset), offset.value_or(0)); return scenario.save;
}
}
struct SavedProgressPosition { std::string xpath; float percentage; };
namespace KOReaderDocumentId {
std::string calculateFromFilename(const std::string&) { trace("hash-filename"); return scenario.hash ? "primary" : ""; }
std::string calculate(const std::string&) { trace("hash-binary"); return scenario.altHash ? (scenario.sameHash ? "primary" : "alternate") : ""; }
}
namespace ProgressMapper {
CrossPointPosition toCrossPoint(const std::shared_ptr<Epub>&, const SavedProgressPosition& pos, GfxRenderer&, int spine, int pages) {
  trace("map %s %.6f %d %d", pos.xpath.c_str(), pos.percentage, spine, pages);
  CrossPointPosition result{2, 3, 10}; result.hasVisibleTextOffset = true; result.visibleTextOffset = 77; return result;
}
std::optional<CrossPointPosition> fromRichPosition(const std::shared_ptr<Epub>&, const KOReaderRichPosition&, GfxRenderer&, bool same) {
  trace("map-rich %d", same); return std::nullopt;
}
}
ProgressComparison compareProgress(const CrossPointPosition&, float, const CrossPointPosition&, float) {
  trace("compare"); return static_cast<ProgressComparison>(scenario.comparison);
}
RemoteRecordChoice selectRemoteRecord(const CrossPointPosition&, float, const CrossPointPosition&, float) {
  trace("select-record"); return scenario.alternate ? RemoteRecordChoice::Alternate : RemoteRecordChoice::Primary;
}
int KOReaderSyncClient::lastHttpCode = 200;
KOReaderSyncClient::Error KOReaderSyncClient::authenticate() { trace("authenticate"); return Error(scenario.auth); }
KOReaderSyncClient::Error KOReaderSyncClient::createUser() { trace("register"); return Error(scenario.auth); }
KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& hash, KOReaderProgress& out) {
  trace("get %s", hash.c_str());
  out.progress = "/body/DocFragment[3]/body/p[4]"; out.percentage = 0.75f;
  return Error(hash == "primary" ? scenario.primary : scenario.secondary);
}
KOReaderSyncClient::Error KOReaderSyncClient::updateProgress(const KOReaderProgress& p) {
  trace("put %s %s %.6f rich=%d metadata=%d", p.document.c_str(), p.progress.c_str(), p.percentage, bool(p.position), bool(p.metadata));
  if (p.position) trace("rich %u %u %u %u %d %u %s",p.position->pctQ,p.position->spineIndex,p.position->pageNumber,p.position->totalPages,bool(p.position->paragraphIndex),p.position->paragraphIndex.value_or(0),p.position->xpath.c_str());
  if (p.metadata) trace("meta %s %s %s %s",p.metadata->filename.c_str(),p.metadata->title.c_str(),p.metadata->authors.c_str(),p.metadata->extraJson.c_str());
  return Error(scenario.upload);
}
const char* KOReaderSyncClient::errorString(Error e) {
  static const char* values[] = {"OK","NO_CREDENTIALS","NETWORK_ERROR","AUTH_FAILED","SERVER_ERROR","JSON_ERROR","NOT_FOUND","LOW_MEMORY","USER_EXISTS"};
  return values[e];
}
struct KOReaderSyncActivity : Activity {
  enum State { WIFI_SELECTION, CONNECTING, SYNCING, SHOWING_RESULT, UPLOADING, UPLOAD_COMPLETE, SYNC_COMPLETE, NO_REMOTE_PROGRESS, SYNC_FAILED, NO_CREDENTIALS };
  std::shared_ptr<Epub> epub;
  std::string epubPath = "/book.epub", localChapterName = "Chapter", statusMessage, documentHash;
  CrossPointPosition localPosition{1, 2, 10}, remotePosition{};
  State state = WIFI_SELECTION;
  bool hasRemoteProgress = false, wifiActivated = false, pluginEventsOnly = false;
  KOReaderProgress remoteProgress{};
  SavedProgressPosition localProgress{"/body/DocFragment[2]/body/p[3]",0.25f};
  int selectedOption = 0;
  uint32_t autoReturnAt = 0;
  static constexpr unsigned AUTO_RETURN_DELAY_MS = 1200;
  struct App { template<typename... T> void on(T...) {} template<typename... T> void setScreen(T...) {} bool invalidated() { return false; } } app;
  void resetUi() {}
  static void onResultRow() {}
  static void resultScreen() {}
  struct Route { bool routed = false; explicit operator bool() const { return false; } };
  Route routeTouch(MappedInputManager&) { return {}; }
  void ensureEpubLoaded(); void saveProgressAndReturn(int,int); void returnToReader();
  bool smartSyncEnabled() const; void markAutoReturn(); void completeAlreadySynced();
  void onWifiSelectionComplete(bool); void performSync(); void performUpload();
  void onEnter(); void onExit(); void chooseResultOption(); void startUpload(); void loop();
};
constexpr unsigned ACTION_ROW = 1;
struct KOReaderAuthActivity : Activity {
  enum class Mode { AUTHENTICATE, SIGN_UP };
  KOReaderAuthActivity() = default;
  KOReaderAuthActivity(GfxRenderer&, MappedInputManager&, Mode m=Mode::AUTHENTICATE):mode(m){trace("auth-open %u",unsigned(m));}
  enum State { WIFI_SELECTION, CONNECTING, AUTHENTICATING, SUCCESS, FAILED };
  Mode mode = Mode::AUTHENTICATE;
  State state = WIFI_SELECTION;
  std::string statusMessage, errorMessage;
  void onWifiSelectionComplete(bool); void performAuthentication(); void onEnter(); void onExit(); void loop();
};
enum class StrId { STR_SERVER_TYPE, STR_CUSTOM_HEADER_1, STR_CUSTOM_HEADER_2 };
struct { const char* get(StrId id) { return id==StrId::STR_CUSTOM_HEADER_1?"STR_CUSTOM_HEADER_1":"STR_CUSTOM_HEADER_2"; } } I18N;
static constexpr StrId serverTypeNames[]={StrId::STR_SERVER_TYPE,StrId::STR_SERVER_TYPE,StrId::STR_SERVER_TYPE};
constexpr int SERVER_TYPE_ITEMS=3;
enum { IDX_USERNAME, IDX_PASSWORD, IDX_SERVER_URL, IDX_SERVER_TYPE, IDX_CUSTOM_HEADER_1, IDX_CUSTOM_HEADER_2, IDX_MATCH_METHOD, IDX_SEND_METADATA, IDX_SYNC_BEHAVIOR, IDX_SYNC_CLIPPINGS, IDX_SIGN_UP, IDX_AUTHENTICATE };
enum class InputType { Text, Password, Url };
struct KeyboardEntryActivity {
  KeyboardEntryActivity(GfxRenderer&, MappedInputManager&, const char* label, const std::string& prefill, int limit, InputType type) {
    trace("keyboard %s [%s] %d %u",label,prefill.c_str(),limit,unsigned(type));
  }
};
struct OptionPopup {
  bool active=false;
  std::function<void(int)> callback;
  bool isActive() {return active;}
  void show(StrId,const StrId*,int count,int selected,std::function<void(int)> f) {trace("popup %d %d",count,selected);active=true;callback=f;}
};
struct KOReaderSettingsActivity : Activity {
  OptionPopup optionPopup;
  struct { void clearTapFlash() {trace("clear-flash");} } app;
  void activateIndex(int);
};
// METHODS
void runSync(const char* name, Scenario value, int choice = -1) {
  trace("CASE %s", name); scenario = value;
  KOReaderSyncActivity activity; activity.pluginEventsOnly = value.plugin;
  activity.onEnter();
  if (activity.wifiCallback) activity.wifiCallback({!value.wifi});
  trace("state %d %d %u %s",activity.state,activity.selectedOption,activity.autoReturnAt,activity.statusMessage.c_str());
  if (choice >= 0) { activity.selectedOption = choice; activity.mappedInput.released = 2; activity.loop(); }
  activity.mappedInput.released = 1; activity.loop();
  activity.onExit();
}
int main() {
  Scenario s;
  runSync("manual-remote",s,0); runSync("manual-local",s,1);
  s.save=false;runSync("save-failure",s,0);s={};
  s.credentials=false;runSync("no-credentials",s);s={};
  s.connected=false;s.wifi=false;runSync("wifi-cancel",s);s={};
  s.connected=false;runSync("wifi-select",s);s={};
  s.hash=false;runSync("hash-failure",s);s={};
  s.clippings=true;runSync("clippings",s);s.clipOk=false;runSync("clippings-failure",s);s={};
  for(int error=1;error<=8;++error){s.primary=error;runSync("fetch-result",s,error==6?1:-1);}s={};
  s.load=false;runSync("book-failure",s);s={};
  for(int comparison=0;comparison<4;++comparison){s.smart=true;s.comparison=comparison;runSync("smart",s);}s={};
  s.smart=true;s.primary=6;runSync("smart-missing",s);s.secondary=0;runSync("alternate-only",s);s.primary=0;runSync("both-primary",s);s.alternate=true;runSync("both-alternate",s);s.sameHash=true;runSync("same-hash",s);s.sameHash=false;s.altHash=false;runSync("alt-hash-failure",s);s={};
  s.plugin=true;s.credentials=false;runSync("plugin-only",s);s={};
  s.primary=6;s.metadata=true;s.extended=true;runSync("metadata",s,1);s.load=false;runSync("metadata-book-failure",s,1);s.load=true;s.rich=false;runSync("standard-server",s,1);s={};
  s.primary=6;s.upload=2;runSync("upload-failure",s,1);s={};
  for(bool signup:{false,true})for(int result=0;result<=8;++result){
    trace("CASE auth %d %d",signup,result);scenario={};scenario.auth=result;
    KOReaderAuthActivity activity;if(signup)activity.mode=KOReaderAuthActivity::Mode::SIGN_UP;
    activity.onEnter();trace("state %d %s %s",activity.state,activity.statusMessage.c_str(),activity.errorMessage.c_str());
    activity.mappedInput.pressed=1;activity.loop();activity.onExit();
  }
  for(int index=0;index<12;++index)for(bool cancel:{false,true})for(const std::string value:{"", "https://", "http://", "new-value", "X-Test: secret"}) {
    trace("CASE settings %d %d [%s]",index,cancel,value.c_str()); scenario={};
    KOREADER_STORE.username="user";KOREADER_STORE.password="secret";KOREADER_STORE.url="";KOREADER_STORE.server=KOReaderServerType::CROSSPOINT;
    KOReaderSettingsActivity settings;settings.activateIndex(index);
    if(settings.wifiCallback)settings.wifiCallback({cancel,KeyboardResult{value}});
    if(settings.optionPopup.callback&&!cancel)settings.optionPopup.callback(1);
  }
  for(int index:{10,11}){scenario.credentials=false;KOReaderSettingsActivity settings;trace("CASE disabled %d",index);settings.activateIndex(index);}
  trace("CASE auth-cancel");scenario={};scenario.connected=false;
  KOReaderAuthActivity auth;auth.onEnter();auth.wifiCallback({true});
  trace("state %d %s",auth.state,auth.errorMessage.c_str());auth.mappedInput.tapped=true;auth.loop();auth.onExit();
}
