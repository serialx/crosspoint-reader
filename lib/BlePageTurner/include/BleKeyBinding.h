#pragma once

#include <BleRemoteProfiles.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Binding EACH button of a BLE remote. A button is known by the fingerprint of its
// RAW HID frame (report id, byte index, value; see freeink::RawButtonEvent). The
// decoded usage is too lossy for that: the decoder drops the second byte of the
// three-button remote and cuts 16-bit usages to 8 bits. Each remote (by BLE address) has one table of at
// most 8 slots, saved in settings.json and applied again on reconnect.
//
// Binding policy is shared by the runtime, settings screen and tests. Device-specific
// report profiles come from the SDK; reader actions and saved overrides stay here.
namespace bleturner {

// One wait for a button lasts at most 15 seconds, as the screen tells the user.
inline constexpr uint32_t kWaitMs = 15000;
// Held this long or more is a "hold". A host whose own buttons skip chapters on a
// hold should use the same number and pin it with a static_assert.
inline constexpr uint32_t kHoldMs = 700;

// Stored by value in settings.json (bits 28-31 of a slot): new actions go last, and the
// numbers never change (a test pins them). The four moves are the remote's own;
// ReaderMenu and SaveQuote are handed to the host, which runs its own action of that name.
enum class Action : uint8_t {
  None,
  NextPage,
  PrevPage,
  NextChapter,
  PrevChapter,
  ReaderMenu,
  SaveQuote,
  Confirm,
  Back
};

// One slot: bits 0-23 the button code (value | byte index << 8 | report id << 16),
// bit 24 "hold", bit 25 "any report id" (built-in defaults only, since that remote's
// report id has not been measured), bits 28-31 the action.
using Binding = uint32_t;
inline constexpr uint32_t kCodeMask = 0x00FFFFFFu;
inline constexpr uint32_t kHoldBit = 1u << 24;
inline constexpr uint32_t kAnyReportBit = 1u << 25;
inline constexpr uint8_t kActionShift = 28;

inline constexpr uint8_t kMaxBindings = 8;  // 4 buttons x tap/hold
inline constexpr uint8_t kMaxRemotes = 4;   // = BleKeyboardHost::kMaxBonds

struct RemoteTable {
  char addr[18];
  uint8_t count;
  Binding bindings[kMaxBindings];
};

// C ABI borrows bounded tables and strings for one call; no allocation or pointer retention.
struct HoldWait;
struct Router;
extern "C" {
bool xp_ble_valid(uint32_t);
bool xp_ble_matches(uint32_t, uint32_t, bool);
uint8_t xp_ble_lookup(const RemoteTable*, uint32_t, bool);
bool xp_ble_find(const RemoteTable*, uint8_t, Binding*);
bool xp_ble_clear(RemoteTable*, uint8_t);
bool xp_ble_learn(RemoteTable*, uint8_t, uint32_t, bool);
uint8_t xp_ble_raw_edge(const RemoteTable*, uint32_t, bool, uint32_t, uint8_t, HoldWait*);
uint8_t xp_ble_poll_hold(HoldWait*, uint32_t);
const RemoteTable* xp_ble_table_for(const RemoteTable*, uint8_t, const char*, const char*,
                                    const RemoteTable* (*)(const char*));
RemoteTable* xp_ble_editable_table(RemoteTable*, uint8_t*, const char*, const char*,
                                   const RemoteTable* (*)(const char*));
bool xp_ble_forget_remote(RemoteTable*, uint8_t*, const char*);
void xp_ble_follow(Router*, bool);
}

constexpr Binding makeBinding(const uint32_t code, const bool hold, const Action action) {
  return (code & kCodeMask) | (hold ? kHoldBit : 0u) | static_cast<uint32_t>(action) << kActionShift;
}
constexpr Action actionOf(const Binding b) { return static_cast<Action>(b >> kActionShift); }

// A slot read from the card is kept only when every bit means something: an action
// in range, a non-zero value, no unknown bit. Garbage is dropped rather than turned
// into a button that does something odd.
inline bool valid(const Binding b) { return xp_ble_valid(b); }

inline bool matches(const Binding b, const uint32_t code, const bool hold) { return xp_ble_matches(b, code, hold); }

// Lookup: at most 8 compares over 32 contiguous bytes, no allocation.
inline Action lookup(const RemoteTable& t, const uint32_t code, const bool hold) {
  return static_cast<Action>(xp_ble_lookup(&t, code, hold));
}

inline bool find(const RemoteTable& t, const Action action, Binding& out) {
  return xp_ble_find(&t, static_cast<uint8_t>(action), &out);
}

inline const char* actionName(const Action action) {
  static const char* const kNames[] = {"none",        "next",       "prev",    "next_chapter", "prev_chapter",
                                       "reader_menu", "save_quote", "confirm", "back"};
  return kNames[static_cast<uint8_t>(action) <= static_cast<uint8_t>(Action::Back) ? static_cast<uint8_t>(action) : 0];
}

// "3:1=02" (report id 3, byte 1, value 0x02); "1=02" for an any-report default slot;
// "0x43" for a button known only by the key the decoder read (byte index 0xFF).
inline void formatCode(char* out, const size_t n, const Binding b) {
  if (((b >> 8) & 0xFF) == 0xFF) {
    snprintf(out, n, "0x%02X", static_cast<unsigned>(b & 0xFF));
  } else if (b & kAnyReportBit) {
    snprintf(out, n, "%u=%02X", static_cast<unsigned>((b >> 8) & 0xFF), static_cast<unsigned>(b & 0xFF));
  } else {
    snprintf(out, n, "%u:%u=%02X", static_cast<unsigned>((b >> 16) & 0xFF), static_cast<unsigned>((b >> 8) & 0xFF),
             static_cast<unsigned>(b & 0xFF));
  }
}

// Translate SDK logical keys into reader actions without changing saved raw codes.
inline const RemoteTable* defaultTableFor(const char* name) {
  static constexpr auto tables = [] {
    std::array<RemoteTable, std::size(freeink::kRemoteButtonProfiles)> result{};
    // RemoteKey order: Right, Left, PageDown, PageUp.
    constexpr Action actions[] = {Action::NextPage, Action::PrevPage, Action::NextChapter, Action::PrevChapter};
    static_assert(std::size(actions) == static_cast<size_t>(freeink::RemoteKey::Count));
    static_assert(std::size(freeink::kRemoteButtonProfiles[0].buttons) <= kMaxBindings);
    for (size_t i = 0; i < result.size(); ++i) {
      const auto& profile = freeink::kRemoteButtonProfiles[i];
      result[i].count = profile.count;
      for (uint8_t j = 0; j < profile.count; ++j) {
        const auto& button = profile.buttons[j];
        result[i].bindings[j] = makeBinding(button.code, false, actions[static_cast<uint8_t>(button.key)]) |
                                (button.anyReport ? kAnyReportBit : 0u);
      }
    }
    return result;
  }();
  const int index = freeink::remoteButtonProfileIndex(name);
  return index < 0 ? nullptr : &tables[index];
}

// Table of the connected remote: the saved one for its address, else the default for
// its name, else none (nullptr = the old usage path, exactly as today). Writes nothing.
inline const RemoteTable* tableFor(const RemoteTable* tables, const uint8_t count, const char* addr, const char* name) {
  return xp_ble_table_for(tables, count, addr, name, defaultTableFor);
}

// Only a table with at least one slot changes the path. An empty table (cleared by
// the user) keeps its address so the default does not come back, and takes the old path.
inline bool routes(const RemoteTable* t) { return t != nullptr && t->count > 0; }

// Editable table of a remote: the saved one, or a new one copied from its default.
// nullptr when kMaxRemotes remotes already have one.
inline RemoteTable* editableTable(RemoteTable* tables, uint8_t& count, const char* addr, const char* name) {
  return xp_ble_editable_table(tables, &count, addr, name, defaultTableFor);
}

// Forgetting a device forgets its table too.
inline bool forgetRemote(RemoteTable* tables, uint8_t& count, const char* addr) {
  return xp_ble_forget_remote(tables, &count, addr);
}

inline bool clearAction(RemoteTable& t, const Action action) { return xp_ble_clear(&t, static_cast<uint8_t>(action)); }

// Store a learned button. An action has one button and a gesture (button + tap or
// hold) does one thing: the old slots of both go. false when the table is full,
// never a silent overwrite.
inline bool learn(RemoteTable& t, const Action action, const uint32_t code, const bool hold) {
  return xp_ble_learn(&t, static_cast<uint8_t>(action), code, hold);
}

// --- Decision for each raw edge ------------------------------------------------

// A press still waiting to become a tap or a hold. Only for a button WITH a hold slot.
struct HoldWait {
  uint32_t code = 0;
  uint32_t atMs = 0;
  Action tap = Action::None;
  Action hold = Action::None;
  bool active = false;
};

// The ONE decision for a raw edge of a remote with a table. `legacy` is what the old
// usage mapping gives the key read from the same frame: a button the table does not
// name keeps turning pages the way it always did.
//   - Press, no hold slot for the button: act now (page turns stay at 1-2 ms).
//   - Press, the button has a hold slot: wait; a release before kHoldMs is a tap,
//     later is a hold.
//   - Release: only ends a wait.
inline Action onRawEdge(const RemoteTable& t, const uint32_t code, const bool pressed, const uint32_t atMs,
                        const Action legacy, HoldWait& w) {
  return static_cast<Action>(xp_ble_raw_edge(&t, code, pressed, atMs, static_cast<uint8_t>(legacy), &w));
}

// Called every pass: a waiting button still down after kHoldMs is a hold, acted on
// now without waiting for the release.
inline Action pollHold(HoldWait& w, const uint32_t nowMs) { return static_cast<Action>(xp_ble_poll_hold(&w, nowMs)); }

// Tick state: the table is picked ONCE per link (off the hot path) and forgotten
// when the link changes or the reader leaves the front.
struct Router {
  const RemoteTable* table = nullptr;
  bool chosen = false;
  bool linked = false;
  HoldWait wait;

  void follow(const bool isLinked) { xp_ble_follow(this, isLinked); }
};

}  // namespace bleturner
