#include <iostream>

#include "Oracle.h"
#include "UsbSerialJtagHandoff.h"
int main() {
  for (const char* fault : {"", "semaphore", "interrupt", "timeout"}) {
    oracle::reset();
    oracle::fault = fault;
    handoffUsbOtgToSerialJtag();
    std::cout << "CASE usb-" << fault << '\n';
    for (const auto& line : oracle::trace) std::cout << line << '\n';
  }
}
