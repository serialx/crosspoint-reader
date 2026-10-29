#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Synchronous, serialized request loan. Callbacks retain no Rust spans and cannot
// reenter these entry points. Spans are length-bounded; filesystem path pointers are NUL-terminated.
// Non-null buffer loans and XpWebUpload::buffer hold exactly this many bytes.
// C++ owns allocation/release; Rust never accesses beyond this capacity.
enum { XP_WEB_BUFFER_CAPACITY = 4096 };
typedef struct {
  const uint8_t* data;
  size_t len;
} XpWebText;
typedef enum {
  XP_WEB_EXISTS,
  XP_WEB_OPEN,
  XP_WEB_CLOSE,
  XP_WEB_IS_OPEN,
  XP_WEB_IS_DIR,
  XP_WEB_SIZE,
  XP_WEB_NEXT,
  XP_WEB_NAME,
  XP_WEB_READ,
  XP_WEB_WRITE,
  XP_WEB_MKDIR,
  XP_WEB_REMOVE,
  XP_WEB_RMDIR,
  XP_WEB_CLIENT_WRITE,
  XP_WEB_CLIENT_CLEAR,
  XP_WEB_CREATE,
  XP_WEB_AVAILABLE,
  XP_WEB_OPEN_READ,
  XP_WEB_ENSURE_DIR,
  XP_WEB_REPLACE  // path is source; data is a NUL-terminated target path.
} XpWebIo;
typedef enum { XP_WEB_CACHE, XP_WEB_REMOVE_BOOK, XP_WEB_MOVE_BOOK, XP_WEB_LIBRARY_BOOK } XpWebBook;
typedef enum {
  XP_WEB_LIST_OPEN,
  XP_WEB_LIST_NOT_DIR,
  XP_WEB_LIST_SCAN,
  XP_WEB_LIST_SKIP,
  XP_WEB_LIST_DONE,
  XP_WEB_STREAM_OOM,
  XP_WEB_FOLDER_UNSAFE,
  XP_WEB_FOLDER_PROTECTED,
  XP_WEB_FOLDER_CREATE,
  XP_WEB_FOLDER_DONE,
  XP_WEB_FOLDER_FAILED,
  XP_WEB_RENAME_DONE,
  XP_WEB_RENAME_FAILED,
  XP_WEB_MOVE_DONE,
  XP_WEB_MOVE_FAILED,
  XP_WEB_OOM
} XpWebDiagnostic;
// argument and path borrow the same scratch String; either call invalidates the
// previous span. A null argument key releases scratch; an empty key requests the
// URI. joined_path also uses this scratch. Callers copy before mutation.
typedef struct {
  XpWebText (*argument)(void*, const char*);
  void (*send)(void*, uint16_t, const char*, XpWebText);
  void (*header)(void*, const char*, XpWebText);
  void (*chunk)(void*, XpWebText);
  void (*length)(void*, size_t);
  size_t (*io)(void*, uint32_t, uint32_t, const char*, uint8_t*, size_t);
  bool (*sensitive)(void*, const char*);
  bool (*book)(void*, uint32_t, const char*, const char*);
  uint8_t* (*buffer)(void*, bool);
  void (*watchdog)(void*);
  void (*yield)(void*);
  uint32_t (*flags)(void*);
  ptrdiff_t (*paths)(void*, bool);
  XpWebText (*path)(void*, size_t);
  void (*diagnostic)(void*, uint32_t, XpWebText, XpWebText);
  const char* (*memory_error)(void*);
} XpWebServices;
typedef enum {
  XP_WEB_DOWNLOAD,
  XP_WEB_MKDIR_ROUTE,
  XP_WEB_RENAME,
  XP_WEB_MOVE,
  XP_WEB_DELETE,
  XP_WEB_LIST,
  XP_WEB_NOT_FOUND
} XpWebRoute;
// File slots 0..2 belong to this request; every open is closed before return.
bool xp_web_component(const uint8_t* data, size_t len);
void xp_web_files(void* ctx, const XpWebServices* services, uint32_t route);
void xp_web_stream(void* ctx, const XpWebServices* services);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  size_t size, buffer_pos, writes, last_logged;
  uint32_t started_at, write_time;
  uint8_t* buffer;
  bool success;
} XpWebUpload;
typedef struct {
  size_t total, received, last_progress, complete_size;
  uint32_t started_at, completed_at;
  uint8_t client;
  bool active;
} XpWebWsUpload;
typedef enum {
  XP_WEB_UPLOAD_NAME,
  XP_WEB_UPLOAD_PATH,
  XP_WEB_UPLOAD_ERROR,
  XP_WEB_WS_NAME,
  XP_WEB_WS_PATH,
  XP_WEB_WS_COMPLETE_NAME,
  XP_WEB_FONT_FAMILY,
  XP_WEB_FONT_PATH,
  XP_WEB_PLUGIN_PATH,
  XP_WEB_PLUGIN_TMP
} XpWebStoredText;
typedef enum {
  XP_WEB_UPLOAD_STOPPED,
  XP_WEB_UPLOAD_UNSAFE,
  XP_WEB_UPLOAD_START,
  XP_WEB_UPLOAD_HEAP,
  XP_WEB_UPLOAD_COLLISION,
  XP_WEB_UPLOAD_OOM,
  XP_WEB_UPLOAD_CREATE_FAILED,
  XP_WEB_UPLOAD_CREATED,
  XP_WEB_UPLOAD_FLUSH_FAILED,
  XP_WEB_UPLOAD_PROGRESS,
  XP_WEB_UPLOAD_COMPLETE,
  XP_WEB_UPLOAD_DIAGNOSTICS,
  XP_WEB_UPLOAD_ABORTED,
  XP_WEB_WS_DISCONNECT,
  XP_WEB_WS_CONNECT,
  XP_WEB_WS_TEXT,
  XP_WEB_WS_UNSAFE,
  XP_WEB_WS_BAD_SIZE,
  XP_WEB_WS_COLLISION,
  XP_WEB_WS_START,
  XP_WEB_WS_EMPTY,
  XP_WEB_WS_COMPLETE,
  XP_WEB_WS_REMOVED,
  XP_WEB_WS_REMOVE_FAILED
} XpWebTransferDiagnostic;
// Strings, files and upload buffers remain C++ owned. get borrows until set on
// that field; joined_path borrows NUL-terminated scratch until its next call
// (null on allocation failure). Scalars are copied back after Rust returns.
typedef struct {
  const XpWebServices* files;
  XpWebText (*get)(void*, uint32_t);
  void (*set)(void*, uint32_t, XpWebText);
  XpWebText (*joined_path)(void*, bool);
  uint32_t (*time)(void*);
  bool (*cancel)(void*);
  void (*diagnostic)(void*, uint32_t, XpWebText, XpWebText, size_t, size_t, size_t);
  // Returns a synchronous disconnect of this client; never reenters Rust.
  bool (*ws_send)(void*, uint8_t, XpWebText);
  // Translated ERROR: response, using checked transport-owned scratch.
  bool (*ws_memory_error)(void*, uint8_t);
} XpWebTransfers;
// Upload status: start=0, write=1, end=2, aborted=3, response=4.
void xp_web_upload(void*, const XpWebTransfers*, XpWebUpload*, uint32_t, XpWebText, const uint8_t*, size_t);
// WS status: disconnected=0, connected=1, text=2, binary=3, abort-from-stop=4.
void xp_web_ws(void*, const XpWebTransfers*, XpWebWsUpload*, uint8_t, uint32_t, const uint8_t*, size_t);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Settings/credential descriptors borrow their C++ owner for one serialized
// request. JSON strings borrow the parsed document; converted setting strings
// borrow scratch until the next json_value call. Setters copy before mutation.
typedef enum {
  XP_WEB_SETTINGS_GET,
  XP_WEB_SETTINGS_POST,
  XP_WEB_OPDS_GET,
  XP_WEB_OPDS_POST,
  XP_WEB_OPDS_DELETE,
  XP_WEB_WIFI_GET,
  XP_WEB_WIFI_POST,
  XP_WEB_WIFI_DELETE,
  XP_WEB_SETTINGS_PAGE
} XpWebConfigRoute;
typedef enum { XP_WEB_TOGGLE, XP_WEB_ENUM, XP_WEB_VALUE, XP_WEB_STRING, XP_WEB_ACTION } XpWebSettingKind;
typedef struct {
  XpWebText key, name, category, text;
  uint32_t kind, options;
  int32_t value, min, max, step;
  bool hidden, has_value, obfuscated, off_on;
} XpWebSetting;
typedef struct {
  XpWebText text;
  int32_t integer;
  bool present, string, is_int, array;
} XpWebJsonValue;
typedef struct {
  XpWebText name, value;
} XpWebHeader;
typedef struct {
  XpWebText name, url, username, password;
  XpWebHeader headers[2];
} XpWebOpds;
typedef struct {
  XpWebText ssid, password;
  bool has_password, last_connected;
} XpWebWifi;
typedef enum { XP_WEB_WIFI_SUMMARY, XP_WEB_WIFI_CREDENTIAL, XP_WEB_WIFI_SSID } XpWebWifiRead;
typedef enum {
  XP_WEB_CONFIG_PAGE,
  XP_WEB_CONFIG_SETTINGS,
  XP_WEB_CONFIG_SKIPPED,
  XP_WEB_CONFIG_APPLIED,
  XP_WEB_CONFIG_OPDS_LIST,
  XP_WEB_CONFIG_OPDS_UPDATED,
  XP_WEB_CONFIG_OPDS_ADDED,
  XP_WEB_CONFIG_OPDS_DELETED,
  XP_WEB_CONFIG_WIFI_LIST,
  XP_WEB_CONFIG_WIFI_UPDATED,
  XP_WEB_CONFIG_WIFI_ADDED,
  XP_WEB_CONFIG_WIFI_DELETED
} XpWebConfigDiagnostic;
typedef struct {
  XpWebText (*request_header)(void*, const char*);
  void (*send_asset)(void*, XpWebText);
  XpWebText asset, etag;
} XpWebAsset;
typedef struct {
  const XpWebServices* files;
  XpWebText (*parse)(void*, XpWebText);
  XpWebJsonValue (*json_value)(void*, const char*, int32_t header, bool convert);
  size_t (*count)(void*, uint32_t route);
  void (*setting)(void*, size_t, bool read_value, XpWebSetting*);
  XpWebText (*option)(void*, size_t, size_t);
  // STRING setters consume the last json_value(convert=true) scratch string.
  void (*set_setting)(void*, size_t, int32_t);
  void (*save_settings)(void*);
  bool (*opds)(void*, size_t, XpWebOpds*);
  bool (*put_opds)(void*, int32_t, const XpWebOpds*);
  void (*remove_opds)(void*, size_t);
  bool (*wifi)(void*, size_t, uint32_t read, XpWebWifi*);
  bool (*put_wifi)(void*, XpWebText, XpWebText);
  bool (*remove_wifi)(void*, XpWebText);
  XpWebAsset page;
  void (*diagnostic)(void*, uint32_t, size_t, XpWebText);
} XpWebConfig;
void xp_web_config(void*, const XpWebConfig*, uint32_t route);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Registry views and paths are borrowed until the next family/file callback.
// The path is NUL-terminated. Upload buffers and stored strings keep their
// existing C++ owner; scalar state is copied back after the serialized call.
typedef struct {
  XpWebText name, sizes;
  size_t files;
} XpWebFontFamily;
typedef struct {
  size_t bytes, buffer_pos;
  uint8_t* buffer;
  bool valid, magic_checked, vector;
} XpWebFontUpload;
typedef enum { XP_WEB_FONT_LIST, XP_WEB_FONT_DELETE, XP_WEB_FONT_PAGE } XpWebFontRoute;
typedef enum { XP_WEB_FONT_REFRESH, XP_WEB_FONT_MAX, XP_WEB_FONT_PSRAM, XP_WEB_FONT_DIRTY } XpWebFontRegistryOp;
typedef enum {
  XP_WEB_FONT_VALID_FAMILY,
  XP_WEB_FONT_VALID_CPFONT,
  XP_WEB_FONT_VALID_VECTOR,
  XP_WEB_FONT_ENSURE_DIR,
  XP_WEB_FONT_BUILD_PATH,
  XP_WEB_FONT_REMOVE_FAMILY
} XpWebFontInstallOp;
typedef enum {
  XP_WEB_FONT_SERVED,
  XP_WEB_FONT_BAD_FAMILY,
  XP_WEB_FONT_BAD_NAME,
  XP_WEB_FONT_DIR_FAILED,
  XP_WEB_FONT_OOM,
  XP_WEB_FONT_OPEN_FAILED,
  XP_WEB_FONT_STARTED,
  XP_WEB_FONT_BAD_MAGIC,
  XP_WEB_FONT_WRITE_FAILED,
  XP_WEB_FONT_ENDED,
  XP_WEB_FONT_ABORTED,
  XP_WEB_FONT_COMPLETE,
  XP_WEB_FONT_DELETED,
  XP_WEB_FONT_DELETE_FAILED,
  XP_WEB_FONT_JSON_OOM
} XpWebFontDiagnostic;
typedef struct {
  const XpWebTransfers* transfers;
  XpWebText (*parse)(void*, XpWebText);
  XpWebText (*json_string)(void*, const char*);
  size_t (*registry)(void*, uint32_t);
  void (*family)(void*, size_t, XpWebFontFamily*);
  XpWebText (*file)(void*, size_t, size_t);
  bool (*install)(void*, uint32_t, const char*, const char*, uint8_t*, size_t);
  XpWebAsset page;
  void (*diagnostic)(void*, uint32_t, XpWebText, XpWebText, size_t, bool);
} XpWebFonts;
void xp_web_fonts(void*, const XpWebFonts*, uint32_t route);
// Upload status matches xp_web_upload: start=0, write=1, end=2, aborted=3, response=4.
void xp_web_font_upload(void*, const XpWebFonts*, XpWebFontUpload*, uint32_t, XpWebText, const uint8_t*, size_t);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Entry spans borrow the PluginLocations result for this request. find/read
// share scratch until the next find/read; JSON spans borrow the parsed document.
// read(null) releases manifest scratch and its parsed document after serialization.
// Upload path/tmp get spans are NUL-terminated and retain their existing owner.
// Files retain their existing owner. Scalars return synchronously;
// error points to immutable static storage. Callback spans must not be retained.
typedef struct {
  XpWebText name, dir;
  bool js, manifest;
} XpWebPluginEntry;
typedef struct {
  size_t bytes;
  const char* error;
  uint16_t status;
  bool started;
} XpWebPluginUpload;
typedef enum { XP_WEB_PLUGIN_LIST, XP_WEB_PLUGIN_FILE, XP_WEB_PLUGIN_KEY, XP_WEB_PLUGIN_PAGE } XpWebPluginRoute;
typedef enum { XP_WEB_PLUGIN_REJECTED, XP_WEB_PLUGIN_SERVED, XP_WEB_PLUGIN_JSON_OOM } XpWebPluginDiagnostic;
typedef struct {
  const XpWebTransfers* transfers;
  XpWebText (*parse)(void*, XpWebText);
  XpWebText (*json_string)(void*, const char*);
  int64_t (*expires)(void*);
  size_t (*scan)(void*);
  void (*entry)(void*, size_t, XpWebPluginEntry*);
  XpWebText (*find)(void*, const char*);
  XpWebText (*read)(void*, const char*);
  int32_t (*decode)(XpWebText, uint8_t*, size_t);
  bool (*store)(const char*, const uint8_t*, size_t, int64_t);
  XpWebAsset page;
  void (*diagnostic)(void*, uint32_t, XpWebText, XpWebText);
} XpWebPlugins;
void xp_web_plugins(void*, const XpWebPlugins*, uint32_t route);
// Status: start=0, write=1, end=2, aborted=3, response=4 (no upload-data loan).
void xp_web_plugin_upload(void*, const XpWebPlugins*, XpWebPluginUpload*, uint32_t, const uint8_t*, size_t);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Opaque, inline Rust queue: six slots and two counters, no owned allocations.
// Initialize once at server construction; stop/begin retain it. Serialized calls
// loan it exclusively; callbacks must neither access it nor reenter job routes.
typedef struct {
  uint32_t words[674];
} XpWebJobQueue;
typedef enum { XP_WEB_JOB_SUBMIT, XP_WEB_JOB_CLAIM, XP_WEB_JOB_COMPLETE, XP_WEB_JOB_STATUS } XpWebJobRoute;
typedef enum {
  XP_WEB_JOB_QUEUED,
  XP_WEB_JOB_EXPIRED,
  XP_WEB_JOB_STALE,
  XP_WEB_JOB_DONE,
  XP_WEB_JOB_FAILED
} XpWebJobDiagnostic;
typedef struct {
  const XpWebServices* files;
  XpWebText (*parse)(void*, XpWebText);
  // C-string views of JSON fields, borrowed until the next parse.
  XpWebText (*string)(void*, const char*);
  uint32_t (*integer)(void*, const char*);
  bool (*boolean)(void*, const char*);
  // Null output measures non-null JSON (null returns zero); otherwise serializes.
  size_t (*json)(void*, const char*, uint8_t*, size_t);
  uint32_t (*time)(void*);
  void (*send)(void*, uint16_t, const char*);
  // Nullable: null suppresses diagnostics. Text is borrowed only for the call.
  void (*diagnostic)(void*, uint32_t, uint32_t, XpWebText, XpWebText);
} XpWebJobServices;
void xp_web_job_init(XpWebJobQueue*);
void xp_web_job_route(void*, const XpWebJobServices*, XpWebJobQueue*, uint32_t);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
  XP_WEB_RANDOM,
  XP_WEB_SHA1,
  XP_WEB_SHA256,
  XP_WEB_AES_ENCRYPT,
  XP_WEB_AES_DECRYPT,
  XP_WEB_RSA_PRIVATE,
  XP_WEB_RSA_GENERATE,
  XP_WEB_RSA_PUBLIC,
  XP_WEB_RSA_SIGN,
  XP_WEB_PKCS12
} XpWebCryptoOp;
// Synchronous request loan. JSON strings expire on release; primitives and
// serializers consume spans during the callback and never retain them.
typedef struct {
  const XpWebServices* files;
  XpWebText (*parse)(void*, XpWebText);
  // Null means missing/non-string; non-null views end at the first NUL.
  XpWebText (*string)(void*, const char*);
  void (*send)(void*, uint16_t, const char*);
  void (*release)(void*, bool arguments);
} XpWebRpc;
typedef struct {
  const XpWebRpc* request;
  void (*prepare)(void*, XpWebText url, XpWebText method, XpWebText body);
  void (*suspend)(void*, bool);
  int32_t (*request_tls)(void*, size_t limit);
  size_t (*header_count)(void*);
  XpWebHeader (*header)(void*, size_t);
  void (*append_header)(void*, XpWebHeader);
  void (*respond)(void*, int32_t upstream_status, const char* status_header, const char* headers_header,
                  uint16_t status, const char* content_type);
} XpWebRelay;
// Four C++ owned checked byte buffers live only for this request. allocate
// replaces a slot; clear releases all buffers, primitive results and password.
// call returns negative primitive errors (AES -2 means output/context OOM),
// otherwise output length or 1 for bool/void primitives. output borrows until clear.
typedef struct {
  const XpWebRpc* request;
  void (*initialize)(void*);
  int32_t (*integer)(void*, const char*, int32_t fallback);
  uint8_t* (*allocate)(void*, uint32_t slot, size_t);
  int32_t (*decode)(XpWebText, uint8_t*, size_t);
  ptrdiff_t (*call)(void*, uint32_t, XpWebText, XpWebText, XpWebText, uint8_t*, size_t);
  XpWebText (*output)(void*, uint32_t);
  XpWebText (*last_error)(void*);
  void (*password)(void*, XpWebText);
  bool (*encode)(void*, const char*, XpWebText);
  // linked requires one immutable, NUL-terminated string; otherwise copies all parts.
  void (*error)(void*, const XpWebText*, size_t count, bool linked);
  void (*clear)(void*);
  bool (*overflowed)(void*);
  void (*send_response)(void*);
  void (*oom)(void*, const char*);
} XpWebCrypto;
void xp_web_relay(void*, const XpWebRelay*);
void xp_web_crypto(void*, const XpWebCrypto*);
#ifdef __cplusplus
}
#endif
