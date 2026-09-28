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

  // 两件事各司其职，不要再拿一个替代另一个：
  //
  //   1) 覆盖率给"这一像素属于哪一档"——底图给黑白，两个选择平面给两档中间灰。
  //      这是宿主真正的 2-bit 抗锯齿数据，并口 16 级屏直接吃下去即可。
  //   2) 邻域只做**有界微调**。4 档表达不了一条斜边，所以纯覆盖率渲染必然出锯齿；
  //      而上一版让邻域**替代**覆盖率，又整体糊掉。把邻域限制在 ±kMaxNudge 级之内，
  //      斜边就得到 4 档之间的过渡（圆润），笔画却不会被抬亮（不糊）。
  //
  // / Two jobs, and neither may replace the other. Coverage decides which step a pixel
  // belongs to: the base page gives black/white and the two selector planes give the
  // two mid tones -- the host's real 2-bit anti-aliasing data, which a 16-level
  // parallel panel takes directly. The neighbourhood only applies a BOUNDED nudge:
  // four steps cannot describe a diagonal, so coverage alone always shows jaggies,
  // while an earlier revision let the neighbourhood REPLACE coverage and blurred
  // everything. Capping it at +/-kMaxNudge levels gives diagonals their in-between
  // tones without lightening strokes.
  //
  // 极性：本文件不做极性推理，沿用 kBlackIsOne 的实测结论——这台玻璃 level 0 是白、
  // 15 是黑（见 EpdiyLcdDriver.cpp 顶部记录）。facade 的约定是"位置一 = 白"，所以
  // 底图里**位清零 = 墨**。平面含义来自 GfxRenderer::mapTwoBitPixel（非 EEGO 分支）：
  // LSB 置位 ⇔ 2-bit 值 1（深灰），MSB 置位 ⇔ 值 1 或 2。
  // / Polarity follows the measured kBlackIsOne result recorded at the top of
  // EpdiyLcdDriver.cpp (level 0 is white on this glass). The facade sets the bit for
  // white, so a CLEAR bit is ink. Plane meaning comes from mapTwoBitPixel.
  // 极性由展开表的实际逻辑钉住（EpdiyLcd.cpp 的 buildExpandTable，blackIsOne=true 取
  // g_expand[0]，即 oneIsBlack=0 -> black = bit ^ 1 -> 位=1 落到 15），与 epdiy.h:93 的
  // 0x0 = 黑 / 0xF = 白 一致。facade 的约定是"位置一 = 白"，所以底图里位清零 = 墨、
  // 位置一 = 纸。**注意 EpdiyLcdDriver.cpp 顶部那段注释说"level 0 呈现为白"，与表
  // 的实际行为相反，不要照它推。**
  // / Polarity is pinned by what buildExpandTable() actually does (with blackIsOne = true
  // EpdiyLcd uses g_expand[0], i.e. oneIsBlack = 0 -> black = bit ^ 1 -> a set bit lands
  // on 15), matching epdiy.h:93 (0x0 = black, 0xF = white). The facade sets the bit for
  // white, so a clear bit is ink and a set bit is paper. NOTE: the comment at the top of
  // EpdiyLcdDriver.cpp claims level 0 renders white, which contradicts the table -- do
  // not reason from it.
  constexpr uint8_t kDarkGray = 5;    // 2-bit 值 1（深灰）/ 2-bit value 1 (dark)
  constexpr uint8_t kLightGray = 10;  // 2-bit 值 2（浅灰）/ 2-bit value 2 (light)
  constexpr int kInkLevel = 0;        // 墨 = 黑 / ink is black
  constexpr int kPaperLevel = 15;     // 纸 = 白 / paper is white
  // 邻域权重与微调上限：这两个就是"圆润 vs 毛刺/模糊"的总旋钮。
  // / The two knobs that trade rounding against jaggies and blur.
  constexpr int kCentreWeight = 8;
  // 微调上限。0 = 纯覆盖率（斜边出毛刺），4 偏糊，2 仍偏糊；1 是当前取值。
  // / Nudge cap. 0 = coverage only (diagonals stair), 4 already read blurry; 2 is
  // / Nudge cap. 0 = coverage only (diagonals stair), 4 blurred, 2 still soft;
  constexpr int kMaxNudge = 1;

  const int w = static_cast<int>(epd_width());
  const int h = static_cast<int>(epd_height());
  const int stride = w / 8;

  for (int y = 0; y < h; ++y) {
    const uint8_t* lrow = lsb + static_cast<size_t>(y) * stride;
    const uint8_t* mrow = msb + static_cast<size_t>(y) * stride;
    uint8_t* drow = g_fb4 + static_cast<size_t>(y) * (w / 2);

    for (int x = 0; x < w; ++x) {
      const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7));
      const bool lb = (lrow[x >> 3] & mask) != 0;
      const bool mb = (mrow[x >> 3] & mask) != 0;
      const bool baseInk = (g_base[static_cast<size_t>(y) * stride + (x >> 3)] & mask) == 0;

      // --- 1) 覆盖率决定的档位 ---------------------------------------------
      int level;
      if (mb && !lb) {
        level = kLightGray;  // 2-bit 值 2
      } else if (lb) {
        level = kDarkGray;  // 2-bit 值 1
      } else {
        level = baseInk ? kInkLevel : kPaperLevel;
      }

      // --- 2) 邻域的有界微调 -----------------------------------------------
      int ink = 0, wsum = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        const int yy = y + dy;
        if (yy < 0 || yy >= h) continue;
        const uint8_t* brow = g_base + static_cast<size_t>(yy) * stride;
        for (int dx = -1; dx <= 1; ++dx) {
          const int xx = x + dx;
          if (xx < 0 || xx >= w) continue;
          const int wgt = (dx == 0 && dy == 0) ? kCentreWeight : 1;
          if ((brow[xx >> 3] & (0x80u >> (xx & 7))) == 0) ink += wgt;  // 位清零 = 墨
          wsum += wgt;
        }
      }
      // 邻域单独会给出的档位，再把这个偏离夹在 ±kMaxNudge 之内。
      // / The level the neighbourhood alone would pick, with its deviation clamped.
      const int nbLevel = (ink * kInkLevel + (wsum - ink) * kPaperLevel + wsum / 2) / wsum;
      int delta = nbLevel - level;
      if (delta > kMaxNudge) delta = kMaxNudge;
      if (delta < -kMaxNudge) delta = -kMaxNudge;
      level += delta;
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
