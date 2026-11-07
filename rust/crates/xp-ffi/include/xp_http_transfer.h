#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Proposal only: synchronous loans expire before the enclosing transfer returns.
typedef struct {
  const uint8_t* data;
  size_t length;
} XpHttpBytes;

typedef struct {
  const void* native_string;
  XpHttpBytes bytes;
} XpHttpNativeText;

// Owns the sole std::string; the descriptor itself owns no second byte buffer.
typedef struct {
  void* native_string;
  uint8_t* data;
  size_t length;
  size_t capacity;
} XpHttpMutableText;

typedef struct {
  uintptr_t words[6];
} XpHttpTextStorageRetireP19;

typedef struct {
  void (*initialize_retire_p19)(XpHttpTextStorageRetireP19*, size_t, XpHttpMutableText*);
  void (*resize_retire_p19)(XpHttpMutableText*, size_t);
  void (*release_retire_p19)(XpHttpMutableText*);
} XpHttpTextLeaves;

typedef struct {
  int32_t status;
  size_t length;
  bool known_length;
  bool complete;
  bool aborted;
} XpHttpResponseFacts;

typedef struct {
  void* context;
  bool (*chunk)(void*, XpHttpBytes, const XpHttpResponseFacts*);
  bool (*should_abort)(void*);
} XpHttpAttemptCallbacks;

typedef struct {
  uint32_t timeout_ms;
  bool insecure;
  bool basic_auth;
  XpHttpBytes user_agent;
  XpHttpNativeText username;
  XpHttpNativeText password;
} XpHttpSdkConfiguration;

typedef struct {
  // acquire constructs even when begin fails; release must follow either result.
  bool (*acquire)(void* storage, XpHttpNativeText url);
  void (*release)(void* storage);
  // Calls timeout, insecure, user-agent, optional basic-auth in that order.
  void (*configure)(void*, const XpHttpSdkConfiguration*);
  void (*add_header)(void*, XpHttpNativeText name, XpHttpNativeText value);
  int32_t (*send)(void*, const char* method, XpHttpBytes body, const XpHttpAttemptCallbacks*, XpHttpResponseFacts*);
  // Copy-constructs the SDK first-match getHeader result in uninitialized storage.
  void (*response_header)(void*, const char* name, XpHttpTextStorageRetireP19*, XpHttpMutableText*);
} XpHttpSdkLeaves;

typedef struct {
  bool (*open_write)(void* file, XpHttpNativeText path);
  size_t (*write)(void* file, XpHttpBytes bytes);
  void (*close)(void* file, bool release);
  bool (*remove)(XpHttpNativeText path);
  bool (*replace)(XpHttpNativeText from, XpHttpNativeText to);
} XpHttpFileLeaves;

typedef struct {
  int32_t (*get_ps)(int32_t* mode);
  int32_t (*set_ps)(int32_t mode);
} XpHttpRadioLeaves;

typedef enum {
  XP_HTTP_LOG_FETCHING,
  XP_HTTP_LOG_DOWNLOADING,
  XP_HTTP_LOG_POST_DOWNLOAD,
  XP_HTTP_LOG_GET_HEAP,
  XP_HTTP_LOG_GET,
  XP_HTTP_LOG_POST,
  XP_HTTP_LOG_BAD_URL,
  XP_HTTP_LOG_UNAUTHORIZED,
  XP_HTTP_LOG_FAILED,
  XP_HTTP_LOG_INCOMPLETE,
  XP_HTTP_LOG_OPEN_FAILED,
  XP_HTTP_LOG_POST_FAILED,
  XP_HTTP_LOG_EMPTY,
  XP_HTTP_LOG_POST_INCOMPLETE,
  XP_HTTP_LOG_REPLACE_FAILED,
  XP_HTTP_LOG_DOWNLOADED,
  XP_HTTP_LOG_GUARD_ENTER_FAILED,
  XP_HTTP_LOG_GUARD_RESTORE_FAILED
} XpHttpDiagnostic;

typedef struct {
  // Existing format strings and physical heap facts; no request decisions.
  void (*emit_retire_z1)(XpHttpDiagnostic, XpHttpBytes url, XpHttpBytes destination, size_t a, size_t b);
} XpHttpDiagnosticsRetireZ1;

typedef struct {
  const XpHttpSdkLeaves* sdk;
  const XpHttpFileLeaves* file;
  const XpHttpRadioLeaves* radio;
  const XpHttpTextLeaves* text;
  const XpHttpDiagnosticsRetireZ1* diagnostics;
} XpHttpLeaves;

typedef struct {
  void* context;
  size_t header_count;
  void (*header_at_retire_n5_n6_c2_c4_b2)(void*, size_t, XpHttpNativeText* name, XpHttpNativeText* value);
  bool (*sink_retire_n5_n6_c2_c4_b2)(void*, XpHttpBytes);
  void (*progress_retire_n5_n6_c2_c4_b2)(void*, size_t downloaded, size_t total);
} XpHttpConsumerRetireN5N6C2C4B2;

typedef enum {
  XP_HTTP_TRANSFER_FETCH,
  XP_HTTP_TRANSFER_CATALOG,
  XP_HTTP_TRANSFER_FORM,
  XP_HTTP_TRANSFER_DOWNLOAD,
  XP_HTTP_TRANSFER_POST_FILE
} XpHttpTransferKind;

// The only projected request; every text/body/callback borrows its native caller.
typedef struct {
  XpHttpTransferKind kind;
  XpHttpNativeText url;
  XpHttpNativeText username;
  XpHttpNativeText password;
  XpHttpNativeText bearer;
  XpHttpNativeText accept;
  XpHttpNativeText accept_language;
  XpHttpNativeText body;
  XpHttpNativeText content_type;
  XpHttpNativeText destination;
  XpHttpMutableText* form_response;
  const XpHttpConsumerRetireN5N6C2C4B2* consumer;
  const bool* cancel_flag;
  int32_t* status_output;
  int32_t* last_status;
  bool* last_redirected;
  bool capture_error_body;
  bool downgrade_redirects;
  // Exclusive call-local placement storage; no new native owner allocation.
  void* client_storage;
  void* file_storage;
} XpHttpRequestRetireN5N6C2C4B2;

typedef struct {
  int32_t previous_mode;
} XpHttpGuardToken;

uint32_t xp_http_transfer_retire_n5_n6_c2_c4_b2(const XpHttpRequestRetireN5N6C2C4B2*, const XpHttpLeaves*);
XpHttpGuardToken xp_wifi_guard_enter_retire_n7_s1_s2_w3b(const XpHttpRadioLeaves*, const XpHttpDiagnosticsRetireZ1*);
void xp_wifi_guard_exit_retire_n7_s1_s2_w3b(XpHttpGuardToken, const XpHttpRadioLeaves*,
                                            const XpHttpDiagnosticsRetireZ1*);
void xp_url_ensure_protocol_retire_c2(XpHttpNativeText, XpHttpMutableText*, const XpHttpTextLeaves*);
void xp_url_extract_host_retire_c2(XpHttpNativeText, XpHttpMutableText*, const XpHttpTextLeaves*);
void xp_url_encode_unsafe_retire_c2(XpHttpNativeText, XpHttpMutableText*, const XpHttpTextLeaves*);
void xp_url_build_retire_c2(XpHttpNativeText base, XpHttpNativeText path, XpHttpMutableText*, const XpHttpTextLeaves*);
void xp_http_header_parse_retire_c1_s2(XpHttpBytes line, XpHttpMutableText* name, XpHttpMutableText* value,
                                       const XpHttpTextLeaves*);
void xp_http_header_format_retire_c1_s2(XpHttpNativeText name, XpHttpNativeText value, bool masked, XpHttpMutableText*,
                                        const XpHttpTextLeaves*);

#ifdef __cplusplus
}
#endif
