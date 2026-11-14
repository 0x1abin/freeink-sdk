#pragma once

#include <BoardConfig.h>

#include "PanelDriver.h"

#if FREEINK_DRIVER_METALIO_PLUS
namespace freeink {
class MetalioPlusDriver : public PanelDriver {
 public:
  const char* controllerName() const override { return "Metalio Plus S31"; }
  uint32_t spiHz() const override { return 0; }
  BusyPolarity busyPolarity() const override { return BusyPolarity::ActiveLow; }
  bool usesExternalBus() const override { return true; }
  PanelGeometry geometry() const override { return {1216, 684, 152, 103968}; }
  void begin(EpdBus& bus) override;
  void deepSleep(EpdBus& bus) override;
  void display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;
  void requestResync(uint8_t) override;
  bool displayCommitted() const override { return committed; }

 private:
  bool ready = false;
  bool committed = false;
};
PanelDriver& metalioPlusDriver();
}  // namespace freeink
#endif
