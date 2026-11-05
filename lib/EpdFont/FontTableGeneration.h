#pragma once
#include <atomic>
#include <cstdint>

// Font/cache operations and measurement use the existing serialized owner.
// Invalidates every descriptor before a moved or freed table can be borrowed.
inline std::atomic<uint32_t> fontTableGeneration{1};
static_assert(sizeof(fontTableGeneration) == sizeof(uint32_t));
static_assert(alignof(decltype(fontTableGeneration)) == alignof(uint32_t));
