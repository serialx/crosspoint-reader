#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
#define XP_PLUGIN_EVENT(name, id, wire)
#define XP_PLUGIN_LIMIT(name, value) XP_PLUGIN_##name = value,
#include "xp_plugin_contract.def"
#undef XP_PLUGIN_LIMIT
#undef XP_PLUGIN_EVENT
};
typedef enum {
  XP_PLUGIN_SUB_FULL,
  XP_PLUGIN_SUB_LONG,
  XP_PLUGIN_SUB_UNKNOWN,
  XP_PLUGIN_SUB_ADDED
} XpPluginSubscriptionDiagnostic;
typedef enum { XP_PLUGIN_DRAIN_REWRITE, XP_PLUGIN_DRAIN_FINISHED } XpPluginDrainDiagnostic;
typedef enum { XP_PLUGIN_VALUE_EMPTY, XP_PLUGIN_VALUE_STRING, XP_PLUGIN_VALUE_INTEGER } XpPluginValueKind;
typedef enum {
  XP_PLUGIN_TOKEN_FILE,
  XP_PLUGIN_TOKEN_PATH,
  XP_PLUGIN_CONFIG_FILE,
  XP_PLUGIN_AUTH_TYPE,
  XP_PLUGIN_AUTH_TOKEN_PATH,
  XP_PLUGIN_TOAST,
  XP_PLUGIN_DEST
} XpPluginManifestField;
// Calls are synchronous: ctx/vtables and input spans live until return. Callbacks
// must not retain borrowed pointers or reenter with active mutable contexts.
// Nested calls require independent contexts/JSON slots and valid outer borrows.
// Span data may be null only when len == 0. Null means absent where specified;
// a non-null zero-length span is an empty value, not absent. No NUL is implied.
// Callback inputs must be copied before return; encode/emit may pass stack data.
typedef struct {
  const uint8_t* data;
  size_t len;
} XpPluginSpan;
// view borrows the current string until replace; replace consumes both spans.
typedef struct {
  void* ctx;
  XpPluginSpan (*view)(void*);
  void (*replace)(void*, size_t, size_t, XpPluginSpan);
} XpPluginString;
extern const char* const xp_plugin_roots[3];
bool xp_plugin_root(XpPluginSpan);
bool xp_plugin_reuse_dir(XpPluginSpan, XpPluginSpan);
bool xp_plugin_version(XpPluginSpan, uint32_t out[3]);
bool xp_plugin_newer(XpPluginSpan, XpPluginSpan);
uint32_t xp_plugin_classify(bool browse, bool events);
uint32_t xp_plugin_picker(uint32_t kind, bool readme);
bool xp_plugin_discoverable(bool quarantined, bool js, bool device, bool manifest);
int32_t xp_plugin_subtitle(int32_t viewport, int32_t label_lines, int32_t label_height, int32_t value_height,
                           int32_t subtitle_height, int32_t padding);
size_t xp_plugin_split_count(XpPluginSpan);
void xp_plugin_split(XpPluginSpan, void*, void (*segment)(void*, XpPluginSpan));
void xp_plugin_substitute(const XpPluginString*, XpPluginSpan key, XpPluginSpan value, bool self_value);
void xp_plugin_encode(XpPluginSpan, void*, void (*append)(void*, XpPluginSpan));
bool xp_plugin_relative(XpPluginSpan);
int32_t xp_plugin_event(XpPluginSpan);
const char* xp_plugin_event_name(uint32_t);
uint8_t xp_plugin_event_mask(uint32_t);
bool xp_plugin_receive(size_t size, size_t len, size_t max, size_t* reserved);
bool xp_plugin_http_ok(int32_t status, bool failed, bool complete);
typedef struct {
  char name[XP_PLUGIN_SUB_NAME_BYTES];
  char dir[XP_PLUGIN_SUB_DIR_BYTES];
  uint8_t mask;
  uint8_t connect_mask;
} XpPluginSubscriber;
// name/dir remain valid through the subscription iteration, including release.
// parse opens event storage; event returns null at end, otherwise valid until
// release. connect reads the same index. release runs even after parse failure.
typedef struct {
  void* ctx;
  size_t (*count)(void*);
  bool (*device)(void*, size_t);
  XpPluginSpan (*name)(void*, size_t);
  XpPluginSpan (*dir)(void*, size_t);
  bool (*parse)(void*, size_t);
  XpPluginSpan (*event)(void*, size_t);
  bool (*connect)(void*, size_t);
  void (*release)(void*);
  void (*diagnostic)(void*, uint32_t, size_t, size_t);
} XpPluginSubscriptionOps;
void xp_plugin_subscriptions(XpPluginSubscriber table[XP_PLUGIN_MAX_EVENT_PLUGINS], const XpPluginSubscriptionOps*);
bool xp_plugin_any(const XpPluginSubscriber table[XP_PLUGIN_MAX_EVENT_PLUGINS], uint32_t event);
uint8_t xp_plugin_mask(const XpPluginSubscriber table[XP_PLUGIN_MAX_EVENT_PLUGINS], XpPluginSpan name);
typedef struct {
  void* ctx;
  XpPluginSpan (*read)(void*, const XpPluginSubscriber*);
} XpPluginConnectOps;
bool xp_plugin_wants_connect(const XpPluginSubscriber table[XP_PLUGIN_MAX_EVENT_PLUGINS], const XpPluginConnectOps*);
// Each entry point selects its operation family below. Unless documented,
// argument is zero and result is ignored. Boolean results are zero/nonzero.
// File sizes, counts and offsets use all 64 bits; HTTP results carry signed
// int32_t status in the low 32 bits (including negative transport errors).
typedef struct {
  void* ctx;
  uint64_t (*call)(void*, uint32_t operation, uint64_t argument);
} XpPluginOps;
// OPEN -> bool; SIZE -> bytes; WRITE -> written bytes of the caller's line;
// TRUNCATE(argument = previous size) rolls back a short write. Other results
// are ignored. OPEN precedes access; cap reset is CLOSE/REMOVE/OPEN; the caller
// owns final close. OPEN_ERROR/CAP/SHORT are diagnostic notifications only.
typedef enum {
  XP_PLUGIN_FILE_OPEN,
  XP_PLUGIN_FILE_CLOSE,
  XP_PLUGIN_FILE_REMOVE,
  XP_PLUGIN_FILE_SIZE,
  XP_PLUGIN_FILE_WRITE,
  XP_PLUGIN_FILE_TRUNCATE,
  XP_PLUGIN_FILE_FLUSH,
  XP_PLUGIN_FILE_OPEN_ERROR,
  XP_PLUGIN_FILE_CAP,
  XP_PLUGIN_FILE_SHORT
} XpPluginAppendOp;
void xp_plugin_append(const XpPluginOps*, size_t line_size);
typedef struct {
  void* ctx;
  bool (*deliver)(void*, XpPluginSpan);
  void (*remove)(void*);
  bool (*rewrite)(void*, size_t);
  void (*diagnostic)(void*, uint32_t, bool);
} XpPluginDrainOps;
void xp_plugin_drain_lines(XpPluginSpan, size_t* budget, const XpPluginDrainOps*);
// EXISTS/BACKUP/RENAME/RESTORE -> bool; other results ignored. The context owns
// destination/temp/backup paths. COMMIT and RESTORE_ERROR notify the caller;
// successful rename removes backup before COMMIT, failure attempts restoration.
typedef enum {
  XP_PLUGIN_INSTALL_EXISTS,
  XP_PLUGIN_INSTALL_REMOVE_BACKUP,
  XP_PLUGIN_INSTALL_BACKUP,
  XP_PLUGIN_INSTALL_RENAME,
  XP_PLUGIN_INSTALL_RESTORE,
  XP_PLUGIN_INSTALL_REMOVE_TEMP,
  XP_PLUGIN_INSTALL_COMMIT,
  XP_PLUGIN_INSTALL_RESTORE_ERROR
} XpPluginInstallOp;
int32_t xp_plugin_install_download(int32_t status, const XpPluginOps*);
// PREPARE creates request scratch; ALLOWED -> bool; DOWNLOAD/RESPONSE -> status.
// RELEASE destroys scratch after every attempt, including a rejected path,
// before MINT or retry. REJECTED is diagnostic. MINT -> bool and owns a fresh
// token; SAVE persists it (result ignored); ACCEPT_TOKEN adopts it and releases
// the duplicate. Retry follows adoption even if saving failed. TOAST runs only
// after success and RELEASE. Other operation results are ignored.
typedef enum {
  XP_PLUGIN_DELIVERY_PREPARE,
  XP_PLUGIN_DELIVERY_ALLOWED,
  XP_PLUGIN_DELIVERY_DOWNLOAD,
  XP_PLUGIN_DELIVERY_RESPONSE,
  XP_PLUGIN_DELIVERY_RELEASE,
  XP_PLUGIN_DELIVERY_REJECTED,
  XP_PLUGIN_DELIVERY_MINT,
  XP_PLUGIN_DELIVERY_SAVE,
  XP_PLUGIN_DELIVERY_ACCEPT_TOKEN,
  XP_PLUGIN_DELIVERY_TOAST
} XpPluginDeliveryOp;
bool xp_plugin_deliver(bool password_grant, bool download, bool show_toast, const XpPluginOps*);
// Slots are 0..XP_PLUGIN_JSON_SLOTS-1, initially root in slot 0. select/pair
// overwrite only `to`. select permits from == to and invalidates cached
// iteration of `to`; pair requires distinct slots. A non-null key selects an
// object field (even if empty); a null key selects the array index.
// pair returns false at end; otherwise writes `to` and key in insertion order.
// text returns null for non-string, non-null empty for an empty string. Returned
// text/key borrows last until document mutation/destruction; the adapter must
// not mutate the document. index implements the platform's atoi conversion.
// resolve replaces slot 0; request/headers preserve root and use slots 1/3;
// manifest may clobber every slot. Never reuse these slots in nested callbacks.
typedef struct {
  void (*select)(void*, uint32_t from, uint32_t to, XpPluginSpan key, size_t index);
  XpPluginSpan (*text)(void*, uint32_t slot);
  bool (*integer)(void*, uint32_t slot);
  bool (*null)(void*, uint32_t slot);
  bool (*pair)(void*, uint32_t from, uint32_t to, size_t index, XpPluginSpan* key);
  size_t (*index)(void*, XpPluginSpan);
} XpPluginJsonVtable;
typedef struct {
  void* ctx;
  const XpPluginJsonVtable* vtable;
} XpPluginJson;
typedef struct {
  void* ctx;
  void (*text)(void*, uint32_t field, XpPluginSpan);
  void (*header)(void*, XpPluginSpan key, XpPluginSpan value);
  void (*reserve)(void*, size_t count);
} XpPluginRequest;
void xp_plugin_resolve(const XpPluginJson*, XpPluginSpan);
// Request field order is URL, method, body. Headers retain JSON insertion order.
void xp_plugin_read_request(const XpPluginJson*, XpPluginSpan default_method, const XpPluginRequest*);
void xp_plugin_read_headers(const XpPluginJson*, const XpPluginRequest*);
uint32_t xp_plugin_value_kind(const XpPluginJson*);
bool xp_plugin_password(XpPluginSpan type, XpPluginSpan url);
bool xp_plugin_mint_status(int32_t);
// A scan owns one directory cursor. name is valid until next/root; append must
// copy retained names/paths. find candidates own their path until candidate or
// clear is called again. No cursor or path survives the enclosing API call.
typedef struct {
  void* ctx;
  bool (*root)(void*, size_t);
  bool (*next)(void*);
  bool (*directory)(void*);
  XpPluginSpan (*name)(void*);
  bool (*seen)(void*);
  void (*claim)(void*);
  void (*path)(void*, size_t);
  bool (*marker)(void*, const char*);
  void (*append)(void*, bool js, bool device, bool manifest);
} XpPluginLocationOps;
bool xp_plugin_scan(const XpPluginLocationOps*, bool existence_only);
typedef struct {
  void* ctx;
  bool (*candidate)(void*, size_t);
  bool (*quarantined)(void*);
  void (*clear)(void*);
} XpPluginFindOps;
void xp_plugin_find(const XpPluginFindOps*, bool enabled, bool quarantine);
// ALLOWED/READ/EXTRACT/OPEN -> bool; SERIALIZE -> byte count; WRITE -> bytes
// written. SHORT(argument = written count) is diagnostic. Others return nothing.
// Load orders ALLOWED/READ/EXTRACT; save invokes segment for nonempty path parts
// (last marks the value), then SERIALIZE/OPEN/WRITE/FLUSH. Short writes close and
// remove the partial file. The context owns normal close and document cleanup.
typedef enum {
  XP_PLUGIN_TOKEN_ALLOWED,
  XP_PLUGIN_TOKEN_READ,
  XP_PLUGIN_TOKEN_EXTRACT,
  XP_PLUGIN_TOKEN_SERIALIZE,
  XP_PLUGIN_TOKEN_OPEN,
  XP_PLUGIN_TOKEN_WRITE,
  XP_PLUGIN_TOKEN_FLUSH,
  XP_PLUGIN_TOKEN_CLOSE,
  XP_PLUGIN_TOKEN_REMOVE,
  XP_PLUGIN_TOKEN_SHORT
} XpPluginTokenOp;
bool xp_plugin_load_token(bool empty_file, const XpPluginOps*);
bool xp_plugin_save_token(bool empty_file, XpPluginSpan path, const XpPluginOps*,
                          void (*segment)(void*, XpPluginSpan, bool last));
typedef struct {
  void* ctx;
  void (*text)(void*, uint32_t field, XpPluginSpan value);
  // field 0 = auth, 1 = handler. Borrow this slot as a new adapter's root;
  // nested read_request must use independent slots. Copy fields before return.
  void (*request)(void*, uint32_t field, uint32_t json_slot, const char* method);
  void (*reserve)(void*, size_t);
  void (*begin_handler)(void*, int32_t event);
  void (*end_handler)(void*);
} XpPluginManifestOps;
void xp_plugin_manifest(const XpPluginJson*, const XpPluginManifestOps*);
typedef struct {
  const char* key;
  const char* value;
} XpPluginVar;
typedef struct {
  uint32_t sequence;
} XpPluginSequence;
typedef struct {
  void* ctx;
  uint32_t (*random)(void*);
  int64_t (*time)(void*);
  void (*text)(void*, const char* key, const char* value, bool static_key);
  void (*number)(void*, const char* key, int64_t);
  void (*object)(void*, const char* key);
  size_t (*serialize)(void*);
  void (*append)(void*, const XpPluginSubscriber*, size_t);
  void (*oversize)(void*, size_t);
} XpPluginEmitOps;
void xp_plugin_emit(const XpPluginSubscriber table[XP_PLUGIN_MAX_EVENT_PLUGINS], XpPluginSequence*, uint32_t event,
                    const XpPluginVar*, size_t count, const XpPluginEmitOps*);
typedef struct {
  void* ctx;
  // Groups: config, metadata, event vars. Result spans last until the next
  // pair call; replace must consume them synchronously without retaining them.
  bool (*pair)(void*, uint32_t group, size_t index, XpPluginSpan* key, XpPluginSpan* value);
  void (*replace)(void*, XpPluginSpan key, XpPluginSpan value, bool event_variable);
} XpPluginSubstitutionOps;
void xp_plugin_event_substitute(XpPluginSpan token, XpPluginSpan timestamp, XpPluginSpan id,
                                const XpPluginSubstitutionOps*);
typedef struct {
  void* ctx;
  bool (*exists)(void*);
  bool (*read)(void*);
  bool (*manifest)(void*);
  size_t (*handlers)(void*);
  void (*remove)(void*);
  void (*prepare)(void*);
  void (*lines)(void*, size_t*);
} XpPluginDrainFileOps;
void xp_plugin_drain_file(size_t* budget, const XpPluginDrainFileOps*);
void xp_plugin_drain_all(const XpPluginSubscriber table[XP_PLUGIN_MAX_EVENT_PLUGINS], size_t budget, void*,
                         void (*drain)(void*, const XpPluginSubscriber*, size_t*));
size_t xp_plugin_pick_handler(bool parsed, int32_t event, size_t count, void*, int32_t (*event_at)(void*, size_t));
typedef struct {
  void* ctx;
  void (*reserve)(void*, size_t);
  void (*append)(void*, XpPluginSpan key, uint32_t json_slot);
} XpPluginConfigOps;
void xp_plugin_config_fields(const XpPluginJson*, const XpPluginConfigOps*);
#ifdef __cplusplus
}
#endif
