#pragma once

#include <Print.h>
#include <XmlPullParser.h>
#include <XmlQName.h>

#include <climits>

// Stream only the algorithm attributes; resource names stay in the ZIP.
class EncryptionManifestProbe : public Print {
 public:
  ~EncryptionManifestProbe() override = default;

  bool setup() { return parser.setup(this, onStartElement); }

  size_t write(uint8_t data) override { return write(&data, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    if (!parser || size > INT_MAX || !parser.write(data, size, false)) {
      return 0;
    }
    return size;
  }

  bool finish() { return parser && parser.write(reinterpret_cast<const uint8_t*>(""), 0, true); }
  bool needsProtectionCheck() const { return aes128; }

 private:
  XmlPullParser parser;
  bool aes128 = false;

  static void onStartElement(void* context, const char* name, const char* const* attributes) {
    if (!xmlLocalNameEquals(name, "EncryptionMethod")) return;
    for (size_t i = 0; attributes[i]; i += 2) {
      // Match the SDK's supported algorithm; it validates resource references.
      if (xmlLocalNameEquals(attributes[i], "Algorithm") &&
          (std::strstr(attributes[i + 1], "aes128-cbc") || std::strstr(attributes[i + 1], "aes256-cbc"))) {
        static_cast<EncryptionManifestProbe*>(context)->aes128 = true;
      }
    }
  }
};
