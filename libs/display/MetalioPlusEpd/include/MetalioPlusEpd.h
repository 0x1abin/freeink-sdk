#pragma once

#include <cstdint>

namespace freeink {
// Synchronous B/W and staged native backend. Set facade bits mean white. The caller
// serializes display operations; the vendor owns its native 4bpp PSRAM buffers.
bool metalioPlusEpdBegin();
enum class MetalioPlusRefresh : uint8_t { Full, Half, Fast };
// Full/unknown baseline clears then GC16; Half/Fast retain the committed native
// baseline and use GL16/DU. The vendor owns all existing PSRAM planes.
bool metalioPlusEpdDraw(const uint8_t* framebuffer, MetalioPlusRefresh mode = MetalioPlusRefresh::Full);
// Borrow the existing native front plane without activating the panel. A commit
// consumes this staged target; cancellation leaves the completed back plane intact.
uint8_t* metalioPlusEpdStageBase(const uint8_t* framebuffer);
bool metalioPlusEpdCommit(MetalioPlusRefresh mode);
void metalioPlusEpdCancelStage();
void metalioPlusEpdRequestResync();
void metalioPlusEpdSleep();
}  // namespace freeink
