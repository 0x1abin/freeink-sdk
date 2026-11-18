#include <cassert>
#include <BoardConfig.h>
#include <type_traits>
#include <esp_heap_caps.h>
#define private public
#include "src/driver/Ssd1677Driver.h"
#include "src/driver/Uc8179Driver.h"
#include "src/driver/Uc8279X4Driver.h"
#undef private

template<class Driver>
static void checkOwnership(bool pro) {
  Driver driver;
  freeink::EpdBus bus;
  bool text = false;
  size_t legacy = 0;
#if defined(BOARD_HAS_PSRAM)
  text = pro && !(std::is_same_v<Driver, freeink::Ssd1677Driver> && FREEINK_SSD1677_TEXT_ROUTING);
  if constexpr (std::is_same_v<Driver, freeink::Uc8179Driver>) legacy = 48000;
#else
  (void)pro;
#endif
  if constexpr (std::is_same_v<Driver, freeink::Uc8279X4Driver>) legacy = 48000;
  const auto before = fakeHeapCapsRequestedBytes;
  driver.begin(bus);
  assert(driver._textAa.ready() == text);
  assert(fakeHeapCapsRequestedBytes - before == legacy + (text ? 576000 : 0));
  const auto after = fakeHeapCapsRequestedBytes;
  driver.begin(bus);
  assert(fakeHeapCapsRequestedBytes == after);
  if constexpr (!std::is_same_v<Driver, freeink::Ssd1677Driver>) free(driver._grayBase);
}

int main() {
  for (auto board : {BoardConfig::Board::XteinkX4Pro, BoardConfig::Board::XteinkX4Classic,
                     BoardConfig::Board::XteinkX3, BoardConfig::Board::XteinkX4}) {
    BoardConfig::ACTIVE.board = board;
    const bool pro = board == BoardConfig::Board::XteinkX4Pro || board == BoardConfig::Board::XteinkX4Classic;
    checkOwnership<freeink::Ssd1677Driver>(pro);
    checkOwnership<freeink::Uc8179Driver>(pro);
    checkOwnership<freeink::Uc8279X4Driver>(pro);
  }
}
