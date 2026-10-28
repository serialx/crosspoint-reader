#pragma once
#include "HalStorage.h"
inline void clearBookCache(const char* p){oracle::record("clear-cache %s",p);}
inline bool removeBookFile(const char* p){oracle::record("remove-book %s",p);return Storage.remove(p);}
