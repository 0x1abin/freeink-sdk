/* The vendor's MetalioSd_ReleaseHold operation, separated from its application
 * storage manager for this display-only probe. Source: metalio_sd.cc at the
 * pinned revision in sources/reference/provenance.json (MIT license).
 */
#include "config.h"
#include "driver/gpio.h"
#include "metalio_sd.h"

void MetalioSd_ReleaseHold(void) {
  const gpio_num_t pins[] = {
      SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN, SDMMC_D1_PIN, SDMMC_D2_PIN, SDMMC_D3_PIN,
  };
  for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
    gpio_hold_dis(pins[i]);
  }
}
