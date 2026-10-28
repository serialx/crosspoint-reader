#pragma once
#include "Oracle.h"
struct ClippingStore {static bool moveBook(const char* a,const char* b){oracle::record("move-book %s %s",a,b);return oracle::fault!="move";}};
