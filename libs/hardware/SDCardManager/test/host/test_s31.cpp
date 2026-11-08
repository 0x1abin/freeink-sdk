#include <vector>

#include "SdmmcBlockDevice.h"
#include "native.h"
int failure = 0, hostInits = 0, hostEnds = 0, frees = 0, reads = 0, writes = 0;
void* dma = nullptr;
size_t allocAlignment = 0, allocBytes = 0;
unsigned allocCaps = 0;
std::vector<size_t> chunks;
int sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t* p) {
  assert(slot == (FREEINK_MCU_S31 ? 0 : 1));
  assert(p->width == 4 && p->clk == 24 && p->cmd == 25 && p->d0 == 20 && p->d1 == 21 && p->d2 == 22 && p->d3 == 23);
  return failure == 2 ? ESP_FAIL : ESP_OK;
}
int sdmmc_card_init(const sdmmc_host_t* h, sdmmc_card_t* c) {
  assert(h->max_freq_khz == 20000);
#if FREEINK_MCU_S31
  assert(allocAlignment == 64 && allocBytes == 4096 && (allocCaps & MALLOC_CAP_INTERNAL));
  assert(h->dma_aligned_buffer == dma && h->unaligned_multi_block_rw_max_chunk_size == 8);
  assert(h->check_buffer_alignment && h->check_buffer_alignment(0, dma, 512));
  uint8_t outside[512];
  assert(!h->check_buffer_alignment(0, outside, 512));
#endif
  c->csd.capacity = failure == 3 ? 0 : 1000;
  c->host = *h;
  return failure == 3 ? ESP_FAIL : ESP_OK;
}
int sdmmc_read_sectors(sdmmc_card_t*, void* out, unsigned sector, size_t n) {
  ++reads;
  assert(out == dma && n <= 8);
  if (failure == 4) return ESP_FAIL;
  chunks.push_back(n);
  memset(out, sector & 255, n * 512);
  return ESP_OK;
}
int sdmmc_write_sectors(sdmmc_card_t*, const void* in, unsigned sector, size_t n) {
  ++writes;
  assert(in == dma && n <= 8);
  const auto* b = static_cast<const uint8_t*>(in);
  assert(b[0] == uint8_t(sector));
  return ESP_OK;
}
int main() {
  BoardConfig::SdmmcPins pins{24, 25, 20, 21, 22, 23, 4};
  freeink::SdmmcBlockDevice dev;
  for (int stage : {1, 2, 3, 4, 5, 6}) {
#if !FREEINK_MCU_S31
    if (stage == 5) continue;
#endif
    failure = stage;
    assert(!dev.begin(pins));
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
    const auto diagnostic = freeink::SdmmcBlockDevice::getInitDiagnostics();
    const char* expected[] = {"", "host", "slot", "card", "sector0", "dma", "dma"};
    assert(strcmp(diagnostic.stage, expected[stage]) == 0 && diagnostic.error != ESP_OK);
#endif
    assert(!dma && dev.sectorCount() == 0);
    dev.end();
  }
  failure = 0;
  assert(dev.begin(pins) && dev.sectorCount() == 1000 && allocBytes == 4096);
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
  assert(strcmp(freeink::SdmmcBlockDevice::getInitDiagnostics().stage, "ready") == 0);
  assert(freeink::SdmmcBlockDevice::getInitDiagnostics().error == ESP_OK);
#endif
  chunks.clear();
  uint8_t dst[17 * 512];
  assert(dev.readSectors(10, dst, 17));
  assert((chunks == std::vector<size_t>{8, 8, 1}));
  assert(dst[0] == 10 && dst[8 * 512] == 18 && dst[16 * 512] == 26);
  for (unsigned i = 0; i < 17; ++i) memset(dst + i * 512, 30 + i, 512);
  assert(dev.writeSectors(30, dst, 17) && writes == 3);
  failure = 4;
  assert(!dev.readSector(0, dst));
  failure = 0;
  assert(!dev.readSectors(0, nullptr, 1) && !dev.writeSectors(0, dst, 0));
  dev.end();
  assert(!dma && dev.sectorCount() == 0);
  auto e = hostEnds;
  dev.end();
  assert(hostEnds == e);
  assert(dev.begin(pins));
  dev.end();
  assert(!dma);
  puts(FREEINK_MCU_S31 ? "S31 SDMMC aligned allocation, cleanup and block I/O passed"
                       : "Legacy S3 SDMMC allocation and block I/O passed");
}
