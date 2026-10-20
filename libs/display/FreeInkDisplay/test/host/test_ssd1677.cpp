#define FREEINK_DEVICE_METALIO_EINK4 1
#include <algorithm>
#include <array>
#include <cstdio>

#include "driver/CrossMuxSsd1677Driver.cpp"
using namespace freeink;
// Keep the existing CrossMux consumer fixture compatible with this isolated driver.
using Ssd1677Driver = CrossMuxSsd1677Driver;
const CrossMuxSsd1677Config& ssd1677MetalioConfig() { return crossmuxSsd1677MetalioConfig(); }

using Mode = RefreshMode;
using Policy = CrossMuxSsd1677CleanPolicy;
void expect(EpdBus& b, std::initializer_list<uint8_t> seq) { assert(b.sequences() == std::vector<uint8_t>(seq)); }
void parameters(EpdBus& b) {
  for (auto& e : b.events)
    if (e.cmd == 0x21) assert(e.bytes.size() == 2);
  assert(b.last(0x18) == std::vector<uint8_t>{0x80});
}

void testCustomLutTransfer() {
  alignas(uint32_t) std::array<uint8_t, 111> custom;
  for (size_t i = 0; i < custom.size(); ++i) custom[i] = static_cast<uint8_t>(i * 37 + 19);
  const auto original = custom;
  std::array<uint8_t, 32> fb;
  fb.fill(0xA5);
  bool batched = true;
  for (uint8_t border : {0x00, 0x80}) {
    auto cfg = crossmuxSsd1677DefaultConfig();
    cfg.borderWaveformGray = border;
    for (bool factory : {false, true}) {
      for (bool turnOff : {false, true}) {
        for (const auto* lut : {custom.data() + 1, static_cast<uint8_t*>(nullptr)}) {
          const auto* expected = lut ? lut : (factory ? lut_factory_quality : cfg.grayLut);
          CrossMuxSsd1677Driver driver(cfg);
          EpdBus bus;
          driver.begin(bus);
          bus.clear();
          driver.displayGray(bus, fb.data(), turnOff, lut, factory);
          std::vector<int> commands;
          for (const auto& e : bus.events)
            if (e.cmd >= 0) commands.push_back(e.cmd);
          std::vector<int> expectedCommands{0x32, 0x03, 0x04, 0x2C};
          if (border) expectedCommands.push_back(0x3C);
          expectedCommands.insert(expectedCommands.end(), {0x21, 0x22, 0x20});
          if (factory && turnOff) expectedCommands.insert(expectedCommands.end(), {0x3C, 0x22, 0x20});
          assert(commands == expectedCommands);
          assert(bus.events[0].bytes == std::vector<uint8_t>(expected, expected + 105));
          assert(bus.events[1].bytes == std::vector<uint8_t>{expected[105]});
          assert(bus.events[2].bytes == std::vector<uint8_t>(expected + 106, expected + 109));
          assert(bus.events[3].bytes == std::vector<uint8_t>{expected[109]});
          assert(bus.events[1].dataTransactions == 1 && bus.events[2].dataTransactions == 3 &&
                 bus.events[3].dataTransactions == 1);
          if (border) assert(bus.events[4].bytes == std::vector<uint8_t>{border});
          const size_t control = border ? 5 : 4;
          assert(bus.events[control].bytes == std::vector<uint8_t>{0x00});
          assert(bus.events[control + 1].bytes == std::vector<uint8_t>{uint8_t(!factory && turnOff ? 0xCF : 0xCC)});
          assert(bus.events[control + 2].bytes.empty());
          if (factory && turnOff) {
            assert(bus.last(0x3C) == std::vector<uint8_t>{cfg.borderWaveformInit});
            assert(bus.last(0x22) == std::vector<uint8_t>{0x03});
          }
          assert(custom == original);
          std::fprintf(stderr,
                       "CrossMux SSD1677 %s factory=%d off=%d border=%02x: LUT bytes=105, data transactions=%u\n",
                       lut ? "RAM" : "PROGMEM", factory, turnOff, border, bus.events[0].dataTransactions);
          batched = batched && bus.events[0].dataTransactions == 1;
        }
      }
    }
  }
  assert(batched);
}

// External drivers implementing only the original interface remain compatible.
struct LegacyDriver : PanelDriver {
  int calls = 0;
  uint32_t spiHz() const override { return 10000000; }
  BusyPolarity busyPolarity() const override { return BusyPolarity::ActiveHigh; }
  PanelGeometry geometry() const override { return {32, 8, 4, 32}; }
  void begin(EpdBus&) override {}
  void deepSleep(EpdBus&) override {}
  void display(EpdBus&, const uint8_t*, const uint8_t*, RefreshMode mode, bool off) override {
    assert(mode == Mode::Fast && off);
    calls |= 1;
  }
  bool displayStart(EpdBus&, const uint8_t*, const uint8_t*, RefreshMode mode, bool off) override {
    assert(mode == Mode::Half && !off);
    calls |= 2;
    return true;
  }
  void displayGrayscaleBase(EpdBus&, const uint8_t*, RefreshMode mode, bool off) override {
    assert(mode == Mode::Full && off);
    calls |= 4;
  }
};

void testDriverSequences() {
  LegacyDriver legacy;
  PanelDriver& compat = legacy;
  EpdBus compatBus;
  compat.displayWithContext(compatBus, nullptr, nullptr, Mode::Fast, true, RefreshContext::ContinuousReading);
  assert(compat.displayStartWithContext(compatBus, nullptr, nullptr, Mode::Half, false,
                                        RefreshContext::ContinuousReading));
  compat.displayGrayscaleBaseWithContext(compatBus, nullptr, Mode::Full, true, RefreshContext::ContinuousReading);
  assert(legacy.calls == 7);

  std::array<uint8_t, 32> fb;
  fb.fill(0xA5);
  const auto original = fb;
  {
    auto cfg = crossmuxSsd1677MetalioConfig();
    assert(cfg.cleanPolicy == Policy::BlackPulse);
    CrossMuxSsd1677Driver d(cfg);
    EpdBus b;
    d.begin(b);
    b.clear();
    d.display(b, fb.data(), nullptr, Mode::Full, false);
    expect(b, {0xF7});  // Explicit FULL must survive first-paint promotion.
    parameters(b);
    b.clear();
    d.displayGray(b, fb.data(), false, nullptr, false);
    expect(b, {0xCC});
    b.clear();
    d.cleanupGrayscaleBuffers(b, fb.data());
    assert(b.sequences().empty());
    b.clear();
    d.display(b, fb.data(), nullptr, Mode::Fast, false);
    expect(b, {0xFC, 0xFC});
    assert(b.last(0x24) == std::vector<uint8_t>(fb.begin(), fb.end()));
    assert(b.last(0x26) == b.last(0x24));
    parameters(b);
    b.clear();
    d.display(b, fb.data(), nullptr, Mode::Fast, false);
    expect(b, {0xFC});
    b.clear();  // one-shot clean was consumed
    d.displayGray(b, fb.data(), false, nullptr, false);
    d.cleanupGrayscaleBuffers(b, fb.data());
    b.clear();
    d.displayWindow(b, fb.data(), nullptr, 0, 0, 8, 1, false);
    expect(b, {0xFC, 0xFC});
    assert(b.last(0x24).size() == fb.size());
    b.clear();
    d.displayGray(b, fb.data(), false, nullptr, false);
    d.cleanupGrayscaleBuffers(b, fb.data());
    b.clear();
    d.displayGrayscaleBase(b, fb.data(), Mode::Fast, false);
    expect(b, {0xFC, 0xFC});
    b.clear();
    d.displayGray(b, fb.data(), false, nullptr, false);
    b.clear();
    d.deepSleep(b);
    expect(b, {0x83});
    assert(b.last(0x3c) == std::vector<uint8_t>{0x80});
    assert(b.events.back().cmd == 0x10 && b.events.back().bytes == std::vector<uint8_t>{3});
    d.begin(b);
    b.clear();
    d.display(b, fb.data(), nullptr, Mode::Full, true);
    b.clear();
    d.displayGray(b, fb.data(), true, nullptr, false);
    expect(b, {0xCF});
    b.clear();
    d.deepSleep(b);
    expect(b, {});  // already powered down
    d.begin(b);
    b.clear();
    // Leaving the reader does not inherit its one-frame baseline permission.
    d.display(b, fb.data(), nullptr, Mode::Fast, false);
    expect(b, {0xFC, 0xFC});
    b.clear();
    d.displayGray(b, fb.data(), false, nullptr, false);
    b.clear();
    // Reading context cannot reuse an unsynchronized grayscale baseline.
    d.displayWithContext(b, fb.data(), nullptr, Mode::Fast, false, RefreshContext::ContinuousReading);
    expect(b, {0xFC, 0xFC});
    assert(fb == original);
  }
  auto cfg = crossmuxSsd1677MetalioConfig();
  assert(cfg.cleanPolicy == Policy::BlackPulse);
  CrossMuxSsd1677Driver d(cfg);
  EpdBus b;
  d.begin(b);
  b.clear();
  assert(!d.displayStart(b, fb.data(), nullptr, Mode::Fast, true));
  expect(b, {0xFC, 0xFC, 0x83});
  std::vector<EpdBus::Event> planes;
  for (auto& e : b.events)
    if (e.cmd == 0x24 || e.cmd == 0x26) planes.push_back(e);
  assert(planes.size() == 7);
  // Even the white-clear build must keep ordinary text/manual/recovery black.
  constexpr uint8_t oldEndpoint = 0xFF;
  constexpr uint8_t cleanEndpoint = 0x00;
  assert(planes[0].cmd == 0x26 && planes[0].bytes == std::vector<uint8_t>(32, oldEndpoint));
  assert(planes[1].cmd == 0x24 && planes[1].bytes == std::vector<uint8_t>(32, cleanEndpoint));
  assert(planes[2].bytes == planes[1].bytes && planes[3].bytes == planes[1].bytes);
  assert(planes[4].bytes == std::vector<uint8_t>(fb.begin(), fb.end()));
  assert(planes[5].bytes == planes[4].bytes && planes[6].bytes == planes[4].bytes);
  b.clear();
  assert(d.displayStart(b, fb.data(), nullptr, Mode::Fast, true));
  expect(b, {0xFC});
  assert(b.busy);
  d.displayFinish(b, fb.data());
  expect(b, {0xFC, 0x83});
  assert(!b.busy);
  b.clear();
  d.display(b, fb.data(), nullptr, Mode::Half, false);
  expect(b, {0xFC, 0xFC});
  b.clear();
  b.stuck = true;
  d.display(b, fb.data(), nullptr, Mode::Half, false);
  expect(b, {0xFC});
  assert(b.events.back().cmd == -1);  // no writes after a timed-out first phase
  b.stuck = false;
  b.busy = false;
  b.clear();
  d.display(b, fb.data(), nullptr, Mode::Fast, false);
  expect(b, {0xFC, 0xFC});
  // A fast reading page does not physically erase residual gray; leaving the
  // reader still cleans even if this last page did not need a new AA overlay.
  b.clear();
  d.displayGray(b, fb.data(), false, nullptr, false);
  d.cleanupGrayscaleBuffers(b, fb.data());
  b.clear();
  d.displayWithContext(b, fb.data(), nullptr, Mode::Fast, false, RefreshContext::ContinuousReading);
  expect(b, {0xFC});
  b.clear();
  d.display(b, fb.data(), nullptr, Mode::Fast, false);
  expect(b, {0xFC, 0xFC});
  // Failure in the restore phase: no baseline writes, next request cleans.
  b.clear();
  b.failAt = b.activation + 2;
  d.display(b, fb.data(), nullptr, Mode::Half, false);
  expect(b, {0xFC, 0xFC});
  assert(b.events.back().cmd == -1);
  b.failAt = 0;
  b.busy = false;
  b.clear();
  d.display(b, fb.data(), nullptr, Mode::Fast, false);
  expect(b, {0xFC, 0xFC});
  // Failed analog power-off must not proceed to deep sleep or be marked off.
  b.clear();
  b.failAt = b.activation + 1;
  d.deepSleep(b);
  expect(b, {0x83});
  assert(b.last(0x10).empty());
  b.failAt = 0;
  b.busy = false;
  b.clear();
  d.deepSleep(b);
  expect(b, {0x83});
  assert(!b.last(0x10).empty());
  d.begin(b);
  b.clear();
  d.display(b, fb.data(), nullptr, Mode::Full, false);
  b.clear();
  d.displayGray(b, fb.data(), false, nullptr, false);
  d.cleanupGrayscaleBuffers(b, fb.data());
  b.clear();
  d.displayWithContext(b, fb.data(), nullptr, Mode::Full, false, RefreshContext::ContinuousReading);
  expect(b, {0xF7});  // Explicit FULL cannot be weakened by reading context.
  b.clear();
  assert(d.displayStart(b, fb.data(), nullptr, Mode::Fast, true));
  b.stuck = true;
  d.displayFinish(b, fb.data());
  expect(b, {0xFC});  // No park while the display is still busy.
  b.stuck = false;
  b.busy = false;
  b.clear();
  d.display(b, fb.data(), nullptr, Mode::Fast, false);
  expect(b, {0xFC, 0xFC});
  // Legacy devices retain their wire behavior and RED-only gray cleanup.
  for (const auto* legacy : {&crossmuxSsd1677DefaultConfig(), &crossmuxSsd1677Waveshare397Config()}) {
    CrossMuxSsd1677Driver old(*legacy);
    EpdBus bus;
    old.begin(bus);
    bus.clear();
    old.display(bus, fb.data(), nullptr, Mode::Fast, false);
    expect(bus, {0xD7});
    bus.clear();
    old.displayGray(bus, fb.data(), false, nullptr, false);
    old.cleanupGrayscaleBuffers(bus, fb.data());
    bus.clear();
    old.display(bus, fb.data(), nullptr, Mode::Fast, false);
    expect(bus, {legacy->fastSeqOverride});
    for (auto& e : bus.events)
      if (e.cmd == 0x21) assert(e.bytes.size() == 1);
  }
  // Absolute gray cannot inherit the continuous-reading overlay shortcut.
  for (const auto* cfg : {&crossmuxSsd1677DefaultConfig(), &crossmuxSsd1677MetalioConfig()}) {
    CrossMuxSsd1677Driver panel(*cfg);
    EpdBus bus;
    panel.begin(bus);
    panel.display(bus, fb.data(), nullptr, Mode::Full, false);
    panel.displayGray(bus, fb.data(), false, nullptr, true);
    panel.cleanupGrayscaleBuffers(bus, fb.data());
    bus.clear();
    panel.displayWithContext(bus, fb.data(), nullptr, Mode::Fast, false, RefreshContext::ContinuousReading);
    if (cfg->cleanPolicy == Policy::BlackPulse) {
      expect(bus, {0xFC, 0xFC});
    } else {
      expect(bus, {0xD7});
    }
    bus.clear();
    panel.display(bus, fb.data(), nullptr, Mode::Fast, false);
    expect(bus, {0xFC});  // Physical clean is consumed once.
  }
  for (const auto* cfg : {&crossmuxSsd1677Waveshare397Config(), &crossmuxSsd1677MetalioConfig(), &crossmuxSsd1677MurphyM4Config()}) {
    CrossMuxSsd1677Driver panel(*cfg);
    assert(!panel.grayscaleCapabilities(GrayscaleMode::Absolute).supported());
    assert(panel.grayscaleCapabilities(GrayscaleMode::Overlay).supported());
  }
  for (const auto mode : {Mode::Fast, Mode::Half, Mode::Full}) {
    CrossMuxSsd1677Driver panel;
    EpdBus bus;
    panel.begin(bus);
    bus.clear();
    panel.display(bus, fb.data(), nullptr, mode, true);
    expect(bus, {mode == Mode::Full ? uint8_t(0xF7) : uint8_t(0xD7)});
  }
  assert(fb == original);
}

void testImageTransitionPolicy() {
  std::array<uint8_t, 32> fb;
  fb.fill(0xA5);
  CrossMuxSsd1677Driver d(crossmuxSsd1677MetalioConfig());
  EpdBus b;
  d.begin(b);
  assert(d.opticalState() == OpticalState::Unknown);
  b.clear();
  d.displayWithContext(b, fb.data(), nullptr, Mode::Half, false, RefreshContext::ImageReading);
  expect(b, {0xFC, 0xFC});
  const auto firstBw = std::find_if(b.events.begin(), b.events.end(), [](const auto& e) { return e.cmd == 0x24; });
#if defined(FREEINK_SSD1677_READER_TRANSITIONS) && FREEINK_SSD1677_READER_TRANSITIONS
  assert(firstBw->bytes == std::vector<uint8_t>(32, 0xFF));
#else
  assert(firstBw->bytes == std::vector<uint8_t>(32, 0x00));
#endif
  assert(d.opticalState() == OpticalState::Bw);
  d.displayGray(b, fb.data(), false, nullptr, false);
  d.cleanupGrayscaleBuffers(b, fb.data());
  assert(d.opticalState() == OpticalState::Gray);  // RED sync never erases physical gray.
  b.clear();
  d.displayWithContext(b, fb.data(), nullptr, Mode::Fast, false, RefreshContext::ImageReading);
#if defined(FREEINK_SSD1677_READER_TRANSITIONS) && FREEINK_SSD1677_READER_TRANSITIONS
  expect(b, {0xFC});
  assert(d.opticalState() == OpticalState::Gray);
#else
  expect(b, {0xFC, 0xFC});
#endif
  d.requestResync(0);
  assert(d.opticalState() == OpticalState::Unknown);
  b.clear();
  d.displayWithContext(b, fb.data(), nullptr, Mode::Fast, false, RefreshContext::ImageReading);
  expect(b, {0xFC, 0xFC});
  d.deepSleep(b);
  d.begin(b);  // A new controller session cannot inherit the prior optical estimate.
  assert(d.opticalState() == OpticalState::Unknown);
}

#ifndef CROSSMUX_READER_REFRESH_TEST
int main() {
  testCustomLutTransfer();
  testDriverSequences();
  testImageTransitionPolicy();
}
#endif
