#pragma once
#include "xp_ui.h"
#include "xp_ui_settings.h"
#ifdef __cplusplus
extern "C" {
#endif

// Private KOReader screen family. Descriptors are aligned and live for each
// call. Services borrow text/resources; no callback retains pointers. Run
// callbacks may publish a separate model, but cannot reenter the same run.
typedef enum { XP_KO_SYNC = 0, XP_KO_AUTH = 1, XP_KO_SIGNUP = 2 } XpKoKind;
typedef enum {
  XP_KO_WIFI = 0,
  XP_KO_CONNECTING,
  XP_KO_SYNCING,
  XP_KO_RESULT,
  XP_KO_UPLOADING,
  XP_KO_UPLOADED,
  XP_KO_SYNCHRONIZED,
  XP_KO_MISSING,
  XP_KO_FAILED,
  XP_KO_NO_CREDENTIALS,
  XP_KO_AUTHENTICATING,
  XP_KO_AUTHENTICATED
} XpKoPhase;
typedef struct {
  uint32_t auto_return_at;
  uint8_t kind, phase, selected, smart, plugin, wifi_activated, touch_armed, pending_back;
} XpKoState;
typedef enum {
  XP_KO_NO_MESSAGE = 0,
  XP_KO_HASH,
  XP_KO_HASH_FAILED,
  XP_KO_CLIPPINGS,
  XP_KO_CLIPPINGS_FAILED,
  XP_KO_FETCH,
  XP_KO_UPLOAD,
  XP_KO_SAVE_FAILED,
  XP_KO_WIFI_FAILED,
  XP_KO_AUTH_BUSY,
  XP_KO_AUTH_OK,
  XP_KO_USER_EXISTS,
  XP_KO_SERVICE_ERROR
} XpKoMessage;
// Callback operations are resource access or visible effects. Return values
// are 0/1 for predicates, KOReaderSyncClient::Error for requests, and the
// existing ProgressComparison discriminant for COMPARE.
typedef enum {
  XP_KO_PUBLISH = 0,
  XP_KO_REPAINT,
  XP_KO_MARK_RETURN,
  XP_KO_RETURN_READER,
  XP_KO_HASH_PRIMARY,
  XP_KO_HASH_ALTERNATE,
  XP_KO_FETCH_PRIMARY,
  XP_KO_FETCH_ALTERNATE,
  XP_KO_ADOPT_ALTERNATE,
  XP_KO_LOAD_BOOK,
  XP_KO_MAP_PRIMARY,
  XP_KO_COMPARE,
  XP_KO_SAVE_PROGRESS,
  XP_KO_CLIP_SYNC,
  XP_KO_UPLOAD_BUILD,
  XP_KO_UPLOAD_METADATA,
  XP_KO_RELEASE_BOOK,
  XP_KO_UPLOAD_SEND,
  XP_KO_RADIO_STOP,
  XP_KO_AUTH_REQUEST,
  XP_KO_DIAGNOSTIC
} XpKoOperation;
typedef enum { XP_KO_DIAG_SMART_MISSING = 0, XP_KO_DIAG_SMART_DECISION, XP_KO_DIAG_SMART_UNKNOWN } XpKoDiagnostic;
typedef struct {
  void* context;
  uint32_t (*call)(void*, uint32_t operation, uint32_t a, uint32_t b);
} XpKoServices;
typedef struct {
  float local_percentage;
  uint8_t smart, clippings, rich, metadata, extended, signup, reserved[2];
} XpKoOptions;
// Actions returned by update: NONE/ACTIVATE/CANCEL use the common UI values;
// ACTIVATE value 0 applies remote, 1 uploads local, 2 starts a missing-record
// upload, and 3 finishes authentication. CANCEL returns to the reader.
void xp_ui_koreader_init(XpKoState*, uint8_t kind, uint8_t smart, uint8_t plugin);
void xp_ui_koreader_publish(XpKoState*, uint8_t phase, uint8_t selected);
void xp_ui_koreader_mark_return(XpKoState*, uint32_t now);
uint8_t xp_ui_koreader_busy(const XpKoState*);
XpUiInterest xp_ui_koreader_interest(const XpKoState*, uint32_t now);
XpUiEffect xp_ui_koreader_update(XpKoState*, const XpUiInput*);
// Synchronous orchestration over platform/book/service leaves. Does not borrow
// the published UI model, allocate, format strings or perform I/O itself.
void xp_ui_koreader_sync(const XpKoOptions*, const XpKoServices*);
void xp_ui_koreader_upload(const XpKoOptions*, const XpKoServices*);
void xp_ui_koreader_auth(uint8_t signup, const XpKoServices*);
void xp_ui_koreader_save(const XpKoServices*);

typedef struct {
  XpUiListStyle list;
  XpUiRect safe, header;
  int32_t small_font, fixed_font;
  int16_t header_bottom, hints_height, spacing, space_xs, space_sm, space_md, space_lg;
  int16_t body_height, small_height, fixed_height;
  uint8_t touch, has_device, reserved[2];
} XpKoLayout;
// Resources: 1 header, 2 hints, 3 status/error, 4 remote chapter,
// 5 remote value, 6 remote device, 7 local chapter, 8 local value,
// 9 apply icon, 10 upload icon; 64..83 are semantic translation resources.
// text() borrows the generated table by its existing StrId, not a resource id.
uint8_t xp_ui_koreader_view(const XpKoState*, const XpKoLayout*, const XpUiBuffer*, XpUiScene*);
const char* xp_ui_koreader_text(uint8_t language, uint32_t handle);
// Settings policy reuses the existing Rust settings list and U4 keyboard.
// Edit effects are private to settings; NONE leaves disabled rows unchanged.
typedef enum { XP_KO_EDIT_KEYBOARD = 4, XP_KO_EDIT_POPUP, XP_KO_EDIT_SAVE, XP_KO_EDIT_AUTH } XpKoEditEffect;
XpUiEffect xp_ui_koreader_edit(uint8_t row, uint8_t value, uint8_t has_credentials);
uint16_t xp_ui_koreader_label(uint8_t row);
uint8_t xp_ui_koreader_url_empty(const uint8_t*, uint32_t);
#ifdef __cplusplus
}
#endif
