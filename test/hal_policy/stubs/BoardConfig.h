#pragma once
namespace BoardConfig {
struct Config { int batteryAdc = 0; struct { int gaugeAddr = 0; } batteryGauge; };
inline Config ACTIVE;
}
enum gpio_num_t { GPIO_NUM_13 = 13 };
