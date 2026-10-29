#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Synchronous, serialized request loan. Callbacks retain no Rust spans and cannot
// reenter these entry points. Spans are length-bounded; filesystem path pointers are NUL-terminated.
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
  XP_WEB_AVAILABLE
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
  XP_WEB_WS_COMPLETE_NAME
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
  const char* (*memory_error)(void*);
  void (*diagnostic)(void*, uint32_t, XpWebText, XpWebText, size_t, size_t, size_t);
  // Returns a synchronous disconnect of this client; never reenters Rust.
  bool (*ws_send)(void*, uint8_t, XpWebText);
} XpWebTransfers;
// Upload status: start=0, write=1, end=2, aborted=3, response=4.
void xp_web_upload(void*, const XpWebTransfers*, XpWebUpload*, uint32_t, XpWebText, const uint8_t*, size_t);
// WS status: disconnected=0, connected=1, text=2, binary=3, abort-from-stop=4.
void xp_web_ws(void*, const XpWebTransfers*, XpWebWsUpload*, uint8_t, uint32_t, const uint8_t*, size_t);
#ifdef __cplusplus
}
#endif
