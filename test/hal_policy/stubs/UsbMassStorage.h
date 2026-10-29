#pragma once
#include <Arduino.h>
inline bool usbBegin=true;
namespace freeink {
enum class UsbMassStorageState { WaitingForHost,Connected,Accessed,Ejected,Disconnected,IoError,Idle };
class UsbMassStorage {
public:
 bool begin(int*) {puts("usb begin");return usbBegin;}
 bool disconnectHost() {puts("usb disconnect");return true;}
 bool hostSuspended() const {return false;}
 void end() {puts("usb end");}
 UsbMassStorageState state() const {return UsbMassStorageState::Idle;}
};
}
