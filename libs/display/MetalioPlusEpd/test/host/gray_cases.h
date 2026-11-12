#pragma once

void runGrayScenario(freeink::MetalioPlusDriver& driver, freeink::EpdBus& bus, const std::string& scenario,
                     std::vector<uint8_t>& frame) {
  const auto caps = driver.grayscaleCapabilities();
  assert(caps.encoding == freeink::GrayscaleEncoding::OverlayMasks && caps.base == freeink::GrayscaleBase::Combined);
  assert(caps.stripUploads && !caps.asyncBase && !caps.stagingWhileBusy);
  assert(!driver.grayscaleCapabilities(freeink::GrayscaleMode::Absolute).supported());
  assert(!driver.grayscaleCapabilities(freeink::GrayscaleMode::Direct).supported());
  std::vector<uint8_t> lsb(frame.size(), 0), msb(frame.size(), 0);
  frame.front() = 0x1f;
  lsb.front() = 0x40;
  msb.front() = 0x60;
  lsb.back() = 0x02;
  msb.back() = 0x03;
  const auto begin = [&](freeink::RefreshMode mode = freeink::RefreshMode::Fast) {
    driver.displayGrayscaleBaseWithContext(bus, frame.data(), mode, false,
                                           freeink::RefreshContext::TextOnlyAntiAliasing);
    assert(!driver.displayCommitted());
  };
  const auto upload = [&](bool strips) {
    if (!strips) {
      driver.copyGrayscaleLsb(bus, lsb.data());
      driver.copyGrayscaleMsb(bus, msb.data());
      return;
    }
    for (const auto plane : {freeink::GrayPlane::Lsb, freeink::GrayPlane::Msb}) {
      const auto& mask = plane == freeink::GrayPlane::Lsb ? lsb : msb;
      for (int y = 0; y < 684; y += 80) {
        driver.writeGrayscalePlaneStrip(bus, plane, mask.data() + y * 152, y, std::min(80, 684 - y));
      }
    }
  };
  const auto commit = [&] { driver.displayGray(bus, msb.data(), false, nullptr, false); };
  const auto expectTones = [&] {
    assert(previousFrame[0] == 0x50 && previousFrame[1] == 0xfa);
    assert(previousFrame[2] == 0xff && previousFrame[sizeof(previousFrame) - 1] == 0xa5);
  };
  begin();
  const int initialOffs = offCalls;
  upload(scenario == "gray_strips");
  assert(powerCalls == 0 && drawCalls == 0 && clearCalls == 0 && offCalls == initialOffs);
  commit();
  assert(driver.displayCommitted() && fullDraws == 1 && diffDraws == 0 && clearCalls == 1);
  expectTones();
  driver.cleanupGrayscaleBuffers(bus, frame.data());
  assert(driver.displayCommitted() && drawCalls == 1);
  expectTones();
  if (scenario == "gray_whole" || scenario == "gray_strips") return;

  begin();
  if (scenario == "gray_missing") {
    driver.copyGrayscaleLsb(bus, lsb.data());
  } else if (scenario == "gray_reordered") {
    driver.copyGrayscaleMsb(bus, msb.data());
    upload(true);
  } else if (scenario == "gray_gap") {
    driver.writeGrayscalePlaneStrip(bus, freeink::GrayPlane::Lsb, lsb.data(), 1, 1);
    upload(true);
  } else if (scenario == "gray_bounds") {
    driver.writeGrayscalePlaneStrip(bus, freeink::GrayPlane::Lsb, lsb.data(), 683, 2);
    upload(true);
  } else if (scenario == "gray_zero_rows") {
    driver.writeGrayscalePlaneStrip(bus, freeink::GrayPlane::Lsb, lsb.data(), 0, 0);
    upload(true);
  } else if (scenario == "gray_null") {
    driver.copyGrayscaleLsb(bus, nullptr);
    upload(true);
  } else if (scenario == "gray_invalid_mask") {
    msb.front() = 0x20;  // Dark LSB without its required MSB.
    upload(false);
  } else {
    upload(true);
  }
  const bool invalid = scenario == "gray_missing" || scenario == "gray_reordered" || scenario == "gray_gap" ||
                       scenario == "gray_bounds" || scenario == "gray_zero_rows" || scenario == "gray_null" ||
                       scenario == "gray_invalid_mask";
  if (invalid) {
    commit();
    assert(!driver.displayCommitted() && drawCalls == 1 && powerCalls == 1);
    driver.cleanupGrayscaleBuffers(bus, nullptr);
    assert(drawCalls == 1);
    return;
  }
  if (scenario == "gray_cancel" || scenario == "gray_fallback") {
    driver.cleanupGrayscaleBuffers(bus, scenario == "gray_cancel" ? nullptr : frame.data());
    if (scenario == "gray_cancel") {
      commit();
      assert(!driver.displayCommitted() && drawCalls == 1);
    } else {
      assert(driver.displayCommitted() && drawCalls == 2 && lastMode == MODE_GL16);
    }
    return;
  }
  if (scenario == "gray_sleep" || scenario == "gray_resync") {
    if (scenario == "gray_sleep")
      driver.deepSleep(bus);
    else
      driver.requestResync(1);
    commit();
    assert(!driver.displayCommitted() && drawCalls == 1);
    begin();
    upload(false);
    commit();
    assert(driver.displayCommitted() && fullDraws == 2 && clearCalls == 2);
    return;
  }
  if (scenario == "gray_new_base") {
    begin();  // Coverage from the superseded target cannot leak into this one.
    commit();
    assert(!driver.displayCommitted() && drawCalls == 1);
    return;
  }
  if (scenario == "gray_full") {
    begin(freeink::RefreshMode::Full);
    upload(false);
  }
  if (scenario == "gray_draw_failure") drawOk = false;
  if (scenario == "gray_power_failure") powerOk = false;
  if (scenario == "gray_off_failure") offOk = false;
  commit();
  const bool failed =
      scenario == "gray_draw_failure" || scenario == "gray_power_failure" || scenario == "gray_off_failure";
  assert(driver.displayCommitted() == !failed);
  if (failed) {
    const int draws = drawCalls, powers = powerCalls;
    driver.cleanupGrayscaleBuffers(bus, frame.data());
    assert(!driver.displayCommitted() && drawCalls == draws && powerCalls == powers);
    drawOk = powerOk = offOk = true;
    begin();
    upload(false);
    commit();
    if (scenario == "gray_off_failure") {
      assert(!driver.displayCommitted() && powerCalls == powers);
    } else {
      assert(driver.displayCommitted() && clearCalls == 2 && fullDraws == 2);
    }
    return;
  }
  expectTones();
  assert(clearCalls == (scenario == "gray_full" ? 2 : 1));
  assert(lastMode == (scenario == "gray_full" ? MODE_GC16 : MODE_GL16));
  if (scenario == "gray_to_bw") {
    driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Fast, false);
    assert(driver.displayCommitted() && lastMode == MODE_GL16 && clearCalls == 1);
    assert(previousFrame[0] == 0 && previousFrame[1] == 0xf0);
    driver.display(bus, frame.data(), nullptr, freeink::RefreshMode::Fast, false);
    assert(driver.displayCommitted() && lastMode == MODE_DU && clearCalls == 1);
  }
}
