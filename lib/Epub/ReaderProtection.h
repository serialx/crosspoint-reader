#pragma once
#include <xp_app.h>
#include <xp_book.h>

namespace freeink::content {
// Stage values match book_owner/protection.rs.
enum class IndexStage : uint8_t { Construct, Scan, ProbeOpen, ProbeClose };
enum class FinishStage : uint8_t { InstallKey, Take, Discard };
enum class StreamStage : uint8_t { Prepare, Read };
// Stable, pointer-aligned storage: 24 words for an index/key platform, six for a
// stream path, and 3 (32-bit) / 8 (host) for a probe file. Always pair ProbeOpen/Close.
// ProbeOpen borrows a NUL-terminated path and returns whether the HAL file opened.
uint8_t readerProtectionIndex(void* storage, uint8_t stage, XpAppText path);
void* readerProtectionFinish(void* storage, uint8_t stage, const uint8_t* key, size_t length);
uint8_t readerProtectionStream(void* handle, void* storage, uint8_t stage, XpAppText path, XpBookSink sink,
                               void* context);
}  // namespace freeink::content
