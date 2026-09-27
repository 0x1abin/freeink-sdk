// 中文：epdiy LCD 路径的 PanelDriver 适配。所有面板时序、LUT/波形与逐行供数都在
// EpdiyLcd 库里（epdiy 本体原样编译）；这里只把 SDK 的 1bpp 帧缓冲与刷新档位翻译
// 成它的调用。
//
// English: PanelDriver adapter for the epdiy LCD path. All panel timing, LUTs and
// line feeding live in the EpdiyLcd library (where epdiy itself is compiled
// verbatim); this file only translates the SDK's 1 bpp framebuffer and refresh
// profiles into its calls.

#include "EpdiyLcdDriver.h"

#include <BoardConfig.h>

#include <cstring>

#include <esp_heap_caps.h>

#if FREEINK_DRIVER_EPDIY_LCD

#ifndef FREEINK_EPDIY_LCD_CONFIG
#error \
    "FREEINK_DRIVER_EPDIY_LCD requires a board config: define `const EpdiyLcdConfig& yourConfig();` in namespace freeink and build with -DFREEINK_EPDIY_LCD_CONFIG=yourConfig"
#endif

namespace freeink {

// 板子提供的配置函数，名字由 -DFREEINK_EPDIY_LCD_CONFIG 注入——与 LgfxEpd 的
// FREEINK_LGFX_EPD_CONFIG 同一套路，SDK 因此不需要 include 任何板子头文件。
// / Board-supplied config function, name injected by -DFREEINK_EPDIY_LCD_CONFIG —
// the same pattern as LgfxEpd's FREEINK_LGFX_EPD_CONFIG, so the SDK never includes
// a board header.
const EpdiyLcdConfig& FREEINK_EPDIY_LCD_CONFIG();

namespace {

// 帧缓冲极性。facade 的约定是「置位 = 白」（FreeInkDisplay.cpp:263-264：1bpp、
// MSB 在前、1 = 白），这里却告诉 EpdiyLcd「置位 = 黑」，也就是展开表取 index 0，
// 于是 facade 的白被翻成 epdiy 的 level 0。**这块玻璃上 level 0 呈现为白**，两处
// 相反的电平约定正好抵消，画面才是对的。
//
// 别把这个常量当成"可以按文档改"的东西：它只是抵消项，不是 epdiy 文档意义上的
// 极性。改动记录——本轮曾在"旧画面残留"之后把它翻成 false，结果整屏反色；退回
// true 后恢复。那次"残留"会让人误判明暗，所以判断极性一定要在一次干净的全屏刷新
// 之后看（epdiyLcdBegin 里的开机全局刷新提供了这个基准）。
//
// / Framebuffer polarity. The facade's contract is "a set bit is white"
// (FreeInkDisplay.cpp:263-264), yet this tells EpdiyLcd "a set bit is black", i.e.
// expansion-table index 0, so a facade white becomes epdiy level 0. On THIS glass
// level 0 renders white, so the two opposite conventions cancel and the picture is
// right.
//
// Do not treat this constant as a documentation question — it is a cancellation
// term, not a polarity in epdiy's sense. Change log: it was briefly flipped to
// false after an "old image residue" report and the screen came out fully
// inverted; reverting to true restored it. Residue makes brightness judgments
// unreliable, so always judge polarity after a clean full-screen refresh (the
// boot-time global refresh in epdiyLcdBegin provides that baseline).
constexpr bool kBlackIsOne = true;

}  // namespace

EpdiyLcdDriver::EpdiyLcdDriver(const EpdiyLcdConfig& cfg) : _cfg(cfg) {}

PanelGeometry EpdiyLcdDriver::geometry() const {
  const uint16_t w = BoardConfig::ACTIVE.displayWidth;
  const uint16_t h = BoardConfig::ACTIVE.displayHeight;
  const uint16_t wb = w / 8;
  return {w, h, wb, static_cast<uint32_t>(wb) * h};
}

GrayscaleCapabilities EpdiyLcdDriver::grayscaleCapabilities(GrayscaleMode mode) const {
  (void)mode;
  // OverlayMasks: plane background 0 = black/white (taken from the B/W base the
  // host pushes first), LSB set = dark, MSB set = light. base is Separate because
  // display() pushes the B/W frame before the grey commit overlays it; stripUploads
  // stays false so the host uses the whole-plane LSB/MSB path this driver implements.
  return {GrayscaleEncoding::OverlayMasks, GrayscaleBase::Separate, false, false, false};
}

void EpdiyLcdDriver::begin(EpdBus& bus) {
  (void)bus;
  _ready = epdiyLcdBegin(_cfg, BoardConfig::ACTIVE.displayWidth, BoardConfig::ACTIVE.displayHeight, kBlackIsOne);
  if (!_ready) return;

  // 两个选择平面：只有宿主真的做抗锯齿时才会被写入，但分配是无条件的，因为它们
  // 必须在 copyGrayscaleLsb() 第一次被调用之前就位。
  // / The two selector planes: only written when the host actually anti-aliases, but
  // allocated unconditionally because they must exist before the first
  // copyGrayscaleLsb() call.
  const PanelGeometry g = geometry();
  if (_lsb == nullptr) _lsb = static_cast<uint8_t*>(heap_caps_malloc(g.bufferSize, MALLOC_CAP_SPIRAM));
  if (_msb == nullptr) _msb = static_cast<uint8_t*>(heap_caps_malloc(g.bufferSize, MALLOC_CAP_SPIRAM));
  if (_lsb == nullptr || _msb == nullptr) {
    _ready = false;
    return;
  }
  memset(_lsb, 0, g.bufferSize);
  memset(_msb, 0, g.bufferSize);
}

void EpdiyLcdDriver::deepSleep(EpdBus& bus) {
  (void)bus;
  epdiyLcdDeepSleep();
  _ready = false;
}

void EpdiyLcdDriver::display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) {
  (void)bus;
  (void)prev;  // epdiy's highlevel keeps its own previous frame / epdiy 自己记上一帧
  if (!_ready) return;

  EpdiyLcdRefresh refresh;
  switch (mode) {
    case RefreshMode::Full:
      refresh = EpdiyLcdRefresh::Full;  // GC16
      break;
    case RefreshMode::Half:
      refresh = EpdiyLcdRefresh::Half;  // GL16
      break;
    case RefreshMode::Fast:
    default:
      refresh = EpdiyLcdRefresh::Fast;  // DU
      break;
  }

  // 帧缓冲极性见文件顶部的 kBlackIsOne。/ Framebuffer polarity: see kBlackIsOne above.
  _lastBaseMode = refresh;
  epdiyLcdDraw(fb, refresh, turnOff);
}

void EpdiyLcdDriver::copyGrayscaleLsb(EpdBus& bus, const uint8_t* lsb) {
  (void)bus;
  if (_lsb == nullptr || lsb == nullptr) return;
  memcpy(_lsb, lsb, geometry().bufferSize);
}

void EpdiyLcdDriver::copyGrayscaleMsb(EpdBus& bus, const uint8_t* msb) {
  (void)bus;
  if (_msb == nullptr || msb == nullptr) return;
  memcpy(_msb, msb, geometry().bufferSize);
}

void EpdiyLcdDriver::displayGray(EpdBus& bus, const uint8_t* fb, bool turnOff, const unsigned char* lut,
                                 bool factoryMode) {
  (void)bus;
  (void)lut;
  (void)factoryMode;
  if (!_ready || _lsb == nullptr || _msb == nullptr) return;

  // `fb` 故意不用：抗锯齿提交时它装的是最后一个选择平面（与页面互补），推上去就是
  // 整屏负片。底图由 EpdiyLcd 在 display() 时留存。
  // / `fb` is deliberately unused: at anti-aliasing commit time it holds the last
  // selector plane (the page's complement), which renders as a screen-wide negative.
  // EpdiyLcd kept the base image during display().
  (void)fb;

  epdiyLcdDrawGray(_lsb, _msb, _lastBaseMode, turnOff);
}

PanelDriver& epdiyLcdDriver() {
  static EpdiyLcdDriver driver(FREEINK_EPDIY_LCD_CONFIG());
  return driver;
}

}  // namespace freeink

#endif  // FREEINK_DRIVER_EPDIY_LCD
