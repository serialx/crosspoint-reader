#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <new>
static size_t attempts=0,live=0;
static bool fail() {
  const char* setting=std::getenv("ASSET_FAIL_ALLOC");
  const size_t index=attempts++;
  return setting && index==std::strtoull(setting,nullptr,10);
}
void* operator new(size_t size,const std::nothrow_t&) noexcept {return fail() ? nullptr : ::operator new(size);}
void* operator new[](size_t size,const std::nothrow_t&) noexcept {return fail() ? nullptr : ::operator new[](size);}
extern "C" void* heap_caps_malloc(size_t size,uint32_t) {if(fail()) return nullptr;void* p=std::malloc(size);if(p) ++live;return p;}
extern "C" void* heap_caps_aligned_alloc(size_t align,size_t size,uint32_t) {if(fail()) return nullptr;void* p=nullptr;if(posix_memalign(&p,align,size)) return nullptr;++live;return p;}
extern "C" void* heap_caps_realloc(void* p,size_t size,uint32_t) {if(fail()) return nullptr;const bool had=p;void* r=std::realloc(p,size);if(r && !had) ++live;return r;}
extern "C" void heap_caps_free(void* p) {if(p) --live;std::free(p);}
extern "C" void xp_chapter_release_block(void*) {std::abort();}
struct Report {~Report(){std::fprintf(stderr,"ALLOC %zu %zu\n",attempts,live);}};
static Report report;
