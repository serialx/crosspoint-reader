// Frozen scalar expressions from bcdfb338 SettingsActivity::toggleSelectedSetting.
#include <cstdint>
#include <cstdio>
#include <initializer_list>

int main() {
  for (int type : {0, 1, 3}) {
    for (int byte : {0, 1, 2, 126, 127, 128, 129, 200, 254, 255}) {
      for (int count : {1, 2, 3, 10, 255}) {
        for (int minimum : {0, 1, 10, 128}) {
          for (int maximum : {10, 127, 128, 255}) {
            for (int increment : {1, 5, 128, 255}) {
              uint8_t value = byte;
              int operation = 6;
              if (type == 0) {
                const bool currentValue = value;
                value = !currentValue;
              } else if (type == 1) {
                const uint8_t currentValue = value;
                if (count > 2)
                  operation = 7;
                else
                  value = (currentValue + 1) % static_cast<uint8_t>(count);
              } else {
                const int8_t currentValue = value;
                if (currentValue + increment > maximum)
                  value = minimum;
                else
                  value = currentValue + increment;
              }
              std::printf("%d %d %d %d %d %d %d %u\n", type, byte, count, minimum, maximum, increment, operation,
                          value);
            }
          }
        }
      }
    }
  }
}
