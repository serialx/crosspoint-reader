#pragma once

#include <future>
#include <mutex>
#include <thread>

#include "activities/RenderLock.h"

// Both native and Rust guards use this same non-recursive host mutex contract.
inline const XpAppRuntime* syncRuntime = nullptr;
inline bool syncLoanActive = false;

inline std::mutex syncRenderMutex;
inline thread_local bool syncOwnsLock = false;
inline std::atomic<unsigned> syncLockTakes{0}, syncLockGives{0};
inline std::promise<void>* syncLockAttempt = nullptr;
inline void* syncTake() {
  assert(!syncOwnsLock && "same-task render-mutex reacquisition");
  if (syncLockAttempt) {
    auto* attempted = syncLockAttempt;
    syncLockAttempt = nullptr;
    attempted->set_value();
  }
  syncRenderMutex.lock();
  assert(clippingRenderDepth.fetch_add(1) == 0);
  syncOwnsLock = true;
  ++syncLockTakes;
  return reinterpret_cast<void*>(1);
}
inline void syncGive(void*) {
  assert(syncOwnsLock && clippingRenderDepth == 1);
  syncOwnsLock = false;
  --clippingRenderDepth;
  ++syncLockGives;
  syncRenderMutex.unlock();
}

inline RenderLock::RenderLock(Mode mode) {
  assert(mode == Mode::Blocking);
  syncRuntime->render_lock();
  isLocked = true;
}
inline RenderLock::RenderLock(Activity&) : RenderLock(Mode::Blocking) {}
inline RenderLock::~RenderLock() { unlock(); }
inline void RenderLock::unlock() {
  if (isLocked) {
    assert(!syncLoanActive);
    syncRuntime->render_unlock(reinterpret_cast<void*>(1));
    isLocked = false;
  }
}

class SyncFrameBufferLoan {
 public:
  SyncFrameBufferLoan(uint8_t* bytes, size_t length) {
    assert(clippingRenderDepth == 1 && !syncLoanActive);
    syncLoanActive = true;
    buildscratch::lend(bytes, length);
  }
  ~SyncFrameBufferLoan() {
    assert(clippingRenderDepth == 1 && syncLoanActive);
    buildscratch::reclaim();
    syncLoanActive = false;
  }
};
