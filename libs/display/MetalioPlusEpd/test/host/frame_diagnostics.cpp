#include <array>
#include <cassert>

#include "FrameDiagnostics.h"

int main() {
  std::array<uint8_t, 16 * 8 / 2> frame;
  frame.fill(0xff);
  const auto blank = freeink::frameDiagnostics(frame.data(), 16, 8);
  assert(blank.entire != 0 && blank.center != 0);
  // A footer or other perimeter control can change without changing the page
  // center. Cover all four edges, independent of the caller's orientation.
  for (const auto edge : {3u * 8u, 3u * 8u + 7u, 8u * 7u + 3u, 3u}) {
    frame[edge] = 0;
    const auto changed = freeink::frameDiagnostics(frame.data(), 16, 8);
    assert(changed.entire != blank.entire && changed.center == blank.center);
    frame[edge] = 0xff;
  }
  frame[3 * 8 + 3] = 0x0f;
  const auto lowNibble = freeink::frameDiagnostics(frame.data(), 16, 8);
  assert(lowNibble.entire != blank.entire && lowNibble.center != blank.center);
  frame[3 * 8 + 3] = 0xf0;
  const auto highNibble = freeink::frameDiagnostics(frame.data(), 16, 8);
  assert(highNibble.entire != lowNibble.entire && highNibble.center != lowNibble.center);
  assert(freeink::frameDiagnostics(nullptr, 16, 8).entire == 0);
  assert(freeink::frameDiagnostics(frame.data(), 15, 8).entire == 0);
  assert(freeink::frameDiagnostics(frame.data(), 16, 0).entire == 0);
}
