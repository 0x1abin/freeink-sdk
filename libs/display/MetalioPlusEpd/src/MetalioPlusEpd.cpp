#include <Arduino.h>
#include <MetalioPlusEpd.h>
#include <esp_log.h>

#include <cstring>

#if defined(METALIO_PLUS_FRAME_DIAGNOSTICS)
#include "FrameDiagnostics.h"
#endif

extern "C" {
#include <epd_board_metalio_eink4_plus.h>
#include <epdiy.h>
#include <metalio_epd.h>
}

namespace freeink {
namespace {
constexpr int kWidth = 1216;
constexpr int kHeight = 684;
constexpr int kFrameBytes = kWidth / 8 * kHeight;
constexpr uint8_t kPowerGood = 1u << 7;
bool attempted = false;
bool ready = false;
bool powerFault = false;
bool baselineKnown = false;
bool targetStaged = false;

bool powerOffChecked() {
  epd_poweroff();
  for (int attempt = 0; attempt < 20; ++attempt) {
    delay(10);
    uint8_t port = 0xff;
    if (epd_board_metalio_eink4_plus_ioexp_read_port(0, &port) && !(port & kPowerGood)) return true;
  }
  powerFault = true;
  ESP_LOGE("MetalioPlus", "PWR_GOOD did not fall; panel work inhibited until reboot");
  return false;
}
}  // namespace

bool metalioPlusEpdBegin() {
  // MetalioEpd_Init allocates buffers/tasks and is not idempotent. Do not retry
  // it after either success or failure; a failed initialization needs a reboot.
  if (attempted) return ready && !powerFault;
  attempted = true;
  const bool initialized = MetalioEpd_Init();
  auto* hl = MetalioEpd_Hl();
  const bool geometryOk = epd_width() == kWidth && epd_height() == kHeight && hl && hl->front_fb;
  const bool off = powerOffChecked();
  ready = initialized && geometryOk && off;
  if (!ready) ESP_LOGE("MetalioPlus", "Display initialization/geometry/power-off check failed");
  return ready;
}

uint8_t* metalioPlusEpdStageBase(const uint8_t* framebuffer) {
  targetStaged = false;
  if (!ready || powerFault || !framebuffer) return nullptr;
  auto* target = epd_hl_get_framebuffer(MetalioEpd_Hl());
  // The native front plane is scratch until committed. Preserve the back plane
  // representing completed ink, including on cancellation or invalid uploads.
  for (int byte = 0; byte < kFrameBytes; ++byte) {
    const uint8_t value = framebuffer[byte];
    for (int pair = 0; pair < 4; ++pair) {
      const unsigned shift = 6 - pair * 2;
      const uint8_t even = (value & (1u << (shift + 1))) ? 0x0f : 0;
      const uint8_t odd = (value & (1u << shift)) ? 0xf0 : 0;
      target[byte * 4 + pair] = even | odd;
    }
  }
  targetStaged = true;
  return target;
}

uint8_t* metalioPlusEpdStageNative() {
  targetStaged = false;
  if (!ready || powerFault) return nullptr;
  auto* target = epd_hl_get_framebuffer(MetalioEpd_Hl());
  if (!target) return nullptr;
  std::memset(target, 0xff, kFrameBytes * 4);  // Existing vendor PSRAM, not a new allocation.
  targetStaged = true;
  return target;
}

bool metalioPlusEpdCommit(MetalioPlusRefresh mode) {
  if (!ready || powerFault || !targetStaged) return false;
  targetStaged = false;
  const auto started = millis();
  const bool clean = !baselineKnown || mode == MetalioPlusRefresh::Full;
  const auto waveform = clean ? MODE_GC16 : (mode == MetalioPlusRefresh::Half ? MODE_GL16 : MODE_DU);
#if defined(METALIO_PLUS_FRAME_DIAGNOSTICS)
  const auto frame = frameDiagnostics(epd_hl_get_framebuffer(MetalioEpd_Hl()), kWidth, kHeight);
  ESP_LOGI("PlusFrame", "source=%08lx center=%08lx baseline=%d requested=%u", static_cast<unsigned long>(frame.entire),
           static_cast<unsigned long>(frame.center), baselineKnown, static_cast<unsigned>(mode));
#endif
  // Once hardware work begins the physical image is uncertain until the entire
  // checked operation completes. A failed frame must never become a diff base.
  baselineKnown = false;
  epd_poweron();
  if (!epd_board_metalio_eink4_plus_tps_ok()) {
    powerOffChecked();
    ESP_LOGE("MetalioPlus", "TPS65185 power-on failed");
    return false;
  }
  auto result = clean ? epd_clear_checked() : EPD_DRAW_SUCCESS;
  if (result == EPD_DRAW_SUCCESS) {
    auto* hl = MetalioEpd_Hl();
    // Full/recovery retains the accepted physical-white initialization. Ordinary
    // power-off does not destroy the vendor's committed PSRAM back framebuffer.
    // No PREVIOUSLY_WHITE hint is valid for a differential page transition.
    result = clean ? epd_hl_update_screen_from_white(hl, waveform, 25) : epd_hl_update_screen(hl, waveform, 25);
  }
  const bool off = powerOffChecked();
  if (result != EPD_DRAW_SUCCESS) ESP_LOGE("MetalioPlus", "Checked frame failed: 0x%x", unsigned(result));
  baselineKnown = result == EPD_DRAW_SUCCESS && off;
  ESP_LOGI("PlusRefresh", "requested=%u waveform=%u clean=%d committed=%d duration_ms=%lu", static_cast<unsigned>(mode),
           static_cast<unsigned>(waveform), clean, baselineKnown, millis() - started);
  return baselineKnown;
}

bool metalioPlusEpdDraw(const uint8_t* framebuffer, MetalioPlusRefresh mode) {
  return metalioPlusEpdStageBase(framebuffer) && metalioPlusEpdCommit(mode);
}

void metalioPlusEpdCancelStage() { targetStaged = false; }

void metalioPlusEpdRequestResync() {
  targetStaged = false;
  baselineKnown = false;
}

void metalioPlusEpdSleep() {
  targetStaged = false;
  baselineKnown = false;
  if (attempted) powerOffChecked();
}
}  // namespace freeink
