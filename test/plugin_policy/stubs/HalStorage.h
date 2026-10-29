#pragma once
#include "WString.h"
constexpr int O_WRONLY=1, O_CREAT=2, O_APPEND=4;
class HalFile {
  std::string path;
  bool opened = false;
  size_t child = 0;
 public:
  HalFile() = default;
  HalFile(HalFile&& f) : path(std::move(f.path)), opened(f.opened), child(f.child) { f.opened=false; }
  HalFile& operator=(HalFile&& f) {
    if(opened) close();
    path=std::move(f.path); opened=f.opened; child=f.child; f.opened=false; return *this;
  }
  ~HalFile() { if(opened) close(); }
  explicit operator bool() const { return opened; }
  bool isOpen() const { return opened; }
  bool isDirectory() const { return oracle::dirs.count(path); }
  size_t fileSize() const { return oracle::files[path].size(); }
  uint64_t fileSize64() const { return fileSize(); }
  void close() { oracle::record("close %s",path.c_str()); opened=false; }
  void flush() { oracle::record("flush %s",path.c_str()); }
  bool truncate(uint64_t n) { oracle::record("truncate %s %llu",path.c_str(),(unsigned long long)n); if(oracle::fault=="truncate") return false; oracle::files[path].resize(n); return true; }
  size_t write(const void* p, size_t n) {
    size_t got=oracle::fault=="write" ? n/2 : n;
    const auto* b=static_cast<const uint8_t*>(p);
    oracle::files[path].insert(oracle::files[path].end(),b,b+got);
    oracle::record("write %s %zu %zu %u",path.c_str(),n,got,oracle::hash(b,got)); return got;
  }
  size_t getName(char* out, size_t n) { auto name=path.substr(path.rfind('/')+1); snprintf(out,n,"%s",name.c_str()); return strlen(out); }
  bool open(const char* p, int mode=0) {
    path=p; child=0;
    opened=oracle::fault!="open" && (mode || oracle::files.count(p) || oracle::dirs.count(p));
    oracle::record("open %s %d %d",p,mode,opened);
    if(opened && mode && !(mode&O_APPEND)) oracle::files[p].clear();
    return opened;
  }
  HalFile openNextFile() {
    std::set<std::string> children;
    const auto prefix=path+"/";
    for(const auto& p:oracle::dirs) if(p.starts_with(prefix) && p.find('/',prefix.size())==std::string::npos) children.insert(p);
    for(const auto& [p,b]:oracle::files) if(p.starts_with(prefix) && p.find('/',prefix.size())==std::string::npos) children.insert(p);
    HalFile out;
    if(child<children.size()) { auto i=children.begin(); std::advance(i,child++); out.open(i->c_str()); }
    return out;
  }
};
struct StorageType {
  bool exists(const char* p) { return oracle::files.count(p)||oracle::dirs.count(p); }
  HalFile open(const char* p, int mode=0) { HalFile f; f.open(p,mode); return f; }
  bool openFileForWrite(const char*, const std::string& p, HalFile& f) { return f.open(p.c_str(),O_WRONLY|O_CREAT); }
  bool remove(const char* p) { oracle::record("remove %s",p); return oracle::fault!="remove" && oracle::files.erase(p); }
  bool rename(const char* p, const char* q) {
    oracle::record("rename %s %s",p,q);
    if((oracle::fault=="backup" && std::string(q).ends_with(".bak")) || (oracle::fault=="swap" && std::string(p).ends_with(".part")) || oracle::fault=="rename" || exists(q) || !exists(p)) return false;
    oracle::files[q]=std::move(oracle::files[p]); oracle::files.erase(p); return true;
  }
  bool readFileToString(const char* origin, const std::string& p, size_t cap, std::string& out) {
    out.clear(); oracle::record("read-file %s %s %zu",origin,p.c_str(),cap);
    if(oracle::fault=="read" || oracle::fault=="read-oom" || !exists(p.c_str())) return false;
    auto& b=oracle::files[p]; if(b.empty() || b.size()>cap) return false;
    out.assign(b.begin(),b.end()); return true;
  }
  bool writeFile(const char* p,const String& s) {
    oracle::record("rewrite %s %zu %u",p,s.size(),oracle::hash(reinterpret_cast<const uint8_t*>(s.data()),s.size()));
    if(oracle::fault=="rewrite") return false;
    oracle::files[p]=std::vector<uint8_t>(s.begin(),s.end()); return true;
  }
};
inline StorageType Storage;
