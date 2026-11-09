#pragma once

#include <cstddef>
#include <cstdint>

namespace loglite {
// X3 linked fatal bounds after the SP sample: drain 4080 B, wait 3536 B.
// Reaudit all post-sample continuations when changing the logger or SDK.
constexpr size_t DrainReserve = 4096;
// Preserve the render floor and loop gate; the extra 16 bytes keep both strict.
constexpr size_t RenderReserve = 1536 + 16;
constexpr size_t LoopGate = 8192;
constexpr bool emissionFits(uintptr_t base, uintptr_t sp, size_t loopStackBytes = 0) {
  const size_t loopReserve = loopStackBytes > LoopGate ? loopStackBytes - LoopGate + 16 : 16;
  const size_t reserve = loopReserve > RenderReserve ? loopReserve : RenderReserve;
  return sp >= base && sp - base >= DrainReserve + reserve;
}
}  // namespace loglite
