#!/usr/bin/env python3
"""Extract the production allocation adapter for isolated recovery fault injection."""
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
source = (root / 'lib/Epub/Epub/Paragraph.cpp').read_text()
start = source.index('namespace {')
end = source.index('\nnamespace {', start + 1)
source = source[start:end]
chapter = (root / 'lib/Epub/Epub/parsers/ChapterPageServices.cpp').read_text()
callback = chapter.split('const XpChapterSink ChapterPageAssembler::sink_ = {', 1)[1].split(',', 1)[0].strip()
output.write_text(r'''
#include <xp_ffi.h>
#include <Memory.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static unsigned calls, failures;
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  printf("allocate %zu\n", size);
  return ++calls <= failures ? nullptr : std::malloc(size);
}
void operator delete[](void* data) noexcept { printf("release\n"); std::free(data); }
namespace freeink {
struct MemoryManager {
 static MemoryManager& instance() { static MemoryManager manager; return manager; }
 void ensureFree(size_t bytes) { printf("reclaim %zu\n", bytes); }
};
}
#define LOG_DBG(tag, format, ...) printf("DBG " tag " " format "\n" __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERR(tag, format, ...) printf("ERR " tag " " format "\n" __VA_OPT__(,) __VA_ARGS__)
''' + source + '\nconst XpParaAllocator* (*chapterAllocator)() = ' + callback + r''';
int main() {
 for (bool chapter : {false, true}) {
  for (unsigned fail : {0u, 1u, 2u}) {
   for (unsigned word : {0u, 1u}) {
    for (size_t bytes : {size_t(0), size_t(1), size_t(4096), size_t(65535)}) {
     calls = 0; failures = fail;
     printf("case %u %u %u %zu\n", chapter, fail, word, bytes);
     const auto* allocator = chapter ? chapterAllocator() : paragraphAllocator();
     void* data = allocator->allocate(bytes, word);
     printf("result %u %u\n", data != nullptr, calls);
     if (data) {
      const auto* p = static_cast<const uint8_t*>(data);
      for (size_t i = 0; i < bytes; ++i) if (p[i] != 0) return 1;
      allocator->release(data);
     }
    }
   }
  }
 }
}
''')
