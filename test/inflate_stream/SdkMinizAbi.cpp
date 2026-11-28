#define CONTENT_EXTERNAL_MINIZ 1
#include <cstddef>

#include "freeink-sdk/libs/book/ContentProtection/include/ContentMinizConfig.h"

extern const size_t SDK_MINIZ_LAYOUT[] = {sizeof(mz_stream),
                                          offsetof(mz_stream, next_in),
                                          offsetof(mz_stream, avail_in),
                                          offsetof(mz_stream, total_in),
                                          offsetof(mz_stream, next_out),
                                          offsetof(mz_stream, avail_out),
                                          offsetof(mz_stream, total_out),
                                          offsetof(mz_stream, msg),
                                          offsetof(mz_stream, state),
                                          offsetof(mz_stream, zalloc),
                                          offsetof(mz_stream, zfree),
                                          offsetof(mz_stream, opaque),
                                          offsetof(mz_stream, data_type),
                                          offsetof(mz_stream, adler),
                                          offsetof(mz_stream, reserved)};
