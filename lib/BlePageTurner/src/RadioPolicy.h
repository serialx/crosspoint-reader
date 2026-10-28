#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "BlePageTurner.h"

// Rust policy entry points; the profile remains selected by the firmware build.
namespace bleturner {

// X3 numbers (the default profile below; the S3 has its own). The stack needs both to start; after it
// started the reader still needs one 32 KiB block (the caller-owned DictZip ring and the streaming Rust inflate
// window). Under kMinimumFreeBytes the heap is short in total; above it a refusal counts as a heap in
// pieces (shouldRestart).
inline constexpr size_t kMinimumFreeBytes = 65536;
inline constexpr size_t kMinimumLargestBlockBytes = 32768;
// The stack took 50,644 to 51,188 B in every X3 log (free before and after the start). A start with
// less than that plus the reader's block free can only roll back, and while it is up the render task
// runs out of heap beside it: X3, 07/10/2026, free 73,656 before the start, 22,676 after, then
// abort() in a page restore. It is refused before the stack comes up.
inline constexpr size_t kStackBytes = 49152;
inline constexpr size_t kStartFreeBytes = kStackBytes + kMinimumLargestBlockBytes;
// A radio nobody connected to for this long stops: left on, it keeps the CPU at full speed
// and a device lying still eats its battery.
inline constexpr uint32_t kIdleOffMs = 30u * 1000u;

// What a chip's stack costs and where the radio may start. X3 (C3, stack pools cut down): 49,152 B as
// logged in every X3 start, and the reader there needs a whole 32 KiB block (the inflate ring is a malloc
// in the same small heap). S3 with NimBLE's host in PSRAM (FREEINK_BLE_STACK_IN_PSRAM, set by
// scripts/s3_nimble_psram.h): the unit measured on 13/10/2026 went from free 124,752 / largest 81,908 at
// Home to 78,244 / 36,852, so the stack took 46,508 B of internal RAM and 45,056 B of the largest block.
struct Profile {
  size_t stackBytes;
  size_t startFreeBytes;    // stackBytes plus the reader's room
  size_t minimumLargest;    // the largest block needed before and after the stack
  size_t homeStartLargest;  // 0: the radio does not start at Home; else the largest block Home must have
};
inline constexpr size_t kS3StackBytes = 46508 + 4096;  // measured, plus a margin of one allocator page
inline constexpr size_t kS3StackLargestDrop = 45056;
// On the S3 malloc and new above 4,096 B go to PSRAM (prebuilt sdkconfig.h: CONFIG_SPIRAM_USE_MALLOC=1,
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096): the inflate ring, the layout buffers and the picture buffers never
// ask internal RAM for a block bigger than this. The heaviest page build on the host asks 16,560 B live in
// such requests and 4,096 B at most in one (HugeBookSection.S3InternalCensusOfTheHeaviestBuild), counting
// operator new only. Malloc and task stacks are not in that census, so the room kept contiguous after the stack
// is six of the largest request (24,576 B), wider than the four the census alone would justify.
inline constexpr size_t kS3InternalAllocMax = 4096;
inline constexpr size_t kS3MinimumLargest = 6 * kS3InternalAllocMax;
inline constexpr Profile kX3Profile{kStackBytes, kStartFreeBytes, kMinimumLargestBlockBytes, 0};
inline constexpr Profile kS3Profile{kS3StackBytes, kS3StackBytes + kS3MinimumLargest, kS3MinimumLargest,
                                    kS3MinimumLargest + kS3StackLargestDrop + 2048};
#ifdef FREEINK_BLE_STACK_IN_PSRAM
inline constexpr const Profile& activeProfile() { return kS3Profile; }
#else
inline constexpr const Profile& activeProfile() { return kX3Profile; }
#endif

struct RadioInputs;
struct NoteInputs;
extern "C" {
bool xp_ble_rearm(bool idleStopped, bool entered, bool requested, bool buildPainted);
uint8_t xp_ble_verdict(const RadioInputs*, const Profile*);
uint8_t xp_ble_next_note(uint8_t, const NoteInputs*);
bool xp_ble_should_restart(uint8_t, uint8_t, bool);
bool xp_ble_tracker_transition(uint8_t, uint8_t, uint8_t*, uint32_t*, bool*);
}

inline bool radioRearmWanted(bool idleStopped, bool entered, bool requested, bool buildPainted) {
  return xp_ble_rearm(idleStopped, entered, requested, buildPainted);
}

// Where the question is asked.
enum class Phase : uint8_t {
  Idle,         // may this book visit ask for the radio (asked once per visit, running or not)
  BeforeStart,  // on the start task, caches released, right before the stack comes up
  JustStarted,  // the stack came up: keep it, or roll it back
  Running,      // the radio is up: keep it?
};

struct RadioInputs {
  Phase phase;
  bool enabled;
  Where where;
  bool pageShown;
  bool bookIndexing;
  bool storageBusy;
  bool heldForBuild;  // beforeChapterBuild() stopped it and afterPaint() has not come yet
  bool idleStopped;   // stopped for idleness and not asked again since
  bool linked;        // a remote is connected
  uint32_t idleMs;    // since a remote was last connected (or the radio came up)
  Heap heap;
  bool connectionBusy = false;
  bool hasBond = false;  // a remote has been chosen (Config::peerAddr): a book or Home starts the radio only for one
};

// The ONE answer to "may the radio be on now". Everything that starts, keeps or stops the
// radio asks here.
inline Why radioVerdict(const RadioInputs& in, const Profile& profile = activeProfile()) {
  return static_cast<Why>(xp_ble_verdict(&in, &profile));
}

// --- The link note ---------------------------------------------------------------
// A radio up this long in one book visit with nothing linked has failed to find the remote.
inline constexpr uint32_t kLinkNoteFailMs = 20000;

struct NoteInputs {
  bool entered;  // this pass starts a book visit
  bool reading;  // a book is in front
  bool enabled;
  bool idleStopped;    // the radio is off for idleness: nothing will try to link
  bool linked;         // a remote is connected
  bool refused;        // this visit's radio start was refused (heap rules, rollback)
  uint32_t runningMs;  // how long the radio has been up without a break in this visit
  bool acknowledged;   // a page turn was applied
  bool hasBond;        // a remote has been chosen
};

// The ONE answer to "what does the status bar say about the remote now".
inline LinkNote nextLinkNote(const LinkNote note, const NoteInputs& in) {
  return static_cast<LinkNote>(xp_ble_next_note(static_cast<uint8_t>(note), &in));
}

// --- The heap restart ------------------------------------------------------------
// A heap in pieces keeps the radio off for good: leaving the book does not give the block
// back (X3, 27/09/2026: free=87308 largest=23540 in the book and on Home alike, 61428 after
// a restart). It shows before the start (enough bytes, no block the stack can take) or right
// after it (the stack came up and left no whole block for the reader: X3, 29/09/2026, the
// 5,000-chapter book, 7 of 7 starts rolled back at largest 26,612). A restart into the same
// book is the cure. It is allowed once until the radio comes up again, so a heap still in
// pieces after the restart cannot restart the device in a loop.
inline constexpr uint8_t kRefusalsBeforeRestart = 3;
inline constexpr uint32_t kRestartSpentMagic = 0x42485231u;

// The second decision, kept apart: restart to hand the radio a whole heap. `why` is the
// verdict that ended this start (before or right after the stack came up).
inline bool shouldRestart(const Why why, const uint8_t inPiecesInRow, const bool restartedSinceRadioUp) {
  return xp_ble_should_restart(static_cast<uint8_t>(why), inPiecesInRow, restartedSinceRadioUp);
}

// Kept in RTC memory, so it outlives ESP.restart. Power-on garbage is anything but the magic and
// reads as "no restart spent".
struct Memo {
  uint32_t magic;
};

// One per boot; only `memo` outlives the restart.
struct Tracker {
  Memo& memo;
  uint8_t fragmentedInRow = 0;
  // Asked by a refusal on the start task, read by the main loop, which restarts once a page is
  // shown.
  std::atomic<bool> wanted{false};

 private:
  enum class Event : uint8_t { Refused, Inconclusive, RadioUp, Restarting };
  bool transition(const Event event, const Why why = Why::Ok) {
    bool pending = wanted.load(std::memory_order_relaxed);
    const bool result = xp_ble_tracker_transition(static_cast<uint8_t>(event), static_cast<uint8_t>(why),
                                                  &fragmentedInRow, &memo.magic, &pending);
    wanted.store(pending, std::memory_order_release);
    return result;
  }

 public:
  // A start the heap ended: refused before the stack came up, or rolled back right after. A heap
  // in pieces counts toward the restart; a heap short in total breaks the streak (a restart
  // cannot give back bytes the book really uses) and drops a restart asked earlier. True when
  // this one asks for the restart.
  bool refused(const Why why) { return transition(Event::Refused, why); }
  // The start ended for a reason other than the heap (the stack failed, the start was cancelled,
  // the card was taken): no sign of pieces, the streak starts again.
  void inconclusive() { transition(Event::Inconclusive); }
  void radioUp() { transition(Event::RadioUp); }
  void restarting() { transition(Event::Restarting); }
};

}  // namespace bleturner
