#pragma once

void runNativeScenario(freeink::MetalioPlusDriver& driver, freeink::EpdBus& bus, const std::string& scenario,
                       std::vector<uint8_t>& frame) {
  if (scenario == "native_cancel") {
    driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Full, false);
    assert(previousFrame[0] == 0x0f);
  }
  const int before = drawCalls;
  auto* target = driver.beginGrayscale16();
  assert(target == nativeFrame);  // RED: the existing Plus driver has no native target.
  assert(std::all_of(target, target + sizeof(nativeFrame), [](uint8_t byte) { return byte == 0xff; }));
  // Sixteen literal levels, even pixel in low nibble, without the mask encoder.
  const uint8_t ramp[] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe};
  std::memcpy(target, ramp, sizeof(ramp));
  assert(drawCalls == before);
  if (scenario == "native_cadence") {
    assert(driver.supportsNativeGrayscaleRefreshCycle());
    assert(driver.commitGrayscale16WithMode(frame.data(), freeink::RefreshMode::Fast));
    assert(lastMode == MODE_GC16 && clearCalls == 1);  // Unknown history still cleans.
    target = driver.beginGrayscale16();
    target[0] = 0x87;
    assert(driver.commitGrayscale16WithMode(frame.data(), freeink::RefreshMode::Fast));
    assert(lastMode == MODE_GL16 && clearCalls == 1 && sourceFirst == 0x10 && previousFrame[0] == 0x87);
    target = driver.beginGrayscale16();
    target[0] = 0x32;
    assert(driver.commitGrayscale16WithMode(frame.data(), freeink::RefreshMode::Full));
    assert(lastMode == MODE_GC16 && clearCalls == 2 && previousFrame[0] == 0x32);
    target = driver.beginGrayscale16();
    target[0] = 0x65;
    assert(driver.commitGrayscale16WithMode(frame.data(), freeink::RefreshMode::Half));
    assert(lastMode == MODE_GL16 && clearCalls == 2);
    return;
  }
  if (scenario == "native_cancel") {
    driver.cancelGrayscale16();
    assert(!driver.commitGrayscale16(frame.data()) && drawCalls == before && previousFrame[0] == 0x0f);
    driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Fast, false);
    assert(driver.displayCommitted() && lastMode == MODE_DU);  // No uncommitted gray cleanup debt.
    return;
  }
  if (scenario == "native_resync" || scenario == "native_sleep") {
    if (scenario == "native_sleep")
      driver.deepSleep(bus);
    else
      driver.requestResync(1);
    assert(!driver.commitGrayscale16(frame.data()) && drawCalls == before);
    target = driver.beginGrayscale16();
    assert(target);
    std::memcpy(target, ramp, sizeof(ramp));
  }
  if (scenario == "native_overlay") {
    driver.displayGrayscaleBase(bus, frame.data(), freeink::RefreshMode::Fast, false);
    assert(!driver.commitGrayscale16(frame.data()) && drawCalls == before);
    driver.cancelGrayscale16();  // A native cancellation must not cancel an overlay transaction.
    std::vector<uint8_t> plane(frame.size(), 0);
    driver.copyGrayscaleLsb(bus, plane.data());
    driver.copyGrayscaleMsb(bus, plane.data());
    driver.displayGray(bus, plane.data(), false, nullptr, false);
    assert(driver.displayCommitted() && previousFrame[0] == 0x0f);
    return;
  }
  if (scenario == "native_from_overlay") {
    driver.cancelGrayscale16();
    driver.displayGrayscaleBase(bus, frame.data(), freeink::RefreshMode::Fast, false);
    target = driver.beginGrayscale16();
    assert(target);
    std::memcpy(target, ramp, sizeof(ramp));
    std::vector<uint8_t> plane(frame.size(), 0);
    driver.copyGrayscaleLsb(bus, plane.data());
    driver.displayGray(bus, plane.data(), false, nullptr, false);
    assert(drawCalls == before && std::memcmp(target, ramp, sizeof(ramp)) == 0);
  }
  if (scenario == "native_draw_failure") drawOk = false;
  if (scenario == "native_power_failure") powerOk = false;
  if (scenario == "native_off_failure") offOk = false;
  const bool failure =
      scenario == "native_draw_failure" || scenario == "native_power_failure" || scenario == "native_off_failure";
  assert(driver.commitGrayscale16(frame.data()) == !failure);
  assert(driver.displayCommitted() == !failure);
  if (failure) {
    const int failedDraws = drawCalls;
    driver.cleanupGrayscaleBuffers(bus, frame.data());
    driver.cancelGrayscale16();
    assert(drawCalls == failedDraws && !driver.commitGrayscale16(frame.data()));
    return;
  }
  assert(drawCalls == before + 1 && lastMode == MODE_GC16);
  assert(std::memcmp(previousFrame, ramp, sizeof(ramp)) == 0);
  assert(!driver.commitGrayscale16(frame.data()) && drawCalls == before + 1);
  driver.cleanupGrayscaleBuffers(bus, frame.data());
  assert(drawCalls == before + 1 && previousFrame[0] == 0x10);
  if (scenario == "native_repeat") {
    target = driver.beginGrayscale16();
    assert(target);
    target[0] = 0x87;
    assert(driver.commitGrayscale16(frame.data()) && previousFrame[0] == 0x87);
  }
  if (scenario == "native_to_bw") {
    driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Fast, false);
    assert(driver.displayCommitted() && sourceFirst == 0x10 && lastMode == MODE_GL16);
    driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Fast, false);
    assert(driver.displayCommitted() && lastMode == MODE_DU);
  }
}
