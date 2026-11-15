#pragma once
#include <ArduinoJson.h>
#include <ObfuscationUtils.h>
namespace fake {
inline int resaves = 0;
inline std::string persisted;
}  // namespace fake
template <class T>
class PersistableStore {
 protected:
  void requestResave() { ++fake::resaves; }
  static std::string extractPassword(JsonVariantConst doc, bool& resave) {
    bool ok = false;
    auto s = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
    if (!ok) {
      s = doc["password"] | "";
      if (!s.empty()) resave = true;
    }
    return s;
  }

 public:
  static T& getInstance() {
    static T t;
    return t;
  }
  bool saveToFile() const {
    JsonDocument d;
    static_cast<const T*>(this)->toJson(d);
    fake::persisted.clear();
    serializeJson(d, fake::persisted);
    return true;
  }
};
