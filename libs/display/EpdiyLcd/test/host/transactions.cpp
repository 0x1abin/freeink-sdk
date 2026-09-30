#include <EpdiyLcd.h>
#include <EpdiyLcdDriver.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
extern "C" {
#include "e0470_epaper_waveform.h"
#include "epd_lcd.h"
#include "epdiy.h"
void check_queue(void);
}

namespace {
bool railsOk = true;
bool powered = false;
bool failDraw = false;
int draws = 0, clears = 0;
int lastMode = 0;
const EpdBoardDefinition* board = nullptr;
EpdiyHighlevelState* state = nullptr;
bool powerOn() {
  powered = railsOk;
  return powered;
}
void powerOff() { powered = false; }
}  // namespace

namespace freeink {
const EpdiyLcdConfig& testConfig() {
  static EpdiyLcdConfig cfg{};
  cfg.power.powerOn = powerOn;
  cfg.power.powerOff = powerOff;
  return cfg;
}
}  // namespace freeink
void delay(unsigned long) {}
extern "C" {
int allocation_calls = 0, fail_allocation = 0;
const EpdWaveform E0470_WAVEFORM{};
const EpdDisplay_t E0470_DISPLAY{16, 2, 16, 18, &E0470_WAVEFORM};
void e0470_waveform_init() {}
void epd_set_board(const EpdBoardDefinition* b) { board = b; }
const EpdDisplay_t* epd_get_display() { return &E0470_DISPLAY; }
int epd_width() { return 16; }
int epd_height() { return 2; }
EpdRect epd_full_screen() { return {0, 0, 16, 2}; }
enum EpdRotation epd_get_rotation() { return EPD_ROT_LANDSCAPE; }
bool epd_init(const EpdBoardDefinition* b, const EpdDisplay_t*, enum EpdInitOptions) {
  board = b;
  return board->init(16);
}
void epd_deinit() {
  board->poweroff(nullptr);
  board->deinit();
}
void epd_poweron() { board->poweron(nullptr); }
void epd_poweroff() { board->poweroff(nullptr); }
void epd_clear() {
  assert(powered);
  ++clears;
}
esp_err_t epd_lcd_init(const LcdEpdConfig_t*, int, int) { return ESP_OK; }
void epd_lcd_set_prefill_lines(int) {}
void epd_lcd_deinit() {}
void epd_leading_skip_discard() {}
void epd_difference_column_range(EpdRect area, int* first, int* end) {
  *first = area.x;
  *end = area.x + area.width;
}
EpdRect epd_difference_image_cropped(const uint8_t* to, const uint8_t* from, EpdRect area, uint8_t*, bool* lines,
                                     uint8_t* columns) {
  bool changed = false;
  for (int y = 0; y < 2; ++y) {
    lines[y] = std::memcmp(to + y * 8, from + y * 8, 8) != 0;
    changed |= lines[y];
  }
  std::memset(columns, changed ? 0xFF : 0, 8);
  return changed ? area : EpdRect{0, 0, 0, 0};
}
enum EpdDrawError epd_draw_base(EpdRect, const uint8_t*, EpdRect, enum EpdDrawMode mode, int, const bool*,
                                const uint8_t*, const EpdWaveform*) {
  assert(powered);
  ++draws;
  lastMode = mode & 0x3F;
  return failDraw ? EPD_DRAW_EMPTY_LINE_QUEUE : EPD_DRAW_SUCCESS;
}
}

void checkHighlevel() {
  // Exercise both real highlevel copy paths without the wrapper's recovery.
  auto hl = epd_hl_init(&E0470_WAVEFORM);
  state = &hl;
  std::memset(hl.front_fb, 0, 16);
  uint8_t baseline[16];
  std::memcpy(baseline, hl.back_fb, 16);
  powered = true;
  failDraw = true;
  assert(epd_hl_update_screen(state, MODE_GL16, 20) != EPD_DRAW_SUCCESS);
  assert(std::memcmp(baseline, hl.back_fb, 16) == 0);
  assert(epd_hl_update_screen_full(state, MODE_GC16, 20) != EPD_DRAW_SUCCESS);
  assert(std::memcmp(baseline, hl.back_fb, 16) == 0);
  failDraw = false;
  assert(epd_hl_update_screen(state, MODE_GL16, 20) == EPD_DRAW_SUCCESS);
  assert(std::memcmp(hl.front_fb, hl.back_fb, 16) == 0);
}

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "highlevel") == 0) {
    checkHighlevel();
    return 0;
  }
  if (argc > 2 && std::strcmp(argv[1], "oom") == 0) {
    fail_allocation = std::atoi(argv[2]);
    freeink::EpdBus bus;
    freeink::EpdiyLcdDriver driver(freeink::testConfig());
    driver.begin(bus);
    assert(!freeink::epdiyLcdReady());
    fail_allocation = 0;
    driver.begin(bus);
    assert(freeink::epdiyLcdReady());
    driver.deepSleep(bus);
    assert(!freeink::epdiyLcdReady());
    driver.begin(bus);
    assert(freeink::epdiyLcdReady());
    return 0;
  }
  if (argc > 1) {
    railsOk = false;
    assert(!freeink::epdiyLcdBegin(freeink::testConfig(), 16, 2, true));
    assert(clears == 0 && draws == 0 && !powered);
    return 0;
  }
  freeink::EpdBus bus;
  freeink::EpdiyLcdDriver driver(freeink::testConfig());
  driver.begin(bus);
  uint8_t fb[4] = {0x55, 0xAA, 0x55, 0xAA}, gray[4]{};
  for (const auto mode : {freeink::RefreshMode::Full, freeink::RefreshMode::Fast, freeink::RefreshMode::Half}) {
    driver.displayGrayscaleBaseWithContext(bus, fb, mode, false, freeink::RefreshContext::Normal);
    driver.copyGrayscaleLsb(bus, gray);
    driver.copyGrayscaleMsb(bus, gray);
    driver.displayGray(bus, gray, true, nullptr, false);
    assert(lastMode == (mode == freeink::RefreshMode::Full ? MODE_GC16 : MODE_GL16));
    fb[0] ^= 1;
  }
  const int before = draws;
  assert(freeink::epdiyLcdDraw(fb, freeink::EpdiyLcdRefresh::Full, true));
  assert(freeink::epdiyLcdDraw(fb, freeink::EpdiyLcdRefresh::Full, true));
  assert(draws == before + 2);  // identical FULL must still drive the panel
  railsOk = false;
  assert(!freeink::epdiyLcdDraw(fb, freeink::EpdiyLcdRefresh::Fast, true));
  assert(draws == before + 2 && !powered);
  railsOk = true;
  failDraw = true;
  fb[0] ^= 1;
  assert(!freeink::epdiyLcdDraw(fb, freeink::EpdiyLcdRefresh::Fast, true));
  failDraw = false;
  const int failed = draws, dirtyClears = clears;
  assert(freeink::epdiyLcdDraw(fb, freeink::EpdiyLcdRefresh::Fast, true));
  assert(draws == failed + 1 && clears == dirtyClears + 1 && lastMode == MODE_GC16);

  check_queue();
}
