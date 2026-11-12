#pragma once
#include <xp_app.h>
#include <xp_book.h>

namespace freeink::content {
enum class IndexStage : uint8_t { Construct, Scan };
enum class FinishStage : uint8_t { InstallKey, Take, Discard };
enum class StreamStage : uint8_t { Prepare, Read };
// Caller-owned storage is 24 pointer words for an index and six for a stream path.
uint8_t readerProtectionIndex(void* storage, uint8_t stage, XpAppText path);
void* readerProtectionFinish(void* storage, uint8_t stage, const uint8_t* key, size_t length);
uint8_t readerProtectionStream(void* handle, void* storage, uint8_t stage, XpAppText path, XpBookSink sink,
                               void* context);
}  // namespace freeink::content
