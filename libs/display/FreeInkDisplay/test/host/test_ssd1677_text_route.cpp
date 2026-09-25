#include <esp_heap_caps.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <vector>
#define private public
#include "FreeInkDisplay.h"
#include "src/driver/PaperMonoDriver.h"
#undef private

using namespace freeink;
using Mode = FreeInkDisplay;
using Bytes = std::vector<uint8_t>;

static Bytes last(const EpdBus& bus, uint8_t command) {
  for (auto i = bus.writes.rbegin(); i != bus.writes.rend(); ++i)
    if (i->command == command) return i->bytes;
  return {};
}
static int pixels(const EpdBus& bus) {
  int n = 0;
  for (const auto& w : bus.writes)
    if (w.command == 0x22 && (w.bytes.at(0) & 4)) ++n;
  return n;
}

#if FREEINK_DEVICE_PAPERMONO
int main(int argc, char** argv) {
  assert(argc == 2);
  BoardConfig::ACTIVE.board = BoardConfig::Board::PaperMono;
  BoardConfig::ACTIVE.orientation.mirrorX = true;
  BoardConfig::ACTIVE.orientation.mirrorY = true;
  allocationFailAt = std::atoi(argv[1]);
  FreeInkDisplay d(0, 0, 0, 0, 0, 0);
  d.begin();
  assert(d.supportsTextOnlyCombinedBase() == (allocationFailAt == 0));
  if (allocationFailAt) {
    assert(liveAllocations == 0);
    d.displayBuffer(Mode::FAST_REFRESH, false);
    assert(pixels(d._bus) > 0);
  } else {
    assert(liveAllocations == 8);
    auto& bus = d._bus;
    d.getFrameBuffer()[0] = 0x7f;
    d.displayBuffer(Mode::FAST_REFRESH, false);
    bus.sleepBusyUntilReset = true;
    d.controllerIdle();
    assert(bus.busy);
    const auto resetBeforeWake = bus.resets;
    d.getFrameBuffer()[1] = 0x7f;
    d.displayBuffer(Mode::FAST_REFRESH, false);
    assert(bus.resets > resetBeforeWake && d.displayCommitted());
  }
}
#else
int main(int argc, char** argv) {
  assert(argc == 2);
#if FREEINK_DEVICE_STICKY
  BoardConfig::ACTIVE.board = BoardConfig::Board::Sticky;
#elif FREEINK_DEVICE_MURPHY_M4
  BoardConfig::ACTIVE.board = BoardConfig::Board::MurphyM4;
#elif FREEINK_DEVICE_WAVESHARE_EPAPER_397
  BoardConfig::ACTIVE.board = BoardConfig::Board::WaveshareEpaper397;
#elif FREEINK_DEVICE_METALIO_EINK4
  BoardConfig::ACTIVE.board = BoardConfig::Board::MetalioEink4;
#elif FREEINK_DEVICE_X4CLASSIC
  BoardConfig::ACTIVE.board = BoardConfig::Board::XteinkX4Classic;
#endif
  const bool uc = std::strncmp(argv[1], "uc", 2) == 0;
  if (uc)
    BoardConfig::ACTIVE.displayController = std::strcmp(argv[1], "uc8179") == 0
                                                ? BoardConfig::DisplayController::UC8179
                                                : BoardConfig::DisplayController::UC8279;
  BoardConfig::ACTIVE.orientation.mirrorX = std::strcmp(argv[1], "rotated") == 0;
  BoardConfig::ACTIVE.orientation.mirrorY = BoardConfig::ACTIVE.orientation.mirrorX;
  allocationFailAt = std::atoi(argv[1]);
  FreeInkDisplay d(0, 0, 0, 0, 0, 0);
#if FREEINK_DEVICE_MURPHY_M4
  if (std::strcmp(argv[1], "batch1") == 0) d.setMurphyM4Batch(MurphyM4Batch::First);
#endif
  d.begin();
  if (uc || allocationFailAt) {
    assert(!d.supportsTextOnlyCombinedBase());
    assert(paperMonoDriver()._pendingBw == nullptr);
    if (!uc) assert(liveAllocations == 0);
    assert(d._driver == d._originalDriver);
    return 0;
  }
  assert(d.supportsTextOnlyCombinedBase());
  assert(liveAllocations == 8);
  auto& bus = d._bus;
  auto& combined = paperMonoDriver();
  std::array<uint8_t, 48000> bw, lsb{}, msb{};
  bw.fill(0xff);
  bw[0] = 0x7f;
  lsb[1] = 0x80;
  msb[1] = 0x40;
  const auto stage = [&](Mode::RefreshMode mode) {
    std::memcpy(d.getFrameBuffer(), bw.data(), bw.size());
    d.displayGrayscaleBase(mode, false, RefreshContext::TextOnlyAntiAliasing);
    d.copyGrayscaleBuffers(lsb.data(), msb.data());
  };
  stage(Mode::HALF_REFRESH);
  assert(pixels(bus) == 0);
  d.displayGrayBuffer(false);
  assert(d.displayCommitted());
  const auto scan = last(bus, 0x01);
  d.cleanupGrayscaleBuffers(bw.data());
  bus.clear();
  bw[100] = 0;
  stage(Mode::FAST_REFRESH);
  assert(pixels(bus) == 0);
  d.displayGrayBuffer(false);
  assert(pixels(bus) == 1);
  const auto initialLut = last(bus, 0x32);
  assert(initialLut.size() == 105);
  assert(last(bus, 0x11) == Bytes{static_cast<uint8_t>(BoardConfig::ACTIVE.orientation.mirrorX ? 0 : 1)});
  assert(scan.at(2) == (BoardConfig::ACTIVE.orientation.mirrorY ? 3 : 2));
  assert(last(bus, 0x4F) == (Bytes{0xdf, 1}));
  assert(last(bus, 0x24).at(100) == 0xff);  // native byte order, no Paper Mono mount transform

  d.cleanupGrayscaleBuffers(bw.data());
  assert(!d._textAaPending);
  // Gray failure commits B/W; cancellation does not commit any pixels.
  bus.clear();
  bw[101] = 0;
  std::memcpy(d.getFrameBuffer(), bw.data(), bw.size());
  d.displayGrayscaleBase(Mode::FAST_REFRESH, false, RefreshContext::TextOnlyAntiAliasing);
  d.copyGrayscaleLsbBuffers(lsb.data());
  d.displayGrayBuffer(false);
  assert(!last(bus, 0x24).empty() && last(bus, 0x32).empty());
  d.cleanupGrayscaleBuffers(bw.data());
  bus.clear();
  stage(Mode::FAST_REFRESH);
  d.abortPostRefresh();
  d.cleanupGrayscaleBuffers(bw.data());
  assert(pixels(bus) == 0);

  // Compare the complete image transaction to the ordinary driver, including
  // both intermediate tones, LUT, power and cleanup commands.
  auto* original = d._originalDriver;
  EpdBus expected;
  original->begin(expected);
  expected.clear();
  original->displayGrayscaleBaseWithContext(expected, bw.data(), RefreshMode::Fast, false, RefreshContext::Normal);
  original->copyGrayscaleLsb(expected, lsb.data());
  original->copyGrayscaleMsb(expected, msb.data());
  original->displayGray(expected, bw.data(), false, nullptr, false);
  original->cleanupGrayscaleBuffers(expected, bw.data());
  d.selectTextAaDriver(false);
  bus.clear();
  std::memcpy(d.getFrameBuffer(), bw.data(), bw.size());
  d.displayGrayscaleBase(Mode::FAST_REFRESH, false);
  d.copyGrayscaleBuffers(lsb.data(), msb.data());
  assert(last(bus, 0x24) == Bytes(lsb.begin(), lsb.end()));
  assert(last(bus, 0x26) == Bytes(msb.begin(), msb.end()));
  d.displayGrayBuffer(false);
  d.cleanupGrayscaleBuffers(bw.data());
  assert(bus.writes.size() == expected.writes.size());
  for (size_t i = 0; i < bus.writes.size(); ++i) {
    assert(bus.writes[i].command == expected.writes[i].command);
    assert(bus.writes[i].bytes == expected.writes[i].bytes);
  }
  assert(d._driver == original);

  // A normal pending async waveform must finish before the next page checks BUSY.
  if (original->supportsAsyncDisplay()) {
    bus.clear();
    d.displayBufferAsync(Mode::FAST_REFRESH);
#if FREEINK_DEVICE_STICKY
    assert(d._refreshPending);
#endif
    if (d._refreshPending) {
      assert(bus.busy);
      const auto firstActivation = bus.activation;
      d.displayBufferAsync(Mode::FAST_REFRESH);
      assert(bus.activation > firstActivation && d._refreshPending);
      d.finishDisplayAsync();
      assert(!bus.busy && !d._refreshPending);
    }
  }
#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_X4CLASSIC
  // A timed-out async refresh cannot run the deferred power-off SPI sequence.
  bus.clear();
  d.triggerDisplayAsync(Mode::FAST_REFRESH, true);
  assert(d._refreshPending);
  bus.failAt = bus.activation;
  const auto timedOutWrites = bus.writes.size();
  d.finishDisplayAsync();
  assert(bus.busy && bus.writes.size() == timedOutWrites);
  bus.failAt = 0;
  bus.busy = false;
#endif

  // Deep sleep may hold BUSY until hardware reset; both routing directions must recover.
  bus.sleepBusyUntilReset = true;
  const auto firstReset = bus.resets;
  stage(Mode::FAST_REFRESH);
  assert(d._driver == &combined && d.combinesGrayscaleBase() && bus.resets > firstReset);
  d.displayGrayBuffer(false);
  d.cleanupGrayscaleBuffers(bw.data());
  const auto secondReset = bus.resets;
  d.selectTextAaDriver(false);
  assert(d._driver == original && !bus.busy && bus.resets > secondReset);
  bus.sleepBusyUntilReset = false;

  // Idle sleep retains the selected text driver but invalidates its controller registers.
  stage(Mode::FAST_REFRESH);
  d.displayGrayBuffer(false);
  d.cleanupGrayscaleBuffers(bw.data());
  bus.sleepBusyUntilReset = true;
  assert(!combined._needsFull);
  d.controllerIdle();
  assert(bus.busy && !d._textDriverReady);
  const auto idleReset = bus.resets;
  stage(Mode::FAST_REFRESH);
  assert(bus.resets > idleReset && d.combinesGrayscaleBase());
  assert(!combined._needsFull);
  d.displayGrayBuffer(false);
  d.cleanupGrayscaleBuffers(bw.data());
  bus.sleepBusyUntilReset = false;
  d.selectTextAaDriver(false);

  // If reset cannot release BUSY, do not send controller commands or claim a text transaction.
  bus.sleepBusyUntilReset = true;
  bus.resetKeepsBusy = true;
  d.displayGrayscaleBase(Mode::FAST_REFRESH, false, RefreshContext::TextOnlyAntiAliasing);
  assert(bus.busy && !d._textDriverReady && !d.combinesGrayscaleBase());
  assert(bus.writes.back().command == 0x10);
  bus.resetKeepsBusy = false;
  stage(Mode::FAST_REFRESH);
  assert(d._driver == &combined && d.combinesGrayscaleBase());
  d.displayGrayBuffer(false);
  d.cleanupGrayscaleBuffers(bw.data());
  d.selectTextAaDriver(false);
  bus.sleepBusyUntilReset = false;

  // An actual BUSY timeout must not make the original grayscale cleanup write RAM.
  bus.busy = true;
  const auto stoppedOriginal = bus.writes.size();
  d.cleanupGrayscaleBuffers(bw.data());
  assert(bus.writes.size() == stoppedOriginal && !d._textDriverReady);
  bus.busy = false;

  stage(Mode::FAST_REFRESH);
  d.displayGrayBuffer(true);
  d.cleanupGrayscaleBuffers(bw.data());
  assert(d._driver == &combined);
#if FREEINK_DEVICE_METALIO_EINK4
  assert(last(bus, 0x22) == Bytes{0x83});
#endif
  d.deepSleep();
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  assert(last(bus, 0x10) == Bytes{0x01});
#else
  assert(last(bus, 0x10) == Bytes{0x03});
#endif
  bus.clear();
  stage(Mode::FAST_REFRESH);
  d.displayGrayBuffer(false);
  assert(pixels(bus) >= 2);
  d.cleanupGrayscaleBuffers(bw.data());
  // Timeouts in both the custom waveform and original corrective B/W must
  // stop all subsequent SPI writes and require a clean recovery.
  for (auto mode : {Mode::FAST_REFRESH, Mode::HALF_REFRESH}) {
    bus.clear();
    bw[102] ^= 0xff;
    stage(mode);
    bus.failAt = bus.activation + 1;
    d.displayGrayBuffer(true);
    assert(bus.busy && !d.displayCommitted());
    const auto stopped = bus.writes.size();
    d.cleanupGrayscaleBuffers(bw.data());
    d.controllerIdle();
    d.deepSleep();
    assert(bus.writes.size() == stopped);
    bus.failAt = 0;
    bus.busy = false;
    bus.clear();
    stage(Mode::FAST_REFRESH);
    d.displayGrayBuffer(false);
    assert(d.displayCommitted() && pixels(bus) >= 2);
    d.cleanupGrayscaleBuffers(bw.data());
  }
  assert(allocationCalls == 8);
}

#endif
