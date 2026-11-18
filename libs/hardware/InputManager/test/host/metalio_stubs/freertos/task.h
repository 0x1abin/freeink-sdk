#pragma once
#include "FreeRTOS.h"
inline void vTaskDelay(int) {}
inline int xTaskCreate(void (*)(void*),const char*,int,void*,int,void**) { return 0; }
namespace fakePinnedTask {
enum class Mode { RunInline, FailCreation, Deferred };
inline Mode mode = Mode::RunInline;
inline void (*pending)(void*) = nullptr;
inline void* pendingArg = nullptr;
inline int stackBytes = 0;
inline void finish() {
  const auto task = pending;
  pending = nullptr;
  if (task) task(pendingArg);
}
}  // namespace fakePinnedTask
inline int xTaskCreatePinnedToCore(void (*fn)(void*), const char*, int stack, void* arg, int, void**, int) {
  fakePinnedTask::stackBytes = stack;
  switch (fakePinnedTask::mode) {
    case fakePinnedTask::Mode::RunInline: fn(arg); return pdPASS;
    case fakePinnedTask::Mode::FailCreation: return 0;
    case fakePinnedTask::Mode::Deferred:
      fakePinnedTask::pending = fn;
      fakePinnedTask::pendingArg = arg;
      return pdPASS;
  }
  return 0;
}
inline void vTaskDelete(void*) {}
