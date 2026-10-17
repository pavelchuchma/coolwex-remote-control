---
id: 001
title: Key sequences do not verify the display end state and cannot recover from info screens
type: bug
status: open
priority: high
component: keyboard
created: 2026-10-17
---

# Key sequences do not verify the display end state and cannot recover from info screens

## Summary

The refresh-status key sequence reports success as soon as the exit key press
is released, without checking that the boiler display has returned to the
`unlocked` screen. If the display is still showing an info screen at that
moment, the next Modbus write (power on, target temperature, eHeat) fails
immediately with a Modbus exception, because every write sequence requires
`unlocked` as its starting point and has no way to leave an info screen.

On 2026-10-16 this left the boiler OFF for the whole scheduled heating
interval: the controller's 12:00 refresh returned with `iregDisplayMode =
infoTh`, the following `cregPowerOn` write was rejected with `Slave Device
Failure`, and the controller does not retry (tracked on the controller side as
home-controller `doc/todo/001-boiler-turnon-no-retry.md`).

Key emulation glitches are an expected property of this hardware hook-up (see
Root cause), so the sequences must be robust against a missed or misread
key press rather than assume every press lands.

## Observed behavior

Controller log (home-controller, Raspberry Pi), 2026-10-16. The gateway is
written to over Modbus TCP; `0x00C8` is `cregRefreshStatus`, `0x00D2` is
`cregPowerOn`:

```
12:00:00,003 DEBUG [BoilerController] Refreshing status
12:00:00,003 DEBUG [ModbusClient]     writing coil register: 0x00C8=true
12:00:08,777 DEBUG [BoilerController] Refreshing state
12:00:08,983 DEBUG [BoilerController] Mode: infoTh, statusAge: 0, tSU: 37, tSL: 21, tT3: 19, tT4: 16, tTP: 21, tTh: 19, tTarget: 42, flags[]
12:00:08,983 DEBUG [BoilerController] Setting power on: true
12:00:08,983 DEBUG [ModbusClient]     writing coil register: 0x00D2=true
12:00:09,142 ERROR Turn on failed
java.lang.RuntimeException: com.ghgande.j2mod.modbus.ModbusSlaveException: Slave Device Failure
```

Points worth noting:

- The refresh coil write returned success after ~8.7 s, which is the normal
  duration of the refresh sequence (every other day in the preceding two weeks
  took 8–9 s as well). All temperatures and `statusAge: 0` were valid.
- `iregDisplayMode` read `infoTh`. Out of ~7100 state reads in the
  controller's log history this is the only `infoTh` and, apart from one
  `displayOff`, the only non-`unlocked` value ever returned after a refresh.
- The power-on write was rejected 160 ms later, i.e. within a single main-loop
  iteration.
- The controller's periodic monitor read `unlocked` again at 12:04, so the
  boiler left the info screen on its own within a few minutes.

Earlier `Slave Device Failure` responses to the **refresh coil itself**
(2026-07-27 15:07, 2026-08-21 17:00) exist in the controller log. Those mean
some `checkDisplayMode()` inside the refresh sequence failed or the 10 s
sequence timeout expired; without the ESP serial log the exact step is
unknown, but they are most likely the same class of glitch.

No ESP serial log is available for any of these events.

## Expected behavior

- A successful `cregRefreshStatus` write must guarantee that the display is
  back in `unlocked` (the state every other sequence starts from).
- A write sequence that finds the display on an info screen (or any other
  recoverable screen) should bring it back to `unlocked` and continue, instead
  of failing on the first step.
- A single missed or misread key press should cost a retry and some delay,
  not a failed Modbus request.

## Root cause

### 1. Refresh sequence finishes without checking the display mode

`src/keySequences.cpp`, `KeyboardSequence::keySequenceRefreshStatus()`:

```cpp
case 15:
    return checkDisplayModeAndDoNextStep(MODE::infoTh, KEYS::keyEHeaterPlusDisinfect, 1100);
case 16:
    Serial.println("INFO: status read completed");
    stateData.onStatusUpdated();
    return false;
```

Step 15 holds EHeater+Disinfect for 1.1 s to leave the last info screen. Step
16 then completes the sequence unconditionally. The only thing between the two
steps is `onLoop()` waiting for three display frames after key-up
(`displayReadsAfterKeyUp < 3`, `src/keySequences.cpp:264`). If the boiler
switches the display back to `unlocked` later than that — or did not register
the long press at all — the sequence still ends, `onStatusUpdated()` sets
`statusAge` to 0, and `onSetRefreshStatusCallback()` (`src/modbusImpl.cpp:7`)
returns success because it only checks `getStatusAgeSeconds() == 0`, never the
display mode. `iregDisplayMode` is updated live from the display, so the
client sees `infoTh`.

### 2. Write sequences cannot leave an info screen

`src/keySequences.cpp`, `KeyboardSequence::commonGetStateSteps0to3()`
(line 24) is the common prologue of every sequence. It handles exactly two
non-`unlocked` starting states: `displayOff` (press Cancel) and `locked`
(hold Enter). Step 2 then demands `unlocked`:

```cpp
case 2:
    return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyVacation, 100);
```

With `infoTh` this returns `false` on the first loop iteration, `onLoop()`
cancels the sequence, `onSetPowerOnCallback()` (`src/modbusImpl.cpp:38`) sees
`isPowerOn()` still `false` and returns `0xFFFF`. The modbus-esp8266 library
compares the stored coil value with the requested `0xFF00` and answers with
exception code 4, `Slave Device Failure` (`Modbus.cpp`, `FC_WRITE_COIL`
handler). Nothing attempts to leave the info screen; the boiler's own display
timeout is the only way out.

### 3. Key emulation glitches are expected, not exceptional

The ESP32 is retro-fitted between the boiler CPU and its display controller
(SPI slave sniffing the frame) and between the CPU and the keyboard matrix. A
key press is emulated by driving a column line in sync with the boiler's row
scan, inside an ISR (`Keyboard::onKeyboardInputRow1Low()`,
`include/keyboard.h`) using busy-wait loops with microsecond timing. A late
interrupt is deliberately ignored (`// delayed interrupt processing, ignore`,
`include/keyboard.h:21`).

Any background activity on the ESP — Wi-Fi stack, FreeRTOS scheduling, the
Modbus task, `Serial.printf()` at 921600 baud from inside `keyDown()` — can
delay the ISR by one or more scan cycles. For a 100 ms press this is usually
harmless; for the 1.1 s long press used to enter and leave the info screens it
can make the boiler see a shorter, interrupted or doubled press, with a
non-deterministic result. Occasional ghosts are therefore inherent to the
design and the sequence logic must tolerate them.

## Proposed fix

In order of impact:

1. **Verify the end state of the refresh sequence.** In step 16 keep
   returning `true` (i.e. keep waiting) until `getDisplayMode() == unlocked`,
   bounded by a loop counter (a few hundred ms). Only then call
   `onStatusUpdated()`. If the bound is hit and the display is still on an
   info screen, repeat the exit long press once (step back to 15) before
   giving up. This uses the same "wait for condition" pattern already used in
   step 5 for the eHeat flag.

2. **Check the display mode in `onSetRefreshStatusCallback()`.** Return
   `0xFFFF` unless `statusAge == 0` **and** the display is `unlocked`, so a
   client never gets a success response while the display is somewhere else.

3. **Add info-screen recovery to `commonGetStateSteps0to3()`.** If the mode is
   any of `infoT5U … infoD7F`, press the key that leaves the info view, wait
   for `unlocked`, then continue with step 2. Which key works from which info
   screen must be verified on the device: the long EHeater+Disinfect press is
   known to work from `infoTh` (that is how step 15 leaves it); Cancel is the
   natural candidate and may be simpler. `setTemp` and `setClock` could get
   the same treatment (Cancel), `setVacation` is already handled in step 3.

4. **Replace the fixed three-frame wait with a mode wait where a mode change
   is expected.** `displayReadsAfterKeyUp < 3` is a heuristic for "display has
   caught up". Where a step knows which mode it expects next, waiting for that
   mode (with a timeout) is more robust than counting frames. This largely
   falls out of 1 and 3.

5. **Reduce ISR jitter (optional, hypothesis).** Move `Serial.printf()` calls
   out of `Keyboard::keyDown()` / key-up handling, or lower the log volume
   while a key is held. Not verified to matter; only worth doing if the serial
   log shows glitches clustering with logging or Wi-Fi activity.

Open questions:

- Capture the ESP serial log around a scheduled 12:00 / 17:00 controller
  write for a few days to see how often `ERR: unlocked expected, but it is:
  ...` and `ERR: Sequence timeout!` actually occur. That gives a baseline for
  judging the fix.
- Does the boiler leave the info view on a short Cancel press, or only on the
  long EHeater+Disinfect press / its own timeout? Determines the recovery key
  in item 3.

The controller side keeps its own retry / reconciliation task
(home-controller `doc/todo/001-boiler-turnon-no-retry.md`); the two fixes are
complementary, not alternatives.

## Resolution

_Not resolved yet._
