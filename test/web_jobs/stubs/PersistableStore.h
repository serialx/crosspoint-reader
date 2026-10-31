#pragma once
#include <ArduinoJson.h>

#include "Oracle.h"
namespace oracle {
inline std::string deviceId, privHex;
}
template <class T>
class PersistableStore {
 public:
  static T& getInstance() {
    static T value;
    return value;
  }
  bool loadFromFile() {
    oracle::record("load %s", T::getFilePath());
    JsonDocument doc;
    doc["device_id"] = oracle::deviceId;
    doc["priv_obf"] = oracle::privHex;
    return static_cast<T*>(this)->fromJson(doc.as<JsonVariantConst>());
  }
  bool saveToFile() {
    JsonDocument doc;
    static_cast<T*>(this)->toJson(doc);
    std::string json;
    serializeJson(doc, json);
    oracle::record("save %s %s", T::getFilePath(), json.c_str());
    return oracle::fault != "save";
  }
};
