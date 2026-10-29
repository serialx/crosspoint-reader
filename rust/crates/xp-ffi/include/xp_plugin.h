#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
  XP_PLUGIN_SUB_FULL,
  XP_PLUGIN_SUB_LONG,
  XP_PLUGIN_SUB_UNKNOWN,
  XP_PLUGIN_SUB_ADDED
} XpPluginSubscriptionDiagnostic;
typedef enum { XP_PLUGIN_APPEND_OPEN, XP_PLUGIN_APPEND_CAP, XP_PLUGIN_APPEND_SHORT } XpPluginAppendDiagnostic;
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
// All spans and callbacks are borrowed for one synchronous call.
typedef struct {
  const uint8_t* data;
  size_t len;
} XpPluginSpan;
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
  char name[24];
  char dir[64];
  uint8_t mask;
  uint8_t connect_mask;
} XpPluginSubscriber;
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
void xp_plugin_subscriptions(XpPluginSubscriber table[8], const XpPluginSubscriptionOps*);
bool xp_plugin_any(const XpPluginSubscriber table[8], uint32_t event);
uint8_t xp_plugin_mask(const XpPluginSubscriber table[8], XpPluginSpan name);
typedef struct {
  void* ctx;
  XpPluginSpan (*read)(void*, const XpPluginSubscriber*);
} XpPluginConnectOps;
bool xp_plugin_wants_connect(const XpPluginSubscriber table[8], const XpPluginConnectOps*);
typedef struct {
  void* ctx;
  bool (*open)(void*);
  void (*close)(void*);
  void (*remove)(void*);
  uint64_t (*size)(void*);
  size_t (*write)(void*);
  void (*truncate)(void*, uint64_t);
  void (*flush)(void*);
  void (*diagnostic)(void*, uint32_t);
} XpPluginAppendOps;
void xp_plugin_append(const XpPluginAppendOps*, size_t line_size);
typedef struct {
  void* ctx;
  bool (*deliver)(void*, XpPluginSpan);
  void (*remove)(void*);
  bool (*rewrite)(void*, size_t);
  void (*diagnostic)(void*, uint32_t, bool);
} XpPluginDrainOps;
void xp_plugin_drain_lines(XpPluginSpan, size_t* budget, const XpPluginDrainOps*);
typedef struct {
  void* ctx;
  bool (*exists)(void*);
  void (*remove_backup)(void*);
  bool (*backup)(void*);
  bool (*install)(void*);
  bool (*restore)(void*);
  void (*remove_temp)(void*);
  void (*committed)(void*);
  void (*restore_failed)(void*);
} XpPluginInstallOps;
int32_t xp_plugin_install_download(int32_t status, const XpPluginInstallOps*);
typedef struct {
  void* ctx;
  int32_t (*run)(void*);
  bool (*mint)(void*);
  void (*save)(void*);
  void (*accept_token)(void*);
  void (*toast)(void*);
} XpPluginDeliveryOps;
bool xp_plugin_deliver(bool password_grant, bool show_toast, const XpPluginDeliveryOps*);
typedef struct {
  void* ctx;
  bool (*allowed)(void*);
  int32_t (*download)(void*);
  int32_t (*response)(void*);
  void (*rejected)(void*);
} XpPluginRunOps;
int32_t xp_plugin_run(bool download, const XpPluginRunOps*);
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
typedef struct {
  void* ctx;
  bool (*allowed)(void*);
  bool (*read_parse)(void*);
  bool (*extract)(void*);
  void (*segment)(void*, XpPluginSpan, bool last);
  size_t (*serialize)(void*);
  bool (*open)(void*);
  size_t (*write)(void*);
  void (*flush)(void*);
  void (*close)(void*);
  void (*remove)(void*);
  void (*short_write)(void*, size_t written, size_t expected);
} XpPluginTokenOps;
bool xp_plugin_load_token(bool empty_file, const XpPluginTokenOps*);
bool xp_plugin_save_token(bool empty_file, XpPluginSpan path, const XpPluginTokenOps*);
typedef struct {
  void* ctx;
  void (*text)(void*, uint32_t field, XpPluginSpan value);
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
void xp_plugin_emit(const XpPluginSubscriber table[8], XpPluginSequence*, uint32_t event, const XpPluginVar*,
                    size_t count, const XpPluginEmitOps*);
typedef struct {
  void* ctx;
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
void xp_plugin_drain_all(const XpPluginSubscriber table[8], size_t budget, void*,
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
