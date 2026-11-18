#pragma once
#include <cstdint>
inline constexpr int ESP_OK = 0;
inline bool fakeEegoMacOk = true;
inline uint32_t fakeEegoUid = 0xBB0409;
inline int esp_efuse_mac_get_default(uint8_t* mac) {
  if (!fakeEegoMacOk) return -1;
  mac[0] = mac[1] = mac[2] = 0;
  mac[3] = fakeEegoUid >> 16;
  mac[4] = fakeEegoUid >> 8;
  mac[5] = fakeEegoUid;
  return ESP_OK;
}
