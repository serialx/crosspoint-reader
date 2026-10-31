#pragma once

#include "RadioPort.h"

namespace bleturner::rust {

// ABI borrows Config until the next begin(). Callbacks and the profile outlive all
// tasks. Main/render/start entry points keep the ownership documented in the public API.
// Diagnostics are synchronous, retain no pointers, and must not reenter Rust.
struct Services {
  Heap (*heap)();
  bool (*release)();
  bool (*deliver)(uint8_t);
  bool (*yieldRadio)();
  bool (*transfer)();
  void (*restart)();
  void (*hold)(bool);
  void (*diagnostic)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
  void (*heapMap)();
  bool (*compiledIn)();
  bool (*begin)();
  bool (*end)(uint32_t);
  bool (*running)();
  bool (*stopping)();
  bool (*connected)();
  bool (*connecting)();
  port::ConnectionActivity (*activity)();
  bool (*scanning)();
  void (*poll)();
  bool (*popRaw)(port::RawEdge*);
  bool (*popKey)(port::KeyPress*);
  bool (*arm)(uint8_t, const char*);
  Peer (*linked)();
  void (*scan)(uint32_t);
  bool (*connect)(const char*);
  void (*disconnect)();
  void (*forget)(const char*);
  bool (*spawn)();
  uint32_t (*now)();
  void (*sleep)(uint32_t);
  uint32_t* (*memo)();
  const RemoteTable* (*defaultTable)(const char*);
  const Profile* profile;
};

// Payload: heap events use a=free, b=largest; Raw uses a=code, b=pressed,
// c=action; Key uses a=usage, b=mods, c=pressed, d=action. Others use a=count/ms/action.
enum class Diagnostic : uint32_t {
  SkippedTransfer = 1,
  SkippedHeap,
  BeginHeap,
  RolledBack,
  KeptHeap,
  ReconnectNotArmed,
  Raw,
  Hold,
  Key,
  Retry,
  Requested,
  Deferred,
  Idle,
  Restart,
  BuildStarved,
  BuildStopFailed
};
extern "C" {
void xp_ble_init(const Services*, Config*);
void xp_ble_start_task();
bool xp_ble_start_async();
bool xp_ble_suspend(uint32_t);
bool xp_ble_stop_idle();
bool xp_ble_restart_wanted();
void xp_ble_request_connect();
bool xp_ble_tick(const Scene*);
bool xp_ble_before_screen(uint32_t, bool);
bool xp_ble_stop_now(uint32_t);
void xp_ble_settle_start();
uint8_t xp_ble_before_build();
void xp_ble_after_paint();
bool xp_ble_holds_heap();
void xp_ble_status(Status*);
uint8_t xp_ble_why();
uint8_t xp_ble_note();
void xp_ble_acknowledge();
bool xp_ble_switch_on();
bool xp_ble_switch_off();
bool xp_ble_service();
bool xp_ble_pair(const char*, const char*);
bool xp_ble_forget(const char*);
bool xp_ble_poll_event(Event*);
void xp_ble_probe_table();
void xp_ble_reset();
}
static_assert(sizeof(Config) == 268 && offsetof(Config, remotes) == 56 && offsetof(Config, remoteCount) == 264);
static_assert(sizeof(RemoteTable) == 52 && offsetof(RemoteTable, bindings) == 20);
static_assert(sizeof(HoldWait) == 12 && offsetof(HoldWait, active) == 10);
static_assert(sizeof(Scene) == 16 && offsetof(Scene, visit) == 4);
static_assert(sizeof(port::RawEdge) == 12 && offsetof(port::RawEdge, atMs) == 8);
static_assert(sizeof(port::KeyPress) == 3 && sizeof(Status) == 9);
static_assert(sizeof(NoteInputs) == 16 && offsetof(NoteInputs, runningMs) == 8);
static_assert(sizeof(RadioInputs) == (sizeof(size_t) == 4 ? 28 : 40));

}  // namespace bleturner::rust
