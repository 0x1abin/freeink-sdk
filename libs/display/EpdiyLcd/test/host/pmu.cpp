#include <BoardReadPico.cpp>
#include <chrono>
#include <thread>

void delay(unsigned long) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

int main() {
  BoardReadPico::detail::beginI2C();
  BoardReadPico::g_pmuPresent = true;
  std::thread clock([] {
    for (int i = 0; i < 30; ++i) {
      uint32_t seconds;
      bool synced;
      assert(BoardReadPico::pmuTimeGet(seconds, synced));
      assert(seconds == 0x12345678 && synced);
    }
  });
  std::thread panel([] {
    for (int i = 0; i < 30; ++i) assert(BoardReadPico::pmuVcomMv() == 1500);
  });
  clock.join();
  panel.join();
  assert(Wire.commands == 60 && !Wire.inFlight);
}
