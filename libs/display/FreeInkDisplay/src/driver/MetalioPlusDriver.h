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
  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode mode = GrayscaleMode::Overlay) const override;
  void beginGrayscale(EpdBus& bus, const uint8_t* fb, GrayscaleMode mode, RefreshMode fallback, bool turnOff) override;
  void displayGrayscaleBase(EpdBus& bus, const uint8_t* fb, RefreshMode fallback, bool turnOff) override;
  void displayGrayscaleBaseWithContext(EpdBus& bus, const uint8_t* fb, RefreshMode fallback, bool turnOff,
                                       RefreshContext context) override;
  void copyGrayscaleLsb(EpdBus& bus, const uint8_t* lsb) override;
  void copyGrayscaleMsb(EpdBus& bus, const uint8_t* msb) override;
  void writeGrayscalePlaneStrip(EpdBus& bus, GrayPlane plane, const uint8_t* rows, uint16_t yStart,
                                uint16_t numRows) override;
  void displayGray(EpdBus& bus, const uint8_t* fb, bool turnOff, const unsigned char* lut, bool factoryMode) override;
  void cleanupGrayscaleBuffers(EpdBus& bus, const uint8_t* bw) override;

 private:
  enum class GrayPass : uint8_t { Idle, Collecting, Invalid, Failed };
  void discardGrayPass();
  bool ready = false;
  bool committed = false;
  bool lastWasGray = false;
  GrayPass grayPass = GrayPass::Idle;
  uint8_t* grayTarget = nullptr;
  uint16_t grayRows[2] = {};
  RefreshMode grayMode = RefreshMode::Full;
};
PanelDriver& metalioPlusDriver();
}  // namespace freeink
#endif
