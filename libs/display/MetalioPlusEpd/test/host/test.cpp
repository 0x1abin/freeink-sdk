#include <MetalioPlusEpd.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "src/driver/MetalioPlusDriver.h"
extern "C" {
#include "vendor_api.h"
uint8_t nativeFrame[1216 * 684 / 2];
EpdiyHighlevelState highlevel{nativeFrame};
bool initOk = true, powerOk = true, offOk = true, clearOk = true, drawOk = true, portReadOk = true, geometryOk = true;
int initCalls = 0, powerCalls = 0, offCalls = 0, clearCalls = 0, drawCalls = 0;
int fullDraws = 0, diffDraws = 0, lastMode = 0;
uint8_t previousFrame[1216 * 684 / 2];
uint8_t sourceFirst = 0xff;
bool MetalioEpd_Init() {
  ++initCalls;
  return initOk;
}
EpdiyHighlevelState* MetalioEpd_Hl() { return &highlevel; }
int epd_width() { return geometryOk ? 1216 : 800; }
int epd_height() { return 684; }
void epd_poweron() { ++powerCalls; }
void epd_poweroff() { ++offCalls; }
bool epd_board_metalio_eink4_plus_tps_ok() { return powerOk; }
bool epd_board_metalio_eink4_plus_ioexp_read_port(int port, uint8_t* value) {
  assert(port == 0);
  *value = offOk ? 0 : 0x80;
  return portReadOk;
}
uint8_t* epd_hl_get_framebuffer(EpdiyHighlevelState* hl) { return hl->front_fb; }
EpdDrawError epd_clear_checked() {
  ++clearCalls;
  return clearOk ? EPD_DRAW_SUCCESS : EPD_DRAW_FAILED;
}
EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState* hl, int mode, int temperature) {
  assert(hl == &highlevel && mode == MODE_GC16 && temperature == 25);
  ++drawCalls;
  ++fullDraws;
  lastMode = mode;
  std::memset(previousFrame, 0xff, sizeof(previousFrame));
  if (drawOk) std::memcpy(previousFrame, nativeFrame, sizeof(previousFrame));
  return drawOk ? EPD_DRAW_SUCCESS : EPD_DRAW_FAILED;
}
EpdDrawError epd_hl_update_screen(EpdiyHighlevelState* hl, int mode, int temperature) {
  assert(hl == &highlevel && temperature == 25 && (mode == MODE_DU || mode == MODE_GL16));
  ++drawCalls;
  ++diffDraws;
  lastMode = mode;
  sourceFirst = previousFrame[0];
  if (drawOk) std::memcpy(previousFrame, nativeFrame, sizeof(previousFrame));
  return drawOk ? EPD_DRAW_SUCCESS : EPD_DRAW_FAILED;
}
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario(argv[1]);
  if (scenario == "init_failure") initOk = false;
  if (scenario == "geometry_failure") geometryOk = false;
  freeink::EpdBus bus;
  freeink::MetalioPlusDriver driver;
  assert(driver.usesExternalBus() && !driver.supportsAsyncDisplay());
  assert(!driver.grayscaleCapabilities().supported() && driver.beginGrayscale16() == nullptr);
  const auto geometry = driver.geometry();
  assert(geometry.width == 1216 && geometry.height == 684 && geometry.widthBytes == 152 &&
         geometry.bufferSize == 103968);
  assert(!driver.displayCommitted());
  driver.begin(bus);
  std::vector<uint8_t> frame(103968, 0xff);
  frame.front() = 0x81;
  frame.back() = 0x01;
  if (scenario == "modes" || scenario == "resync" || scenario == "sleep_recovery" || scenario == "diff_failure" ||
      scenario == "diff_power_failure" || scenario == "diff_off_failure") {
    // Any first request establishes a physical baseline, even a Fast request.
    driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Fast, true);
    assert(driver.displayCommitted() && clearCalls == 1 && fullDraws == 1 && diffDraws == 0);
    assert(previousFrame[0] == 0x0f);
    frame.front() = 0x42;
    if (scenario == "diff_failure") drawOk = false;
    if (scenario == "diff_power_failure") powerOk = false;
    if (scenario == "diff_off_failure") offOk = false;
    driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Fast, false);
    if (scenario == "diff_power_failure") {
      assert(!driver.displayCommitted() && clearCalls == 1 && drawCalls == 1);
    } else {
      // Catches the original unconditional-clear/full-frame fallback.
      assert(clearCalls == 1 && fullDraws == 1 && diffDraws == 1 && lastMode == MODE_DU);
      assert(sourceFirst == 0x0f);
    }
    if (scenario == "modes" || scenario == "resync" || scenario == "sleep_recovery") {
      assert(driver.displayCommitted() && previousFrame[0] == 0xf0);
      if (scenario == "resync") driver.requestResync(1);
      if (scenario == "sleep_recovery") driver.deepSleep(bus);
      frame.front() = 0x81;
      driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Half, true);
      if (scenario == "modes") {
        assert(clearCalls == 1 && fullDraws == 1 && diffDraws == 2 && lastMode == MODE_GL16);
        assert(sourceFirst == 0xf0 && previousFrame[0] == 0x0f);
        driver.displayWindow(bus, frame.data(), nullptr, 1, 1, 8, 8, true);
        assert(clearCalls == 1 && diffDraws == 3 && lastMode == MODE_DU);
        assert(!driver.displayStart(bus, frame.data(), nullptr, freeink::RefreshMode::Fast, true));
        assert(clearCalls == 1 && diffDraws == 4 && lastMode == MODE_DU);
        driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Full, true);
        assert(clearCalls == 2 && fullDraws == 2 && lastMode == MODE_GC16);
      } else {
        assert(clearCalls == 2 && fullDraws == 2 && diffDraws == 1 && lastMode == MODE_GC16);
      }
      assert(driver.displayCommitted());
    } else {
      assert(!driver.displayCommitted());
      if (scenario == "diff_failure") assert(previousFrame[0] == 0x0f);
      powerOk = drawOk = offOk = true;
      const int powers = powerCalls;
      driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Fast, true);
      if (scenario == "diff_off_failure") {
        assert(!driver.displayCommitted() && powerCalls == powers && clearCalls == 1);
      } else {
        assert(driver.displayCommitted() && clearCalls == 2 && fullDraws == 2 && lastMode == MODE_GC16);
      }
    }
    driver.deepSleep(bus);
    assert(!driver.displayCommitted());
    std::printf("Metalio Plus %s: passed\n", scenario.c_str());
    return 0;
  }
  if (scenario == "init_failure" || scenario == "geometry_failure") {
    driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Full, true);
    assert(!driver.displayCommitted() && drawCalls == 0 && powerCalls == 0 && offCalls > 0);
    driver.begin(bus);
    assert(initCalls == 1);  // vendor begin allocates and cannot be repeated
  } else {
    if (scenario == "power_failure") powerOk = false;
    if (scenario == "clear_failure") clearOk = false;
    if (scenario == "draw_failure") drawOk = false;
    if (scenario == "off_failure") offOk = false;
    if (scenario == "port_failure") portReadOk = false;
    driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Fast, false);
    const bool success = scenario == "success";
    assert(driver.displayCommitted() == success && offCalls >= 2);
    if (scenario == "power_failure") assert(clearCalls == 0 && drawCalls == 0);
    if (scenario == "clear_failure") assert(clearCalls == 1 && drawCalls == 0);
    if (scenario == "draw_failure") assert(drawCalls == 1);
    if (success) {
      const uint8_t first[] = {0x0f, 0, 0, 0xf0};
      const uint8_t last[] = {0, 0, 0, 0xf0};
      assert(std::memcmp(nativeFrame, first, 4) == 0);
      assert(nativeFrame[4] == 0xff && nativeFrame[1216 * 684 / 2 - 5] == 0xff);
      assert(std::memcmp(nativeFrame + 1216 * 684 / 2 - 4, last, 4) == 0);
      assert(!driver.displayStart(bus, frame.data(), nullptr, freeink::RefreshMode::Half, true));
      driver.displayWindow(bus, frame.data(), nullptr, 1, 1, 8, 8, true);
      assert(driver.displayCommitted() && drawCalls == 3 && clearCalls == 1);  // Half/window preserve the baseline
      driver.requestResync(1);
      assert(!driver.displayCommitted());
      driver.begin(bus);
      assert(initCalls == 1);
    } else {
      const int previousDraws = drawCalls, previousPowers = powerCalls;
      powerOk = clearOk = drawOk = offOk = portReadOk = true;
      driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Full, true);
      if (scenario == "off_failure" || scenario == "port_failure") {
        assert(!driver.displayCommitted() && drawCalls == previousDraws && powerCalls == previousPowers);
      } else {
        assert(driver.displayCommitted() && drawCalls == previousDraws + 1);
      }
    }
  }
  driver.deepSleep(bus);
  assert(!driver.displayCommitted());
  std::printf("Metalio Plus %s: passed\n", scenario.c_str());
}
