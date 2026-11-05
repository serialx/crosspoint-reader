#!/usr/bin/env python3
"""Compile the production reclaim decision against recorded memory-manager outcomes."""
from pathlib import Path
import sys
root, output = map(Path, sys.argv[1:])
source = (root / 'lib/Epub/ContentProtection.cpp').read_text()
start = source.index('void reclaimContentCaches()')
end = source.index('// ByteSource over', start)
output.write_text(r'''
#include <xp_ffi.h>
#include <cstdio>
#include <initializer_list>
static size_t largest, reclaimed, freeBytes;
constexpr int MALLOC_CAP_8BIT=0;
size_t heap_caps_get_largest_free_block(int) {return largest;}
struct {size_t getMaxAllocHeap()const{return largest;}size_t getFreeHeap()const{return freeBytes;}} ESP;
namespace freeink {struct MemoryManager {
 static MemoryManager& instance(){static MemoryManager value;return value;}
 void clearCaches(){puts("clear");largest=reclaimed;}
};}
#define LOG_DBG(tag, format, ...) printf("DBG " tag " " format "\n" __VA_OPT__(,) __VA_ARGS__)
''' + source[start:end] + r'''
int main(){
 for(size_t before:{size_t(0),size_t(65535),size_t(65536),size_t(65537)})
  for(size_t after:{size_t(0),size_t(65535),size_t(65536),size_t(65537)})
   for(bool allocationFailure:{false,true}) {
    largest=before;reclaimed=after;freeBytes=100000;
    printf("case %zu %zu %u\n",before,after,allocationFailure);
    reclaimContentCaches();
    printf("allocation %u\n",!allocationFailure);
    reclaimContentCaches();
    printf("largest %zu\n",largest);
   }
}
''')
