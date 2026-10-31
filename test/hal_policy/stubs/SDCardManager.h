#pragma once
#include <Print.h>
#include <common/FsApiConstants.h>

#include <cstring>
#include <utility>
#include <vector>
inline bool fileOk = true, directory = false, detachOk = true, mountOk = true;
inline size_t fileLength = 40;
inline int readLength = -2;
inline int writeLength = -2;
class FsFile {
  bool live = false;

 public:
  FsFile() = default;
  explicit FsFile(bool open) : live(open) { puts("file create"); }
  FsFile(FsFile&& o) : live(std::exchange(o.live, false)) { puts("file move"); }
  FsFile& operator=(FsFile&& o) {
    close();
    live = std::exchange(o.live, false);
    puts("file assign");
    return *this;
  }
  ~FsFile() { close(); }
  bool close() {
    if (live) puts("file close");
    live = false;
    return true;
  }
  bool isOpen() const {
    puts("file isOpen");
    return live;
  }
  bool isDirectory() const {
    puts("file directory");
    return directory;
  }
  uint64_t fileSize() const {
    printf("file size %zu\n", fileLength);
    return fileLength;
  }
  size_t size() const { return fileSize(); }
  int read(void* out, size_t n) {
    printf("file read %zu\n", n);
    int got = readLength == -2 ? int(n) : readLength;
    if (got > 0) memset(out, 'x', size_t(got) < n ? size_t(got) : n);
    return got;
  }
  int read() { return -1; }
  bool getModifyDateTime(uint16_t* d, uint16_t* t) {
    *d = 1;
    *t = 2;
    return live;
  }
  FsFile openNextFile() { return FsFile(fileOk); }
  size_t write(const void*, size_t n) {
    printf("file write %zu\n", n);
    return writeLength == -2 ? n : writeLength;
  }
  void flush() {}
  void rewindDirectory() {}
  template <typename... T>
  size_t getName(T...) const {
    return 0;
  }
  template <typename... T>
  size_t seekSet(T...) const {
    return 0;
  }
  template <typename... T>
  size_t seekCur(T...) const {
    return 0;
  }
  template <typename... T>
  size_t truncate(T...) const {
    return 0;
  }
  template <typename... T>
  size_t available(T...) const {
    return 0;
  }
  template <typename... T>
  size_t position(T...) const {
    return 0;
  }
  size_t write(uint8_t) { return 1; }
  template <typename... T>
  size_t rename(T...) const {
    return 0;
  }
};
struct SDCardManager {
  static SDCardManager& getInstance() {
    static SDCardManager s;
    return s;
  }
  bool begin() {
    puts("sd begin");
    return mountOk;
  }
  bool ready() const { return true; }
  void shutdown() { puts("sd shutdown"); }
  int* detachFilesystemForRawAccess() {
    puts("sd detach");
    static int block = 0;
    return detachOk ? &block : nullptr;
  }
  FsFile open(const char* path, oflag_t mode) {
    printf("sd open %s %u\n", path, mode);
    return FsFile(fileOk);
  }
  bool openFileForRead(const char* tag, const char* path, FsFile& out) {
    printf("openFileForRead %s %s\n", tag, path);
    out = FsFile(fileOk);
    return fileOk;
  }
  bool openFileForWrite(const char* tag, const char* path, FsFile& out) {
    printf("openFileForWrite %s %s\n", tag, path);
    out = FsFile(fileOk);
    return fileOk;
  }
  std::vector<String> listFiles(const char*, int) { return {}; }
  String readFile(const char*) { return {}; }
  template <typename... T>
  size_t readFileToStream(T&&...) {
    return 0;
  }
  template <typename... T>
  size_t readFileToBuffer(T&&...) {
    return 0;
  }
  template <typename... T>
  size_t writeFile(T&&...) {
    return 0;
  }
  template <typename... T>
  size_t ensureDirectoryExists(T&&...) {
    return 0;
  }
  template <typename... T>
  size_t mkdir(T&&...) {
    return 0;
  }
  template <typename... T>
  size_t exists(T&&...) {
    return 0;
  }
  template <typename... T>
  size_t remove(T&&...) {
    return 0;
  }
  template <typename... T>
  size_t rename(T&&...) {
    return 0;
  }
  template <typename... T>
  size_t replaceFile(T&&...) {
    return 0;
  }
  template <typename... T>
  size_t rmdir(T&&...) {
    return 0;
  }
  template <typename... T>
  size_t removeDir(T&&...) {
    return 0;
  }
};
