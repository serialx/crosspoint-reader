#pragma once
#include <HalStorage.h>
#include <xp_hal.h>
namespace protection_test {
inline int64_t now = 1800002000;
inline const XpHalTimeServices timeServices{[]() {
                                              trace.push_back("clock");
                                              return now;
                                            },
                                            []() -> int64_t { return 0; }, [](int64_t) {},
                                            [](int64_t value) {
                                              trace.push_back("clock-floor");
                                              return value;
                                            },
                                            [](int64_t) {}};
}  // namespace protection_test
namespace trustedtime {
inline int64_t trustedNow() { return xp_hal_time_now(&protection_test::timeServices); }
}  // namespace trustedtime
