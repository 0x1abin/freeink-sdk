#pragma once
#include "FreeRTOS.h"
#include <chrono>
#include <thread>
inline bool fakeTaskEnabled = false;
inline void vTaskDelay(int ms) {
  if (fakeTaskEnabled) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
inline int xTaskCreate(void (*fn)(void*),const char*,int,void* arg,int,void**) {
  if (!fakeTaskEnabled) return 0;
  std::thread([=] { fn(arg); }).detach();
  return pdPASS;
}
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
