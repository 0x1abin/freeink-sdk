import os
from pathlib import Path
import subprocess
import tempfile

LIB = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'BoardConfig.h').write_text(r"""
#pragma once
#include <cstdint>
#define FREEINK_CAP_USB_MSC 1
#define FREEINK_DEVICE_METALIO_EINK4_PLUS 1
namespace freeink {
struct BoardConfig {
  static bool isMetalioEInk4Plus() { return true; }
};
namespace metalioplus { void routeUsbToOtg(bool otg); }
}
""")
    (tmp / 'SdFat.h').write_text(r"""
#pragma once
#include <cstddef>
#include <cstring>
using Sector_t = uint32_t;
struct FsBlockDeviceInterface {
  uint8_t data[1024]{};
  Sector_t sectorCount() const { return 2; }
  bool readSectors(Sector_t sector, uint8_t* out, size_t count) {
    std::memcpy(out, data + sector * 512, count * 512);
    return true;
  }
  bool writeSectors(Sector_t sector, const uint8_t* in, size_t count) {
    std::memcpy(data + sector * 512, in, count * 512);
    return true;
  }
  bool readSector(Sector_t sector, uint8_t* out) { return readSectors(sector, out, 1); }
  bool writeSector(Sector_t sector, const uint8_t* in) { return writeSectors(sector, in, 1); }
};
""")
    (tmp / 'Arduino.h').write_text('void delay(unsigned long);\n')
    (tmp / 'USB.h').write_text('inline struct UsbDevice { bool begin(); } USB;\n')
    (tmp / 'USBMSC.h').write_text(r"""
#include <cstdint>
inline bool mscOk = true;
inline int32_t (*readMsc)(uint32_t, uint32_t, void*, uint32_t) = nullptr;
inline int32_t (*writeMsc)(uint32_t, uint32_t, uint8_t*, uint32_t) = nullptr;
inline bool (*startStopMsc)(uint8_t, bool, bool) = nullptr;
struct USBMSC {
  void vendorID(const char*) {}
  void productID(const char*) {}
  void productRevision(const char*) {}
  void mediaPresent(bool) {}
  void isWritable(bool) {}
  bool begin(uint32_t count, uint16_t size);
  void end() { readMsc = nullptr; writeMsc = nullptr; startStopMsc = nullptr; }
  void onRead(decltype(readMsc) callback) { readMsc = callback; }
  void onWrite(decltype(writeMsc) callback) { writeMsc = callback; }
  void onStartStop(decltype(startStopMsc) callback) { startStopMsc = callback; }
};
""")
    (tmp / 'test.cpp').write_text(r"""
#include "UsbMassStorage.h"
#include <USB.h>
#include <USBMSC.h>
#include <cassert>
#include <string>

using freeink::UsbMassStorageState;

static std::string events;
static bool usbOk = true;
static bool mounted = false;

bool USBMSC::begin(uint32_t count, uint16_t size) {
  assert(count == 2 && size == 512);
  events += 'M';
  return mscOk;
}
bool UsbDevice::begin() { events += 'B'; return usbOk; }
void freeink::metalioplus::routeUsbToOtg(bool otg) { events += otg ? 'O' : 'J'; }
extern "C" bool tud_mounted() { return mounted; }
extern "C" bool tud_suspended() { return false; }
extern "C" bool tud_disconnect() { events += 'D'; return true; }
void delay(unsigned long) { mounted = false; events += 'W'; }

int main() {
  freeink::UsbMassStorage drive;
  FsBlockDeviceInterface card;
  mscOk = false;
  assert(!drive.begin(&card));
  assert(events == "M" && !drive.active() && readMsc == nullptr);
  events.clear();
  mscOk = true;
  usbOk = false;
  assert(!drive.begin(&card));
  assert(events == "MOBJ" && drive.state() == UsbMassStorageState::Idle && readMsc == nullptr);

  events.clear();
  usbOk = true;
  assert(drive.begin(&card));
  assert(events == "MOB" && drive.state() == UsbMassStorageState::WaitingForHost);
  mounted = true;
  assert(drive.state() == UsbMassStorageState::Connected);
  uint8_t data[]{1, 2, 3};
  assert(writeMsc(0, 511, data, 3) == 3);
  memset(data, 0, sizeof(data));
  assert(readMsc(0, 511, data, 3) == 3);
  assert(data[0] == 1 && data[1] == 2 && data[2] == 3);
  assert(drive.state() == UsbMassStorageState::Accessed);
  assert(startStopMsc(0, false, true));
  assert(drive.state() == UsbMassStorageState::Ejected);
  drive.end();
  assert(events == "MOBDWJ" && !drive.active() && readMsc == nullptr);
  assert(drive.state() == UsbMassStorageState::Idle);
}
""")
    binary = tmp / 'usb_msc'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(tmp), '-I', str(LIB / 'include'), str(tmp / 'test.cpp'),
                    str(LIB / 'src/UsbMassStorage.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
