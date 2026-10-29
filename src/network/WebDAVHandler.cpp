#include "WebDAVHandler.h"

#include <Logging.h>

#include "ClippingStore.h"
#include "util/BookCacheUtils.h"
#include "util/TaskWatchdog.h"

namespace {
uint32_t methodId(HTTPMethod method) {
  switch (method) {
    case HTTP_OPTIONS:
      return 0;
    case HTTP_PROPFIND:
      return 1;
    case HTTP_GET:
      return 2;
    case HTTP_HEAD:
      return 3;
    case HTTP_PUT:
      return 4;
    case HTTP_DELETE:
      return 5;
    case HTTP_MKCOL:
      return 6;
    case HTTP_MOVE:
      return 7;
    case HTTP_COPY:
      return 8;
    case HTTP_LOCK:
      return 9;
    case HTTP_UNLOCK:
      return 10;
    default:
      return 11;
  }
}
std::string requestPath(const String& uri, bool destination) {
  String input = uri;
  if (destination) {
    if (input.isEmpty()) return {};
    const size_t offset = xp_dav_destination_offset(reinterpret_cast<const uint8_t*>(input.c_str()), input.length());
    input = offset == input.length() ? "/" : input.substring(offset);
  }
  const String decoded = WebServer::urlDecode(input);
  // Preserve the old C-string boundary before path normalization.
  const size_t len = strlen(decoded.c_str());
  std::string result(len + 2, '\0');
  result.resize(xp_dav_normalize(reinterpret_cast<const uint8_t*>(decoded.c_str()), len,
                                 reinterpret_cast<uint8_t*>(result.data())));
  return result;
}
struct Context {
  WebServer& server;
  char* path;
  size_t pathLength;
  std::string destination;
  std::string temp;
  HalFile root;
  HalFile entry;
  HalFile& upload;
  String xml;
};
size_t operation(void* opaque, uint32_t op, size_t a, size_t b, const uint8_t* bytes) {
  auto& c = *static_cast<Context*>(opaque);
  const char* path = reinterpret_cast<const char*>(bytes);
  HalFile& file = a == 2 ? c.upload : (a == 1 ? c.entry : c.root);
  switch (op) {
    case XP_DAV_EXISTS:
      return Storage.exists(path);
    case XP_DAV_OPEN:
      c.root = Storage.open(path);
      return static_cast<bool>(c.root);
    case XP_DAV_CLOSE:
      if (file) file.close();
      return 0;
    case XP_DAV_IS_OPEN:
      return static_cast<bool>(file);
    case XP_DAV_DIRECTORY:
      return file.isDirectory();
    case XP_DAV_SIZE:
      return file.size();
    case XP_DAV_NEXT:
      c.entry = c.root.openNextFile();
      return static_cast<bool>(c.entry);
    case XP_DAV_NAME:
      file.getName(reinterpret_cast<char*>(const_cast<uint8_t*>(bytes)), b);
      return 0;
    case XP_DAV_READ:
      return static_cast<size_t>(file.read(const_cast<uint8_t*>(bytes), b));
    case XP_DAV_WRITE:
      return file.write(bytes, b);
    case XP_DAV_AVAILABLE:
      return file.available();
    case XP_DAV_REMOVE:
      return Storage.remove(path);
    case XP_DAV_RMDIR:
      return Storage.rmdir(path);
    case XP_DAV_MKDIR:
      return Storage.mkdir(path);
    case XP_DAV_RENAME:
      return c.root.rename(path);
    case XP_DAV_OPEN_WRITE:
      return Storage.openFileForWrite("DAV", path, file);
    case XP_DAV_REMOVE_BOOK:
      return removeBookFile(path);
    case XP_DAV_CLEAR_CACHE:
      clearBookCache(path);
      return 0;
    case XP_DAV_MOVE_BOOK:
      return ClippingStore::moveBook(c.path, c.destination.c_str());
    case XP_DAV_STREAM: {
      NetworkClient client = c.server.client();
      return Storage.readFileToStream(path, client);
    }
    case XP_DAV_WATCHDOG:
      resetTaskWatchdogIfSubscribed();
      return 0;
    case XP_DAV_YIELD:
      yield();
      return 0;
    case XP_DAV_LENGTH:
      c.server.setContentLength(a);
      return 0;
  }
  return 0;
}
void response(void* opaque, uint32_t status, const uint8_t* mime, const uint8_t* body) {
  static_cast<Context*>(opaque)->server.send(status, reinterpret_cast<const char*>(mime),
                                             reinterpret_cast<const char*>(body));
}
void content(void* opaque, uint32_t op, const uint8_t* bytes, size_t len) {
  auto& c = *static_cast<Context*>(opaque);
  switch (op) {
    case 0:
      c.xml = "";
      break;
    case 1:
      c.xml.concat(reinterpret_cast<const char*>(bytes), len);
      break;
    case 2:
      c.server.sendContent(c.xml);
      break;
    case 3:
      c.server.sendContent(reinterpret_cast<const char*>(bytes), len);
      break;
  }
}
void header(void* opaque, const uint8_t* key, const uint8_t* value) {
  static_cast<Context*>(opaque)->server.sendHeader(reinterpret_cast<const char*>(key),
                                                   reinterpret_cast<const char*>(value));
}
void diagnostic(void* opaque, uint32_t event, size_t a, size_t b) {
  auto& c = *static_cast<Context*>(opaque);
  const char* path = c.path;
  switch (event) {
    case 0:
      LOG_DBG("DAV", "OPTIONS %s", c.server.uri().c_str());
      break;
    case 1:
      LOG_DBG("DAV", "PROPFIND %s depth=%d", path, (int)a);
      break;
    case 2:
      LOG_DBG("DAV", "GET %s", path);
      break;
    case 3:
      LOG_DBG("DAV", "HEAD %s", path);
      break;
    case 4:
      LOG_ERR("DAV", "GET %s: failed to stream file", path);
      break;
    case 5:
      LOG_DBG("DAV", "PUT %s", path);
      break;
    case 6:
      LOG_DBG("DAV", "PUT complete: %s", path);
      break;
    case 7:
      LOG_DBG("DAV", "DELETE %s", path);
      break;
    case 8:
      LOG_DBG("DAV", "MKCOL %s", path);
      break;
    case 9:
      LOG_DBG("DAV", "Created directory: %s", path);
      break;
    case 10:
      LOG_DBG("DAV", "MOVE %s -> %s (overwrite=%d)", path, c.destination.c_str(), (int)a);
      break;
    case 11:
      LOG_DBG("DAV", "COPY %s -> %s (overwrite=%d)", path, c.destination.c_str(), (int)a);
      break;
    case 12:
      LOG_DBG("DAV", "LOCK %s (dummy)", path);
      break;
    case 13:
      LOG_DBG("DAV", "UNLOCK %s (dummy)", c.server.uri().c_str());
      break;
    case 14:
      LOG_DBG("DAV", "PUT START: %s", path);
      break;
    case 15:
      LOG_DBG("DAV", "PUT END: %u bytes, ok=%d", (unsigned)a, (int)b);
      break;
  }
}
XpDavServices services(Context& c) {
  const String overwrite = c.server.header("Overwrite");
  const String depth = c.server.header("Depth");
  const uint32_t flags = xp_dav_headers(reinterpret_cast<const uint8_t*>(depth.c_str()), depth.length(),
                                        reinterpret_cast<const uint8_t*>(overwrite.c_str()), overwrite.length());
  return {&c,
          operation,
          response,
          content,
          header,
          diagnostic,
          reinterpret_cast<uint8_t*>(c.path),
          c.pathLength,
          reinterpret_cast<uint8_t*>(c.destination.data()),
          c.destination.size(),
          reinterpret_cast<const uint8_t*>(c.temp.c_str()),
          flags & 1u,
          (flags & 2u) != 0,
          c.server.clientContentLength() > 0};
}
}  // namespace
bool WebDAVHandler::canHandle(WebServer&, HTTPMethod method, const String&) { return methodId(method) < 11; }
bool WebDAVHandler::canRaw(WebServer& server, const String&) { return server.method() == HTTP_PUT; }
bool WebDAVHandler::handle(WebServer& server, HTTPMethod method, const String&) {
  auto path = requestPath(server.uri(), false);
  const uint32_t id = methodId(method);
  Context c{server,
            path.data(),
            path.size(),
            id == 7 || id == 8 ? requestPath(server.header("Destination"), true) : std::string{},
            id == 4 ? path + ".davtmp" : std::string{},
            {},
            {},
            putFile,
            {}};
  const auto api = services(c);
  return xp_dav_handle(&api, &putState, id);
}
void WebDAVHandler::raw(WebServer& server, const String&, HTTPRaw& raw) {
  uint32_t event;
  switch (raw.status) {
    case RAW_START:
      event = 0;
      putPath = requestPath(server.uri(), false).c_str();
      break;
    case RAW_WRITE:
      event = 1;
      break;
    case RAW_END:
      event = 2;
      break;
    case RAW_ABORTED:
      event = 3;
      break;
    default:
      return;
  }
  Context c{server, putPath.begin(), putPath.length(), {}, {}, {}, {}, putFile, {}};
  if (event != 1) {
    c.temp = putPath.c_str();
    c.temp += ".davtmp";
  }
  const auto api = services(c);
  xp_dav_raw(&api, &putState, event, raw.buf, raw.currentSize, raw.totalSize);
}
