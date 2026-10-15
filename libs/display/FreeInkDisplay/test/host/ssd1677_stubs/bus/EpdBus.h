#pragma once
#include <Arduino.h>

#include <cassert>
#include <vector>
namespace freeink {
enum class BusyPolarity { ActiveHigh };
class EpdBus {
 public:
  class Transaction {
   public:
    explicit Transaction(EpdBus& bus) : _bus(&bus) {
      assert(!_bus->transaction);
      _bus->transaction = true;
    }
    ~Transaction() {
      if (_bus) {
        assert(_bus->transaction);
        _bus->transaction = false;
      }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&& other) noexcept : _bus(other._bus) { other._bus = nullptr; }
    void writeBytes(const uint8_t* p, uint16_t n) {
      assert(_bus && _bus->transaction);
      _bus->data(p, n);
    }

   private:
    EpdBus* _bus;
  };
  struct Event {
    int cmd;
    std::vector<uint8_t> bytes;
  };
  std::vector<Event> events;
  bool busy = false, stuck = false, transaction = false;
  int activation = 0, failAt = 0;
  void (*onRefreshComplete)() = nullptr;
  void reset() { busy = false; }
  void cmd(uint8_t c) {
    assert(!busy && !transaction);
    events.push_back({c, {}});
    if (c == 0x20) {
      busy = true;
      ++activation;
    }
  }
  void data(uint8_t b) { events.back().bytes.push_back(b); }
  void data(const uint8_t* p, uint16_t n) { events.back().bytes.insert(events.back().bytes.end(), p, p + n); }
  void cmdData(uint8_t c, const uint8_t* p, uint16_t n) {
    cmd(c);
    data(p, n);
  }
  Transaction beginTxn() { return Transaction(*this); }
  void waitBusy(const char*) {
    assert(!transaction);
    busy = stuck || (failAt != 0 && activation == failAt);
    events.push_back({-1, {}});
  }
  void waitRefreshComplete(const char* s) {
    waitBusy(s);
    if (onRefreshComplete) onRefreshComplete();
  }
  bool isBusy() const { return busy; }
  void fillPlane(uint8_t c, uint8_t v, uint16_t h, uint16_t wb) {
    cmd(c);
    events.back().bytes.assign(h * wb, v);
  }
  void clear() {
    assert(!busy && !transaction);
    events.clear();
  }
  std::vector<uint8_t> sequences() const {
    std::vector<uint8_t> result;
    for (auto& e : events)
      if (e.cmd == 0x22) result.push_back(e.bytes.at(0));
    return result;
  }
  std::vector<uint8_t> last(int c) const {
    for (auto i = events.rbegin(); i != events.rend(); ++i)
      if (i->cmd == c) return i->bytes;
    return {};
  }
};
}  // namespace freeink
