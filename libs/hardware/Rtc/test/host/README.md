# RTC host checks

Run from the SDK root with Python 3 and a C++ compiler:

```sh
python3 libs/hardware/Rtc/test/host/run_pcf8563.py
python3 libs/hardware/Rtc/test/host/test_pcf85063.py
python3 libs/hardware/Rtc/test/host/run.py
```

The PCF8563 runner compiles production `Rtc.cpp` and `I2cBusLock.h` with a
recording Wire device, using ASan/UBSan. It covers one control/calendar snapshot,
STOP/TEST/VL rejection, BCD and Gregorian calendar validation, unchanged outputs
on failed reads, the existing 1900/2000 century interpretation, and a trusted
STOP/calendar/RUN update under one bus guard. Fault injection includes control
read, each write stage, a partially applied calendar write, short reads and
pointer NACKs. TESTC plus VL can be initialized by an explicit valid set; TEST1
is refused. Alarm, CLKOUT and timer state remain unchanged by reads and sets;
`begin()` retains its existing CLKOUT-disable policy.

These checks do not prove backup power retention, actual bus concurrency,
oscillator advancement, UTC interpretation of calendars written by other
firmware, or drift. Physical acceptance must sync trusted UTC, remove all power,
restore power offline, and verify recovery plus ordinary sleep/wake continuity
while touch, gauge and display use the shared bus. RTC alarm wake is outside
this timekeeping change.
