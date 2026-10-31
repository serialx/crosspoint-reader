#include "Trace.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <SPI.h>
#include <freertos/semphr.h>

#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
bool traceAllocations = false;
bool failPsramOnly = false;
int allocationsUntilFailure = -1;
uint64_t nowUs = 0, waitStart = 0, durationUs = 4000;
int waitPolarity = -1, idlePolarity = 0;
bool fullBytes = false;
std::vector<uint8_t> probeBytes;
size_t probeBit = 0;
void (*isr)() = nullptr;
std::string lastEvent;
size_t repetitions = 0;
int pinLevels[128] = {};
void flush() {
  if (!repetitions) return;
  std::cout << lastEvent;
  if (repetitions > 1) std::cout << " repeat=" << repetitions;
  std::cout << '\n';
  repetitions = 0;
}
void bytes(const uint8_t* data, size_t size) {
  std::ostringstream out;
  out << "spi.bytes n=" << size << ' ';
  if (fullBytes || size <= 32) {
    out << "hex=";
    for (size_t i = 0; i < size; ++i) out << std::hex << std::setfill('0') << std::setw(2) << unsigned(data[i]);
  } else {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * UINT64_C(1099511628211);
    out << "fnv1a64=" << std::hex << std::setfill('0') << std::setw(16) << hash;
  }
  trace::event(out.str());
}
}  // namespace
namespace trace {
void allocationTrace(int failAfter) {
  traceAllocations = true;
  failPsramOnly = failAfter == -2;
  allocationsUntilFailure = failAfter < 0 ? -1 : failAfter;
}
void allocationRecovery() {
  allocationsUntilFailure = -1;
  failPsramOnly = false;
}
static void* allocate(size_t bytes, const std::string& kind, bool psram = false) {
  bool failed = allocationsUntilFailure == 0 || (psram && failPsramOnly);
  if (allocationsUntilFailure > 0) --allocationsUntilFailure;
  if (traceAllocations)
    event("alloc " + kind + " bytes=" + std::to_string(bytes) + " failed=" + std::to_string(failed));
  return failed ? nullptr : std::malloc(bytes);
}
void* allocateCaps(size_t bytes, int caps) { return allocate(bytes, "caps=" + std::to_string(caps), caps & 1); }
void* allocateDefault(size_t bytes) { return allocate(bytes, "default"); }
void event(const std::string& text) {
  if (text == lastEvent) {
    ++repetitions;
    return;
  }
  flush();
  lastEvent = text;
  repetitions = 1;
}
void finish() { flush(); }
void configure(bool full, int polarity, std::vector<uint8_t> probe) {
  fullBytes = full;
  idlePolarity = polarity;
  probeBytes = std::move(probe);
}
void busyDuration(uint64_t us) { durationUs = us; }
void step(const std::string& name) { event("step " + name); }
void boardHook(const char* name, bool value) { event(std::string(name) + " " + std::to_string(value)); }
Wait::Wait(const char* kind, int polarity, const char* tag)
    : previousPolarity(waitPolarity), previousStart(waitStart), name(kind) {
  waitPolarity = polarity;
  waitStart = nowUs;
  event("wait.begin " + name + " polarity=" + std::to_string(polarity) + " tag=" + (tag ? tag : "-"));
}
Wait::~Wait() {
  event("wait.end " + name + " elapsed_us=" + std::to_string(nowUs - waitStart));
  waitPolarity = previousPolarity;
  waitStart = previousStart;
}
}  // namespace trace
unsigned long millis() { return nowUs / 1000; }
void delay(unsigned long ms) {
  trace::event("delay.ms " + std::to_string(ms));
  nowUs += ms * 1000;
}
void delayMicroseconds(unsigned int us) {
  trace::event("delay.us " + std::to_string(us));
  nowUs += us;
}
void pinMode(int pin, int mode) { trace::event("pin.mode " + std::to_string(pin) + " " + std::to_string(mode)); }
void digitalWrite(int pin, int level) {
  if (pin >= 0 && pin < 128) pinLevels[pin] = level;
  trace::event("pin.write " + std::to_string(pin) + " " + std::to_string(level));
}
int digitalRead(int pin) {
  int value = 0;
  if (pin == BoardConfig::ACTIVE.display.busy) {
    int polarity = waitPolarity < 0 ? idlePolarity : waitPolarity;
    bool working = waitPolarity >= 0 && nowUs - waitStart < durationUs;
    value = polarity == 0 ? working : !working;
  } else if (pin == BoardConfig::ACTIVE.display.mosi && probeBit < probeBytes.size() * 8) {
    value = (probeBytes[probeBit / 8] >> (7 - probeBit % 8)) & 1;
    ++probeBit;
  } else if (pin >= 0 && pin < 128) {
    value = pinLevels[pin];
  }
  trace::event("pin.read " + std::to_string(pin) + " " + std::to_string(value));
  return value;
}
void gpio_hold_dis(int pin) { trace::event("pin.hold_dis " + std::to_string(pin)); }
void attachInterrupt(int pin, void (*callback)(), int mode) {
  trace::event("irq.attach " + std::to_string(pin) + " " + std::to_string(mode));
  isr = callback;
}
void detachInterrupt(int pin) {
  trace::event("irq.detach " + std::to_string(pin));
  isr = nullptr;
}
SemaphoreHandle_t xSemaphoreCreateBinary() {
  static int semaphore;
  trace::event("semaphore.create");
  return &semaphore;
}
int xSemaphoreTake(SemaphoreHandle_t, unsigned int timeout) {
  trace::event("semaphore.take timeout_ms=" + std::to_string(timeout));
  if (!timeout) return pdFALSE;
  const uint64_t elapsed = nowUs - waitStart;
  const uint64_t remaining = durationUs > elapsed ? durationUs - elapsed : 0;
  if (isr && remaining && remaining <= timeout * 1000ULL) {
    nowUs += remaining;
    isr();
    return pdTRUE;
  }
  nowUs += timeout * 1000ULL;
  return pdFALSE;
}
void xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t*) { trace::event("semaphore.give.isr"); }
void FakeSPI::begin(int sclk, int miso, int mosi, int cs) {
  trace::event("spi.begin " + std::to_string(sclk) + " " + std::to_string(miso) + " " + std::to_string(mosi) + " " +
               std::to_string(cs));
}
void FakeSPI::end() { trace::event("spi.end"); }
void FakeSPI::beginTransaction(SPISettings settings) {
  trace::event("spi.txn.begin hz=" + std::to_string(settings.hz) + " order=" + std::to_string(settings.order) +
               " mode=" + std::to_string(settings.mode));
}
void FakeSPI::endTransaction() { trace::event("spi.txn.end"); }
uint8_t FakeSPI::transfer(uint8_t value) {
  bytes(&value, 1);
  return 0;
}
void FakeSPI::writeBytes(const uint8_t* data, size_t size) { bytes(data, size); }
void gpio_hold_en(int pin) { trace::event("pin.hold_en " + std::to_string(pin)); }
