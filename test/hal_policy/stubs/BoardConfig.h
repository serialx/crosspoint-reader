#pragma once
#include <cstdio>
namespace BoardConfig {
enum class Board { XteinkX3, XteinkX3Uc8279, XteinkX4, XteinkX4Pro, XteinkX4Classic, Other };
enum class DisplayController { UC8253, UC8279 };
struct Config { int batteryAdc = 0; struct { int gaugeAddr = 0; } batteryGauge; Board board = Board::XteinkX4; DisplayController displayController = DisplayController::UC8253; int usbDetect = 20; struct { int power = 6; } input; };
inline Config ACTIVE;
inline void selectDevice(Board b) { printf("board %u\n", unsigned(b)); ACTIVE.board = b; }
inline bool hasHomeKey() { return false; }
inline bool isPaperMono() { return false; }
inline bool isM5PaperV11() { return false; }
inline bool isSticky() { return false; }
inline bool isMetalioEInk4() { return false; }
}
enum gpio_num_t { GPIO_NUM_13 = 13 };
