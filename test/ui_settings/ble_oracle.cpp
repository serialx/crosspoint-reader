// Frozen from bcdfb338 BleSettingsActivity::stepLearning/keepLearned.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
namespace bleturner {
enum class Action : uint8_t { None, NextPage };
struct Peer {
  const char* addr;
  const char* name;
};
struct Status {
  bool connected;
};
struct Event {
  uint32_t code;
  bool pressed;
  uint32_t atMs;
  bool wasRest;
};
struct RemoteTable {};
constexpr uint32_t kHoldMs = 700, kWaitMs = 15000;
static bool connection = true, same = true, tableOk = true, saveOk = true;
static std::vector<Event> events;
static unsigned head = 0;
Peer linked() { return {same ? "same" : "other", "remote"}; }
Status status() { return {connection}; }
bool pollEvent(Event& e) {
  if (head >= events.size()) return false;
  e = events[head++];
  return true;
}
RemoteTable* editableTable(RemoteTable* tables, uint8_t&, const char*, const char*) {
  return tableOk ? tables : nullptr;
}
static int held = -1;
bool learn(RemoteTable&, Action, uint32_t, bool hold) {
  held = hold;
  return saveOk;
}
}  // namespace bleturner
struct Settings {
  struct {
    bleturner::RemoteTable remotes[1];
    uint8_t remoteCount = 0;
  } ble;
  int saved = 0;
  void saveToFile() { ++saved; }
} SETTINGS;
static uint32_t now;
uint32_t millis() { return now; }
#define STR_BT_CONNECT_REMOTE "1"
#define STR_BT_NO_KEY "2"
#define STR_BT_KEYS_NOT_SAVED "3"
#define STR_BT_MAPPING_SAVED "4"
const char* tr(const char* s) { return s; }
class BleSettingsActivity {
 public:
  bleturner::Action learnAction = bleturner::Action::NextPage;
  char learnAddr[18] = "same";
  uint32_t learnCode = 0, learnPressMs = 0, learnSinceMs = 0;
  const char* learnNote = nullptr;
  int updates = 0;
  void refresh() {}
  void requestUpdate() { ++updates; }
  void stepLearning();
  void keepLearned(bool);
};
void BleSettingsActivity::stepLearning() {
  const auto peer = bleturner::linked();
  if (!bleturner::status().connected || strcmp(peer.addr, learnAddr) != 0) {
    learnAction = bleturner::Action::None;
    learnNote = tr(STR_BT_CONNECT_REMOTE);
    refresh();
    requestUpdate();
    return;
  }
  const auto action = learnAction;
  bleturner::Event ev;
  while (learnAction == action && bleturner::pollEvent(ev)) {
    if (ev.pressed) {
      learnCode = ev.code;
      learnPressMs = ev.atMs;
      learnSinceMs = millis();
    } else if (learnCode != 0 && ev.code == learnCode) {
      if (ev.wasRest) {
        learnCode = 0;  // the remote idling on a non-zero byte, not a button
      } else {
        // A button held past the reader's hold threshold is mapped as a hold, apart from its tap.
        keepLearned(ev.atMs - learnPressMs >= bleturner::kHoldMs);
      }
    }
  }
  if (learnAction == action) {
    // Allow the full learning window for a hold before falling back to a no-release tap.
    if (learnCode != 0 && millis() - learnSinceMs >= bleturner::kWaitMs) {
      keepLearned(false);
    } else if (learnCode == 0 && millis() - learnSinceMs >= bleturner::kWaitMs) {
      learnAction = bleturner::Action::None;
      learnNote = tr(STR_BT_NO_KEY);
    }
  }
  if (learnAction != action) {
    refresh();
    requestUpdate();
  }
}

void BleSettingsActivity::keepLearned(const bool hold) {
  // Preserve the remote's other bindings when changing the selected action.
  const auto peer = bleturner::linked();
  if (!bleturner::status().connected || strcmp(peer.addr, learnAddr) != 0) {
    learnAction = bleturner::Action::None;
    learnNote = tr(STR_BT_CONNECT_REMOTE);
    return;
  }
  bleturner::RemoteTable* table =
      bleturner::editableTable(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount, peer.addr, peer.name);
  if (table == nullptr || !bleturner::learn(*table, learnAction, learnCode, hold)) {
    learnAction = bleturner::Action::None;
    learnNote = tr(STR_BT_KEYS_NOT_SAVED);
    return;
  }
  SETTINGS.saveToFile();
  learnCode = 0;
  learnSinceMs = millis();
  learnAction = bleturner::Action::None;
  learnNote = tr(STR_BT_MAPPING_SAVED);
}

int main() {
  struct Step {
    uint32_t now, code, at;
    int kind;
    bool connection, same, table, save;
  };
  const std::vector<std::vector<Step>> cases = {
      {{1, 10, 100, 1, 1, 1, 1, 1}, {2, 10, 799, 2, 1, 1, 1, 1}},
      {{1, 10, 100, 1, 1, 1, 1, 1}, {2, 10, 800, 2, 1, 1, 1, 1}},
      {{1, 10, 100, 1, 1, 1, 1, 1}, {2, 10, 900, 3, 1, 1, 1, 1}, {15001, 0, 0, 0, 1, 1, 1, 1}},
      {{1, 10, 100, 1, 1, 1, 1, 1}, {15000, 0, 0, 0, 1, 1, 1, 1}, {15001, 0, 0, 0, 1, 1, 1, 1}},
      {{15000, 0, 0, 0, 1, 1, 1, 1}},
      {{1, 10, 100, 1, 1, 1, 1, 1}, {2, 10, 800, 2, 1, 1, 0, 1}},
      {{1, 10, 100, 1, 1, 1, 1, 1}, {2, 10, 800, 2, 1, 1, 1, 0}},
      {{1, 10, 100, 1, 1, 1, 1, 1}, {2, 0, 0, 0, 0, 1, 1, 1}},
      {{1, 10, 100, 1, 1, 1, 1, 1}, {2, 0, 0, 0, 1, 0, 1, 1}},
      {{4294967000u, 10, 4294967000u, 1, 1, 1, 1, 1}, {500, 10, 500, 2, 1, 1, 1, 1}},
      {{1, 10, 100, 1, 1, 1, 1, 1},
       {2, 11, 200, 2, 1, 1, 1, 1},
       {3, 12, 300, 1, 1, 1, 1, 1},
       {4, 12, 400, 2, 1, 1, 1, 1}},
      {{1, 0, 100, 1, 1, 1, 1, 1}, {2, 0, 900, 2, 1, 1, 1, 1}, {15001, 0, 0, 0, 1, 1, 1, 1}},
  };
  for (unsigned c = 0; c < cases.size(); ++c) {
    BleSettingsActivity screen;
    SETTINGS.saved = 0;
    bleturner::held = -1;
    for (auto s : cases[c]) {
      now = s.now;
      bleturner::connection = s.connection;
      bleturner::same = s.same;
      bleturner::tableOk = s.table;
      bleturner::saveOk = s.save;
      bleturner::head = 0;
      bleturner::events.clear();
      if (s.kind) bleturner::events.push_back({s.code, s.kind == 1, s.at, s.kind == 3});
      screen.stepLearning();
      std::cout << c << ' ' << s.now << ' ' << s.code << ' ' << s.at << ' ' << s.kind << ' ' << s.connection << ' '
                << s.same << ' ' << s.table << ' ' << s.save << ' ' << int(screen.learnAction) << ' '
                << (screen.learnNote ? screen.learnNote : "0") << ' ' << screen.learnCode << ' ' << screen.learnPressMs
                << ' ' << screen.learnSinceMs << ' ' << SETTINGS.saved << ' ' << bleturner::held << ' '
                << screen.updates << '\n';
    }
  }
}
