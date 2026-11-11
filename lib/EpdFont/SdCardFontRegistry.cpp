#include "SdCardFontRegistry.h"

#if CROSSPOINT_VECTOR_FONTS
#include <HalStorage.h>

unsigned long SdCardFontRegistry::halFileRead(void* ctx, const unsigned long offset, unsigned char* buffer,
                                              const unsigned long count) {
  auto* f = static_cast<HalFile*>(ctx);
  if (f == nullptr || !*f) return 0;
  if (!f->seek(static_cast<size_t>(offset))) return 0;
  if (count == 0) return 0;
  const int n = f->read(buffer, count);
  return n < 0 ? 0 : static_cast<unsigned long>(n);
}

#endif
