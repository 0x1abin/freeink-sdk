#include "MetalioPlusDriver.h"

#if FREEINK_DRIVER_METALIO_PLUS
#include <MetalioPlusEpd.h>
namespace freeink {
namespace {
MetalioPlusRefresh refreshFor(RefreshMode mode) {
  switch (mode) {
    case RefreshMode::Full:
      return MetalioPlusRefresh::Full;
    case RefreshMode::Half:
      return MetalioPlusRefresh::Half;
    case RefreshMode::Fast:
      return MetalioPlusRefresh::Fast;
  }
  return MetalioPlusRefresh::Full;
}
}  // namespace

void MetalioPlusDriver::discardGrayPass() {
  grayPass = GrayPass::Idle;
  grayTarget = nullptr;
  grayRows[0] = grayRows[1] = 0;
  metalioPlusEpdCancelStage();
}

void MetalioPlusDriver::begin(EpdBus& bus) {
  (void)bus;
  discardGrayPass();
  committed = false;
  ready = metalioPlusEpdBegin();
}
void MetalioPlusDriver::deepSleep(EpdBus& bus) {
  (void)bus;
  discardGrayPass();
  metalioPlusEpdSleep();
  committed = false;
}
void MetalioPlusDriver::display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) {
  (void)bus;
  (void)prev;
  (void)turnOff;
  // Keep the existing checked power-off policy. The native PSRAM baseline
  // survives it; only sleep/resync/failure requires physical re-initialization.
  discardGrayPass();
  // Returning from a gray frame needs a gray-capable differential waveform.
  if (lastWasGray && mode == RefreshMode::Fast) mode = RefreshMode::Half;
  committed = ready && metalioPlusEpdDraw(fb, refreshFor(mode));
  if (committed) lastWasGray = false;
}
void MetalioPlusDriver::requestResync(uint8_t) {
  discardGrayPass();
  committed = false;
  metalioPlusEpdRequestResync();
}

GrayscaleCapabilities MetalioPlusDriver::grayscaleCapabilities(GrayscaleMode mode) const {
  if (mode != GrayscaleMode::Overlay) return {};
  return {GrayscaleEncoding::OverlayMasks, GrayscaleBase::Combined, true, false, false};
}

uint8_t* MetalioPlusDriver::beginGrayscale16() {
  discardGrayPass();
  committed = false;
  grayTarget = ready ? metalioPlusEpdStageNative() : nullptr;
  grayPass = grayTarget ? GrayPass::Native16 : GrayPass::Invalid;
  return grayTarget;
}

bool MetalioPlusDriver::commitGrayscale16(const uint8_t* bwProxy) {
  return commitGrayscale16WithMode(bwProxy, RefreshMode::Full);
}

bool MetalioPlusDriver::commitGrayscale16WithMode(const uint8_t* bwProxy, RefreshMode mode) {
  (void)bwProxy;  // The caller maintains a B/W proxy; native ink remains the real baseline.
  committed = false;
  if (grayPass != GrayPass::Native16 || !grayTarget) return false;
  committed = metalioPlusEpdCommit(mode == RefreshMode::Full ? MetalioPlusRefresh::Full : MetalioPlusRefresh::Half);
  if (committed) {
    lastWasGray = true;
    discardGrayPass();
  } else {
    grayPass = GrayPass::Failed;
  }
  return committed;
}

void MetalioPlusDriver::cancelGrayscale16() {
  if (grayPass == GrayPass::Native16 || grayPass == GrayPass::Failed) discardGrayPass();
}

void MetalioPlusDriver::beginGrayscale(EpdBus& bus, const uint8_t* fb, GrayscaleMode mode, RefreshMode fallback,
                                       bool turnOff) {
  (void)bus;
  (void)turnOff;
  discardGrayPass();
  committed = false;
  grayMode = fallback;
  grayTarget = ready && mode == GrayscaleMode::Overlay ? metalioPlusEpdStageBase(fb) : nullptr;
  grayPass = grayTarget ? GrayPass::Collecting : GrayPass::Invalid;
}

void MetalioPlusDriver::displayGrayscaleBase(EpdBus& bus, const uint8_t* fb, RefreshMode fallback, bool turnOff) {
  beginGrayscale(bus, fb, GrayscaleMode::Overlay, fallback, turnOff);
}

void MetalioPlusDriver::displayGrayscaleBaseWithContext(EpdBus& bus, const uint8_t* fb, RefreshMode fallback,
                                                        bool turnOff, RefreshContext context) {
  (void)context;
  displayGrayscaleBase(bus, fb, fallback, turnOff);
}

void MetalioPlusDriver::copyGrayscaleLsb(EpdBus& bus, const uint8_t* lsb) {
  writeGrayscalePlaneStrip(bus, GrayPlane::Lsb, lsb, 0, geometry().height);
}

void MetalioPlusDriver::copyGrayscaleMsb(EpdBus& bus, const uint8_t* msb) {
  writeGrayscalePlaneStrip(bus, GrayPlane::Msb, msb, 0, geometry().height);
}

void MetalioPlusDriver::writeGrayscalePlaneStrip(EpdBus& bus, GrayPlane plane, const uint8_t* rows, uint16_t yStart,
                                                 uint16_t numRows) {
  (void)bus;
  if (grayPass != GrayPass::Collecting) return;
  const auto g = geometry();
  const bool msb = plane == GrayPlane::Msb;
  const unsigned index = msb ? 1 : 0;
  if (!rows || !numRows || yStart != grayRows[index] || yStart >= g.height || numRows > g.height - yStart ||
      (msb && grayRows[0] != g.height)) {
    grayPass = GrayPass::Invalid;
    return;
  }
  const size_t first = static_cast<size_t>(yStart) * g.widthBytes;
  const size_t bytes = static_cast<size_t>(numRows) * g.widthBytes;
  for (size_t byte = 0; byte < bytes; ++byte) {
    for (unsigned pixel = 0; pixel < 8; ++pixel) {
      const bool set = (rows[byte] & (0x80u >> pixel)) != 0;
      uint8_t& pair = grayTarget[(first + byte) * 4 + pixel / 2];
      const unsigned shift = (pixel & 1) * 4;
      const uint8_t value = (pair >> shift) & 0x0f;
      // LSB marks dark; MSB selects dark/light. Base endpoints remain intact.
      if (msb && !set && value == 5) {
        grayPass = GrayPass::Invalid;
        return;
      }
      if (set) {
        const uint8_t gray = !msb || value == 5 ? 5 : 10;
        pair = (pair & ~(0x0fu << shift)) | (gray << shift);
      }
    }
  }
  grayRows[index] += numRows;
}

void MetalioPlusDriver::displayGray(EpdBus& bus, const uint8_t* fb, bool turnOff, const unsigned char* lut,
                                    bool factoryMode) {
  (void)bus;
  (void)fb;
  (void)turnOff;
  (void)factoryMode;
  committed = false;
  if (grayPass != GrayPass::Collecting || lut || grayRows[0] != geometry().height || grayRows[1] != geometry().height)
    return;
  const auto mode = grayMode == RefreshMode::Full ? MetalioPlusRefresh::Full : MetalioPlusRefresh::Half;
  committed = metalioPlusEpdCommit(mode);
  if (committed) {
    lastWasGray = true;
    discardGrayPass();
  } else {
    grayPass = GrayPass::Failed;
  }
}

void MetalioPlusDriver::cleanupGrayscaleBuffers(EpdBus& bus, const uint8_t* bw) {
  switch (grayPass) {
    case GrayPass::Idle:
      return;  // Preserve the completed native gray baseline.
    case GrayPass::Collecting:
    case GrayPass::Invalid:
      if (bw) {
        display(bus, bw, nullptr, grayMode, false);
        return;
      }
      break;
    case GrayPass::Failed:
      break;  // Do not immediately retry failed hardware work.
    case GrayPass::Native16:
      break;  // Cancellation is not permission to display a native target or its proxy.
  }
  discardGrayPass();
}
PanelDriver& metalioPlusDriver() {
  static MetalioPlusDriver driver;
  return driver;
}
}  // namespace freeink
#endif
