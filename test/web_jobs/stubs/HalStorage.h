#pragma once
#include "Arduino.h"
class HalFile {
 std::string path; size_t pos=0; bool opened=false;
 public:
 ~HalFile() { if(opened) close(); }
 explicit operator bool() const { return opened; }
 bool isOpen() const { return opened; }
 void close() { oracle::record("close %s",path.c_str()); opened=false; }
 size_t fileSize() const { return oracle::files[path].size(); }
 int read(uint8_t* p, size_t n) { size_t got=std::min(n,fileSize()-pos); if(oracle::fault=="read" && ++oracle::readCount==2) got=0; memcpy(p,oracle::files[path].data()+pos,got); pos+=got; oracle::record("read %zu %zu %u",n,got,oracle::hash(p,got)); return int(got); }
 size_t write(const uint8_t* p,size_t n) { size_t got=oracle::fault=="write" ? n/2 : n; oracle::files[path].insert(oracle::files[path].end(),p,p+got); oracle::record("write %zu %zu %u",n,got,oracle::hash(p,got)); return got; }
 bool open(const char* p,bool write) { path=p; pos=0; opened=oracle::fault!="open" && (write || oracle::files.count(p)); oracle::record("open %s %d %d",p,write,opened); if(opened&&write) oracle::files[p].clear(); return opened; }
};
struct StorageType {
 bool openFileForRead(const char*,const char* p,HalFile& f) { return f.open(p,false); }
 bool openFileForWrite(const char*,const char* p,HalFile& f) { return f.open(p,true); }
 bool remove(const char* p) { oracle::record("remove %s",p); return oracle::files.erase(p); }
 bool replaceFile(const char* a,const char* b) { oracle::record("replace %s %s",a,b); if(oracle::fault=="replace") return false; oracle::files[b]=oracle::files[a]; oracle::files.erase(a); return true; }
};
inline StorageType Storage;
