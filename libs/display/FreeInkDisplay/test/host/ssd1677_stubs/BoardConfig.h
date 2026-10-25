#pragma once
#include <MurphyM4Batch.h>
namespace BoardConfig {
enum class Board { XteinkX4, XteinkX4Pro, Sticky, WsEpaper397, WaveshareEpaper397, MetalioEink4 };
struct Profile {
  struct { int8_t sclk=12, mosi=11, cs=13, dc=18, rst=14, busy=6, powerEnable=-1; } display;
  struct { int8_t miso=40; } sd;
  unsigned char grayscaleLevels = 4;
  Board board = Board::MetalioEink4;
  unsigned displayWidth = 32, displayHeight = 8, displaySpiHz = 10000000;
  struct {
    bool mirrorX = false, mirrorY = false;
  } orientation;
};
inline Profile ACTIVE;
}  // namespace BoardConfig
