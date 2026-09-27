// 中文：epdiy LCD 输出路径的板级补齐与推帧实现。epdiy 本体在 src/epdiy/ 下原样
// 编译（LGPL-3.0-or-later）；这里只做三件事：把 EpdBoardDefinition 接到板子的
// 电源钩子上、把 1bpp 帧缓冲展开成 epdiy 的 4bpp、按档位调 epd_hl_update_screen。
//
// English: board glue and frame feeding for epdiy's LCD output path. epdiy itself
// is compiled verbatim under src/epdiy/ (LGPL-3.0-or-later); this file only wires
// the EpdBoardDefinition to the board's power hooks, expands a 1 bpp framebuffer
// into epdiy's 4 bpp layout, and calls epd_hl_update_screen per refresh profile.
//
// 冻结 / Frozen：
//   - 不改 epdiy 源码。/ Do not modify epdiy sources.
//   - set_vcom 是空实现：面板 VCOM 只由 PMU 出厂值决定，主机只读。
//     / set_vcom is a no-op: the panel VCOM is the PMU's factory value, read-only.

#include <EpdiyLcd.h>

#include <cstring>

#include <esp_heap_caps.h>

extern "C" {
// 未带 extern "C" 守卫的 epdiy 头（见本库 README 的清单）在这里统一包住；
// 已带守卫的再包一层是合法的。
// / epdiy headers without extern "C" guards are wrapped here; re-wrapping the
// guarded ones is harmless.
#include "epdiy/src/epd_board.h"
#include "epdiy/src/epd_display.h"
#include "epdiy/include/epd_waveform.h"
#include "epdiy/src/epdiy.h"
#include "epdiy/src/epd_highlevel.h"
#include "epdiy/include/epd_lcd.h"
#include "e0470/include/e0470_epaper_waveform.h"
}

namespace freeink {
namespace {

const EpdiyLcdConfig* g_cfg = nullptr;
EpdiyHighlevelState g_hl = {};
uint8_t* g_fb4 = nullptr;
// 最近一次推上去的黑白页（1bpp）。AA 的灰度提交需要它当底图，因为那时调用方的缓冲
// 装的是选择平面而不是页面。/ The last B/W page pushed (1 bpp). The AA gray commit
// needs it as a base because the caller's buffer then holds a selector plane.
uint8_t* g_base = nullptr;
bool g_started = false;
// 调用方的 1bpp 位约定，由 epdiyLcdBegin 给定一次。
// / The caller's 1 bpp bit convention, fixed once by epdiyLcdBegin.
bool g_blackIsOne = false;

// 1bpp 行 → 4bpp 行的展开表：一个输入字节出 4 个输出字节。
// epdiy 是每字节两个像素，偶数列在低半字节、奇数列在高半字节，15 = 白。
// / Expansion table: one 1 bpp input byte becomes four 4 bpp bytes. epdiy packs
// two pixels per byte, even column in the low nibble, odd in the high, 15 = white.
uint32_t g_expand[2][256];

void buildExpandTable() {
  // index 0: 置位 = 黑 (level 0)，清零 = 白 (level 15)
  // index 1: 置位 = 白 (level 15)，清零 = 黑 (level 0)
  for (int oneIsBlack = 0; oneIsBlack < 2; ++oneIsBlack) {
    for (int b = 0; b < 256; ++b) {
      uint8_t out[4];
      for (int pair = 0; pair < 4; ++pair) {
        uint8_t lvl[2];
        for (int k = 0; k < 2; ++k) {
          const int bit = (b >> (7 - (pair * 2 + k))) & 1;
          const int black = oneIsBlack ? bit : (bit ^ 1);
          lvl[k] = static_cast<uint8_t>(black ? 0 : 15);
        }
        out[pair] = static_cast<uint8_t>(lvl[0] | (lvl[1] << 4));
      }
      g_expand[oneIsBlack][b] = static_cast<uint32_t>(out[0]) | (static_cast<uint32_t>(out[1]) << 8) |
                                (static_cast<uint32_t>(out[2]) << 16) | (static_cast<uint32_t>(out[3]) << 24);
    }
  }
}

float panelTemperature() {
  if (g_cfg != nullptr && g_cfg->power.getTemperature != nullptr) {
    const float t = g_cfg->power.getTemperature();
    if (t >= 0.0f && t <= 50.0f) return t;
  }
  // 波形只有一个 0–50 °C 档，所以选表结果与具体值无关。
  // / The table has a single 0–50 °C range, so the exact value is irrelevant.
  return 20.0f;
}

// --- EpdBoardDefinition -----------------------------------------------------

void boardInit(uint32_t epdRowWidth) {
  (void)epdRowWidth;  // 行宽由 lcd_bus_config_t + 面板宽度决定 / from the bus config
  if (g_cfg == nullptr) return;

  // 引脚先归到安全电平（厂商 board_init 的顺序：pinMode 全部先做，再让 LCD 外设接管）。
  // / Park the pins first, exactly like the reference board_init(), then hand them
  // to the LCD peripheral.
  if (g_cfg->power.prepare != nullptr) (void)g_cfg->power.prepare();

  LcdEpdConfig_t c = {};
  c.pixel_clock = static_cast<size_t>(g_cfg->pclkMhz) * 1000u * 1000u;
  c.line.le_high_time = g_cfg->line.leHighTime;
  c.line.line_front_porch = g_cfg->line.lineFrontPorch;
  c.line.line_end = g_cfg->line.lineEnd;
  c.line.ckv_high_time = g_cfg->line.ckvHighTime01us;
  c.bus_width = g_cfg->busWidth;
  for (int i = 0; i < 16; ++i) c.bus.data[i] = static_cast<gpio_num_t>(g_cfg->dataPins[i]);
  c.bus.clock = static_cast<gpio_num_t>(g_cfg->pinClock);
  c.bus.ckv = static_cast<gpio_num_t>(g_cfg->pinCkv);
  c.bus.start_pulse = static_cast<gpio_num_t>(g_cfg->pinStartPulse);
  c.bus.leh = static_cast<gpio_num_t>(g_cfg->pinLeh);
  c.bus.stv = static_cast<gpio_num_t>(g_cfg->pinStv);

  epd_lcd_init(&c, static_cast<int>(epd_width()), static_cast<int>(epd_height()));
  epd_lcd_set_prefill_lines(g_cfg->prefillLines);
}

void boardDeinit() {}

// XOE 与 MODE 在 FCA9555 上，由 powerOn/powerOff 按顺序持有；本板没有 epdiy 假设的
// 那个通用控制寄存器，所以这里是空实现。
// / XOE and MODE live on the FCA9555 and are sequenced by powerOn/powerOff. This
// board has no equivalent of epdiy's generic control register, so this is a no-op.
void boardSetCtrl(epd_ctrl_state_t* state, const epd_ctrl_state_t* const mask) {
  (void)state;
  (void)mask;
}

void boardPowerOn(epd_ctrl_state_t* state) {
  (void)state;
  if (g_cfg != nullptr && g_cfg->power.powerOn != nullptr) (void)g_cfg->power.powerOn();
}

void boardPowerOff(epd_ctrl_state_t* state) {
  (void)state;
  if (g_cfg != nullptr && g_cfg->power.powerOff != nullptr) g_cfg->power.powerOff();
}

void boardMeasureVcom(epd_ctrl_state_t* state) { (void)state; }

// 硬规则：面板 VCOM 是出厂写进 PMU 的，与这块玻璃配对，写错会永久损坏面板。
// epdiy 的 v6/v7 板才会实现这个回调；本板必须保持空实现。
// / HARD RULE: the panel VCOM is factory-written in the PMU and paired with this
// glass; a wrong value damages it permanently. Only epdiy's v6/v7 boards implement
// this callback — on this board it must stay a no-op.
void boardSetVcom(int vcomMv) { (void)vcomMv; }

float boardGetTemperature() { return panelTemperature(); }

const EpdBoardDefinition kBoard = {
    /* init            */ boardInit,
    /* deinit          */ boardDeinit,
    /* set_ctrl        */ boardSetCtrl,
    /* poweron         */ boardPowerOn,
    /* measure_vcom    */ boardMeasureVcom,
    /* poweroff        */ boardPowerOff,
    /* set_vcom        */ boardSetVcom,
    /* get_temperature */ boardGetTemperature,
    /* gpio_set_direction */ nullptr,
    /* gpio_read          */ nullptr,
    /* gpio_write         */ nullptr,
};

}  // namespace

bool epdiyLcdBegin(const EpdiyLcdConfig& cfg, uint16_t width, uint16_t height, bool blackIsOne) {
  if (g_started) return g_fb4 != nullptr;
  g_cfg = &cfg;
  g_blackIsOne = blackIsOne;

  // 波形必须在 epd_hl_init 之前建好：E0470_WAVEFORM 的相位数据由它填。
  // / The waveform must be built before epd_hl_init(): it fills E0470_WAVEFORM.
  e0470_waveform_init();

  epd_set_board(&kBoard);
  epd_init(&kBoard, &E0470_DISPLAY, EPD_OPTIONS_DEFAULT);

  // 几何校验必须在 epd_init 之后：epd_width()/epd_height() 读的是 epd_init 里
  // `display = disp` 设进去的那张表，在此之前 esp_get_display() 还是 NULL。
  // epdiy 用它自己的 epd_width()/epd_height()（E0470_DISPLAY = 1216x684）扫描，
  // 与 BoardProfile 的几何必须一致，否则扫描与帧缓冲会错位。
  // / The geometry check MUST come after epd_init: epd_width()/epd_height() read the
  // display table that epd_init assigns, and before it epd_get_display() is NULL.
  // epdiy scans using its own epd_width()/epd_height() (E0470_DISPLAY is 1216x684);
  // it must agree with the BoardProfile geometry or scan and framebuffer disagree.
  if (epd_width() != width || epd_height() != height) return false;

  g_hl = epd_hl_init(&E0470_WAVEFORM);
  g_fb4 = epd_hl_get_framebuffer(&g_hl);
  if (g_fb4 == nullptr) return false;

  // 1bpp 底图，宽/8 字节每行。/ 1 bpp base image, width/8 bytes per row.
  const size_t baseBytes = static_cast<size_t>(width) / 8 * static_cast<size_t>(height);
  g_base = static_cast<uint8_t*>(heap_caps_malloc(baseBytes, MALLOC_CAP_SPIRAM));
  if (g_base == nullptr) return false;
  memset(g_base, 0xFF, baseBytes);  // 起始为白纸 / starts as white paper

  buildExpandTable();

  // 开机全局刷新：把面板驱动到一个确定的白色状态。
  //
  // e-ink 会保留上一次的画面，而且那幅图很可能是被复位/掉电打断的中间态；epdiy 的
  // 差分刷新却假定基线是白的（epd_hl_init 的帧缓冲全 0，而在本玻璃上 level 0 就是
  // 白）。两者对不上时，每一帧差分都做在错的基线上，旧画面就一直残留。所以上电后
  // 先无条件全屏清一次，再用白场同步 epdiy 的帧缓冲和底图，之后第一帧差分才是对的。
  //
  // / Boot-time global refresh: drive the panel to a known white state.
  //
  // An e-ink panel keeps its previous image, and that image is likely an interrupted
  // mid-waveform state, while epdiy's differential refresh assumes a white baseline
  // (epd_hl_init's framebuffer is all zero, and level 0 is white on this glass).
  // While the two disagree, every diff runs against the wrong baseline and the old
  // picture ghosts through. So clear the whole panel unconditionally after bring-up,
  // then sync epdiy's framebuffer and our base image to white so the first real frame
  // diffs correctly.
  epd_poweron();
  epd_clear();
  epd_poweroff();

  const size_t bufBytes = static_cast<size_t>(width) / 2 * static_cast<size_t>(height);
  // 白场按调用方的位约定展开，极性翻转时不会写错。
  // / The white field is expanded with the caller's bit convention, so it stays
  // correct if the polarity is ever flipped.
  const uint32_t whiteWord = g_expand[blackIsOne ? 0 : 1][0xFF];
  uint32_t* words = reinterpret_cast<uint32_t*>(g_fb4);
  for (size_t i = 0; i < bufBytes / 4; ++i) words[i] = whiteWord;
  memset(g_base, 0xFF, baseBytes);  // facade 约定：置位 = 白 / facade: a set bit is white

  g_started = true;
  return true;
}

void epdiyLcdEnd() {
  if (!g_started) return;
  // epdiy 没有 epd_hl_deinit()（highlevel.c 只提供 init/get_framebuffer/update_*），
  // 所以那个 4bpp 帧缓冲留在 PSRAM 里不回收：它只有一个，且只在本板分配一次。
  // / epdiy provides no epd_hl_deinit() (highlevel.c only has init/get_framebuffer/
  // update_*), so the 4 bpp framebuffer stays allocated in PSRAM: there is exactly
  // one and it is allocated once per boot on this board.
  epd_lcd_deinit();
  g_fb4 = nullptr;
  g_hl = {};
  g_started = false;
}

namespace {

enum EpdDrawMode drawModeFor(EpdiyLcdRefresh mode) {
  switch (mode) {
    case EpdiyLcdRefresh::Full:
      return static_cast<enum EpdDrawMode>(MODE_GC16 | PREVIOUSLY_WHITE);
    case EpdiyLcdRefresh::Half:
      return static_cast<enum EpdDrawMode>(MODE_GL16 | PREVIOUSLY_WHITE);
    case EpdiyLcdRefresh::Fast:
    default:
      return static_cast<enum EpdDrawMode>(MODE_DU | PREVIOUSLY_WHITE);
  }
}

// 1bpp 页 -> epdiy 4bpp。展开表把调用方的位约定翻成 epdiy 的灰度级；本玻璃实测
// level 0 呈白、15 呈黑，所以 blackIsOne 的语义见 EpdiyLcd.h 的说明。
// / 1 bpp page -> epdiy 4 bpp. The table translates the caller's bit convention into
// epdiy gray levels. On this glass level 0 renders white and 15 renders black; see
// EpdiyLcd.h for what blackIsOne means.
void fillFrom1bpp(const uint8_t* fb) {
  const uint16_t w = static_cast<uint16_t>(epd_width());
  const uint16_t h = static_cast<uint16_t>(epd_height());
  const size_t srcStride = w / 8;
  const size_t dstStride = w / 2;
  const uint32_t* table = g_expand[g_blackIsOne ? 0 : 1];
  for (uint16_t y = 0; y < h; ++y) {
    const uint8_t* src = fb + static_cast<size_t>(y) * srcStride;
    uint32_t* dst = reinterpret_cast<uint32_t*>(g_fb4 + static_cast<size_t>(y) * dstStride);
    for (size_t i = 0; i < srcStride; ++i) dst[i] = table[src[i]];
  }
}

void pushFrame(EpdiyLcdRefresh mode, bool turnOff) {
  epd_poweron();
  (void)epd_hl_update_screen(&g_hl, drawModeFor(mode), static_cast<int>(panelTemperature()));
  if (turnOff) epd_poweroff();
}

}  // namespace

void epdiyLcdDraw(const uint8_t* fb, EpdiyLcdRefresh mode, bool turnOff) {
  if (!g_started || fb == nullptr || g_fb4 == nullptr || g_cfg == nullptr) return;

  // 留一份底图：AA 的 displayGray() 提交时调用方的缓冲已经变成选择平面了。
  // / Keep a base copy: by the time the AA displayGray() commit runs, the caller's
  // buffer has become a selector plane.
  const size_t bytes = static_cast<size_t>(epd_width()) / 8 * static_cast<size_t>(epd_height());
  if (g_base != nullptr) memcpy(g_base, fb, bytes);

  fillFrom1bpp(fb);
  pushFrame(mode, turnOff);
}

void epdiyLcdDrawGray(const uint8_t* lsb, const uint8_t* msb, EpdiyLcdRefresh mode, bool turnOff) {
  if (!g_started || g_fb4 == nullptr || g_cfg == nullptr) return;
  if (g_base == nullptr || lsb == nullptr || msb == nullptr) return;

  // 局部平均重建：1bpp 页面里本来就编码了灰阶——文字靠覆盖率阈值，图片靠抖动图案。
  // 对 3x3 邻域求墨占比，就能把抖动图案还原成连续灰阶，同时把文字边缘磨成中间灰，
  // 一个机制同时管住抗锯齿和图片。两个选择平面给出宿主明确的每像素意图，用它主导、
  // 邻域微调；没有标记的像素（纯黑/纯白/抖动图案）完全交给邻域。
  //
  // / Local-average reconstruction. The 1 bpp page already encodes tone: text through
  // its thresholded coverage, images through their dither pattern. Averaging ink over
  // a 3x3 neighbourhood recovers dithered images as continuous tone and softens text
  // edges -- one mechanism for both anti-aliasing and images. The two selector planes
  // carry the host's explicit per-pixel intent, which leads with the neighbourhood as
  // a refinement; unmarked pixels (solid black/white, dither) come from the
  // neighbourhood alone.
  //
  // 极性锚在展开表上，不靠文档推理：`g_expand[g_blackIsOne ? 0 : 1]` 把"位=1"送到
  // level 15，而调用方的 1bpp 约定是「位清零 = 墨、位置一 = 纸」
  // （GfxRenderer::drawPixel，GfxRenderer.cpp:635-640：置位走 |=，即白）。BW 页面在真机上
  // 正确，所以这张玻璃上 level 15 = 白、0 = 黑，与 epdiy.h:93 一致。下面两个极值直接由
  // g_blackIsOne 推出，灰度提交因此不可能和它所叠加的页面唱反调。
  // / Polarity is anchored to the expansion table rather than argued from a header:
  // `g_expand[g_blackIsOne ? 0 : 1]` sends bit=1 to level 15, and the caller's 1 bpp
  // convention is "clear bit = ink, set bit = paper" (GfxRenderer::drawPixel,
  // GfxRenderer.cpp:635-640 -- the set path is |=, i.e. white). The B/W page is correct
  // on hardware, so on this glass level 15 = white and 0 = black, matching epdiy.h:93.
  // The extremes below come from g_blackIsOne, so the grey commit cannot disagree with
  // the page it overlays.
  //
  // 旧实现的两处错误，记下来避免再犯 / Two past mistakes, recorded so they are not
  // repeated:
  //   * 把"位=1"当成墨，于是整个灰阶提交与底图整体反相。
  //     / treating bit=1 as ink, which inverted the whole grey commit against the base;
  //   * light/dark 两档接反（2-bit 值 1 是深灰，却给了更亮的 level）。
  //     / swapping the light/dark slots (2-bit value 1 is dark grey but got the
  //       brighter level).

  // 定点：f 是"墨占比"，0 = 全白、255 = 全黑。
  // / Fixed point: f is the ink fraction, 0 = all white, 255 = all black.
  constexpr int kInkDark = 178;    // 2-bit 值 1（深灰）≈ 0.70 墨
  constexpr int kInkLight = 89;    // 2-bit 值 2（浅灰）≈ 0.35 墨
  constexpr int kIntentWeight = 166;  // 平面意图占 0.65，邻域占 0.35
  // 中心权重：无权重盒式平均会造出 3 像素宽的灰过渡，字干只有 1-2 像素时整笔就被
  // 糊掉。中心给 24、八邻各给 1（总权重 32），过渡只有 1 像素且中心压倒性主导，
  // 笔画内部与背景基本保持纯黑纯白。实测 8 偏柔、16 仍嫌糊，24 是当前取值 —— 这块
  // 板 PPI 很高，单靠二值渲染就已经很锐利，抗锯齿只该作为极轻的修饰存在。
  // / Centre weight: an unweighted box filter produces a 3-px-wide grey ramp, which
  // smears a 1-2 px stem into mush. Centre 24, each of the eight neighbours 1 (total
  // 32) keeps the ramp one pixel wide with the centre overwhelmingly dominant, so
  // stroke interiors and background stay essentially black/white. 8 read soft and 16
  // still looked blurred on hardware; 24 is the current value. This panel has a high
  // enough pixel density that plain binary rendering is already crisp, so anti-aliasing
  // should only ever be a very light touch.
  constexpr int kCentreWeight = 24;
  // 两个极值来自判墨方向，不由常量硬编码 / The two extremes follow from g_blackIsOne.
  const int inkLevel = g_blackIsOne ? 0 : 15;
  const int paperLevel = 15 - inkLevel;

  const int w = static_cast<int>(epd_width());
  const int h = static_cast<int>(epd_height());
  const int stride = w / 8;

  for (int y = 0; y < h; ++y) {
    const uint8_t* lrow = lsb + static_cast<size_t>(y) * stride;
    const uint8_t* mrow = msb + static_cast<size_t>(y) * stride;
    uint8_t* drow = g_fb4 + static_cast<size_t>(y) * (w / 2);

    for (int x = 0; x < w; ++x) {
      // --- 3x3 墨占比 ------------------------------------------------------
      // 中心加权的 3x3：权重和决定过渡宽度，而不是邻域大小。
      // / Centre-weighted 3x3: the weights, not the window size, set the ramp width.
      int ink = 0, wsum = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        const int yy = y + dy;
        if (yy < 0 || yy >= h) continue;
        const uint8_t* brow = g_base + static_cast<size_t>(yy) * stride;
        for (int dx = -1; dx <= 1; ++dx) {
          const int xx = x + dx;
          if (xx < 0 || xx >= w) continue;
          const int wgt = (dx == 0 && dy == 0) ? kCentreWeight : 1;
          const bool bit = (brow[xx >> 3] & (0x80u >> (xx & 7))) != 0;
          if (!bit) ink += wgt;  // 位清零 = 墨 / clear bit = ink
          wsum += wgt;
        }
      }
      int f = wsum > 0 ? (ink * 255) / wsum : 0;

      // --- 宿主的每像素意图 ------------------------------------------------
      const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
      const bool lb = (lrow[x >> 3] & mask) != 0;
      const bool mb = (mrow[x >> 3] & mask) != 0;
      if (lb || mb) {
        const int intent = lb ? kInkDark : kInkLight;
        f = (kIntentWeight * intent + (255 - kIntentWeight) * f) / 255;
      }

      // --- 墨占比 -> level（0 = 黑，15 = 白）-------------------------------
      int level = (f * inkLevel + (255 - f) * paperLevel + 127) / 255;
      if (level < 0) level = 0;
      if (level > 15) level = 15;

      // epdiy 每字节两个像素：偶数列低半字节、奇数列高半字节。
      // / epdiy packs two pixels per byte: even column low nibble, odd high.
      uint8_t* cell = drow + (x >> 1);
      if ((x & 1) != 0) {
        *cell = static_cast<uint8_t>((*cell & 0x0Fu) | static_cast<uint8_t>(level << 4));
      } else {
        *cell = static_cast<uint8_t>((*cell & 0xF0u) | static_cast<uint8_t>(level));
      }
    }
  }

  pushFrame(mode, turnOff);
}

void epdiyLcdDeepSleep() {
  if (!g_started) return;
  (void)epd_poweroff();
  epdiyLcdEnd();
}

bool epdiyLcdReady() { return g_started && g_fb4 != nullptr; }

}  // namespace freeink
