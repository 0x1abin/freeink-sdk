#include "MetalioPlusDriver.h"

#if FREEINK_DRIVER_METALIO_PLUS
#include <MetalioPlusEpd.h>
namespace freeink {
void MetalioPlusDriver::begin(EpdBus& bus) {
  (void)bus;
  committed = false;
  ready = metalioPlusEpdBegin();
}
void MetalioPlusDriver::deepSleep(EpdBus& bus) {
  (void)bus;
  metalioPlusEpdSleep();
  committed = false;
}
void MetalioPlusDriver::display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) {
  (void)bus;
  (void)prev;
  (void)turnOff;
  // Keep the existing checked power-off policy. The native PSRAM baseline
  // survives it; only sleep/resync/failure requires physical re-initialization.
  auto refresh = MetalioPlusRefresh::Full;
  switch (mode) {
    case RefreshMode::Full:
      break;
    case RefreshMode::Half:
      refresh = MetalioPlusRefresh::Half;
      break;
    case RefreshMode::Fast:
      refresh = MetalioPlusRefresh::Fast;
      break;
  }
  committed = ready && metalioPlusEpdDraw(fb, refresh);
}
void MetalioPlusDriver::requestResync(uint8_t) {
  committed = false;
  metalioPlusEpdRequestResync();
}
PanelDriver& metalioPlusDriver() {
  static MetalioPlusDriver driver;
  return driver;
}
}  // namespace freeink
#endif
