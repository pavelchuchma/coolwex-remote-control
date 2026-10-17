---
id: 001
title: Key sequences do not verify the display end state and cannot recover from info screens
type: bug
status: in-progress
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

### Verified device behavior (2026-10-17, via `hregPressKey`)

- Cancel does nothing on an info screen.
- A long EHeater+Disinfect press (1.1 s) leaves the info view from **any**
  info screen, not only from `infoTh`.
- Down arrow cycles through the info screens; after `infoTh` it wraps around
  to `infoT5U`.

Consequence: the exit key for the whole `infoT5U … infoD7F` range is the long
EHeater+Disinfect press, and the info-reading part of the refresh sequence
does not need to assume a fixed screen order.

### Design

1. **Wait for a mode instead of checking it once.** `checkDisplayModeAndDoNextStep()`
   keeps the sequence waiting (same step, no key press) until the expected mode
   shows up or a per-step timeout (~1500 ms since key-up) expires; only the
   timeout fails the step. `onLoop()` records the key-up time in the branch
   that already resets `displayReadsAfterKeyUp`; `startKeySequence()` records
   the sequence start as the initial reference. The three-frame settle wait
   stays as a minimum, it protects steps whose expected mode equals the
   previous one (OnOff from `unlocked` to `unlocked`) from a stale frame.

2. **One `ensureUnlocked()` routine, used as prologue and epilogue.** Replaces
   steps 0 and 1 of `commonGetStateSteps0to3()` and step 16 of the refresh
   sequence. It stays in its step and acts on whatever mode is shown:

   | Mode | Action |
   | --- | --- |
   | `unlocked` | done, continue |
   | `displayOff` | Cancel 100 ms |
   | `locked` | Enter 3200 ms |
   | `infoT5U … infoD7F` | EHeater+Disinfect 1100 ms |
   | `setTemp`, `setClock`, `setVacation` | Cancel 100 ms |
   | `unknown`, `invalid` | wait for the next frame |

   Bounded to 4 key presses, then the sequence fails. Because it re-reads the
   mode after every press, it also recovers from a doubled exit press that
   re-enters the info view, and from a race where the boiler leaves the info
   view on its own just before our press.

3. **Collect info screens instead of stepping through them.** Replace refresh
   steps 10–15 with a loop: note which info screen is shown (its value is
   already stored by `decodeDisplayData()`), press Down, repeat until all six
   screens `T5U, T5L, T3, T4, TP, Th` have been seen, bounded to ~10 presses.
   A missed or doubled Down press then costs one extra press instead of
   failing the sequence. Entering the view (step 9) stays as is; leaving it is
   `ensureUnlocked()`.

4. **Refresh Modbus callback requires `unlocked`.** `onSetRefreshStatusCallback()`
   returns `0xFFFF` unless `statusAge == 0` and the display mode is `unlocked`.
   Independent safety net for item 2.

5. **Timeouts.** The refresh takes ~8.7 s today with a 10 s limit; waiting and
   one retry do not fit. Raise refresh to 15 s and power-on to 10 s (the
   controller's TCP timeout is 20 s). Update the comments of
   `cregRefreshStatus` / `cregPowerOn` in `include/types.h`.

6. **Distinguish done from failed.** Step functions return
   `{continue, done, failed}` instead of `bool`, so `processKeySequence()` can
   report the real outcome and the retry logic in items 2 and 3 stays
   readable. The side-effect checks in `modbusImpl.cpp` stay.

7. **Failure diagnostics over Modbus.** No ESP serial log exists for any of
   the incidents. Add three input registers: last failed sequence, step, and
   display mode at that moment. The controller logs them with every state
   read, so the next glitch is captured without a serial cable.

8. **ISR jitter (optional).** Only if the log shows glitches clustering with
   logging or Wi-Fi activity: move `Serial.printf()` out of `Keyboard::keyDown()`
   and print waiting steps only on a step change (today a waiting step logs a
   line every ~30 ms loop).

Residual risk: a glitched combo press may register as a lone short EHeater or
Disinfect press. EHeater alone only opens the eHeat selection (needs Enter to
apply); what a lone Disinfect press does is unverified. Watch for it in the
log once item 7 is in place.

### Implementation order

Items 1–7 go in as one change and one flash: they rewrite the same steps of
`keySequences.cpp`, every test round costs a heating cycle on the real boiler,
and the diagnostics of item 7 are what is needed to judge the rest. Item 8
only on evidence from the log.

Side note: `MODE::vacation` in `decodeDisplayMode()` is unreachable, the same
bit already returns `setVacation` earlier in the function.

The controller side keeps its own retry / reconciliation task
(home-controller `doc/todo/001-boiler-turnon-no-retry.md`); the two fixes are
complementary, not alternatives.

## Implementation status (2026-10-17)

Items 1–7 are implemented in the working tree (`keySequences.cpp/.h`,
`modbusImpl.cpp`, `types.h`, `common.h/.cpp`), build passes, firmware flashed.
Not yet committed, not yet verified on the boiler.

### Desk test (board not connected to the boiler, display mode `unknown`)

Modbus TCP from a laptop, one persistent connection:

- Read of input registers 100–111 works; the new registers read 0 before any
  failure.
- Write of `cregRefreshStatus` returned exception 4 (Slave Device Failure) and
  afterwards registers 109–111 read `1, 0, 0` = `ksRefreshStatus` failed at
  step 0 with display mode `unknown`. The diagnostics path works end to end.
- Two problems found and handled (phase 0, desk, `tools/boiler_test.py`):
  1. **Without display frames the sequence never ran a step.** `onLoop()`
     gated every step behind `displayReadsAfterKeyUp >= 3`, which only
     advances when a frame arrives, so the 1.5 s mode wait was never evaluated
     and the sequence died on the 15 s `processKeySequence()` timeout. Fixed:
     the frame gate is bounded by the same wait timeout and fails the
     sequence with `no display frames received`. Measured afterwards: the
     exception response arrives after 1.66 s with `lastFail = RefreshStatus /
     step 0 / unknown`.
  2. **One exception response arrived after 24 s instead of 15 s.** Not
     reproduced; a clean run measured 15.15 s. Most likely a request from the
     controller was being processed at the same time (the Modbus task is
     blocked while a sequence runs, so a second request queues behind it).
     Keep the 30 s client timeout recommendation below.
- A burst of garbled serial output when the monitor is opened is a known
  artifact of this computer / USB adapter (seen in other ESP projects too),
  not a firmware problem. Ignore the first seconds of every capture.

### Boiler test (2026-10-17, `tools/boiler_test.py`, board in the boiler)

All runs passed, no `ERR` line in any log (`tools/logs/boiler-test-20261017-1406*.log`):

| Scenario | Start state | Result | Duration |
| --- | --- | --- | --- |
| refresh | `displayOff` (recovered via Cancel, then Enter 3.2 s from `locked`) | OK, all temps, `unlocked`, statusAge 0 | 8.76 s |
| power on (already on) | `displayOff` (Cancel woke it straight to `unlocked`) | OK, "No change" after the probe | 1.03 s |
| target temp 60 (current value) | `unlocked` | OK | 1.19 s |
| refresh after `hregPressKey` opened the info view | `infoT5U` | OK, step 0 left the info view with the long press, then the normal path | 6.15 s |
| refresh ×10 in a row | `unlocked` | 10× OK | 4.90–6.04 s |

This reproduces the 2026-10-16 case (write issued while the display is on an
info screen) and shows it is now handled by the gateway. A refresh from the
`unlocked` screen takes ~5 s; the ~8.7 s seen by the controller is the
`displayOff` / `locked` case, the display locks itself between the
controller's writes.

Not tested: a real power toggle (touches the heating) and an actual key glitch
(cannot be injected). Both are left to the diagnostics registers in the field.

### Java client (home-controller): logging the diagnostics

Where things are:

- Gateway firmware with the register map: `include/types.h` in this repo
  (commit `397a2e5`), the source of truth for register numbers and enum order.
- Java client: `home-controller/extensions/src/main/java/org/chuma/homecontroller/extensions/external/boiler/`,
  `BoilerController.java` holds the nested `ModbusRegisters` constants, the
  `ModbusClient` constructor call with the 20 s timeout and `getState()` with
  the debug log line; `State.java` holds the `MODE` enum (ordinal mapped).
- The controller's own retry issue: `home-controller/doc/todo/001-boiler-turnon-no-retry.md`.
  Its item 3 (wait for `unlocked` before writing) is no longer needed, a
  successful refresh now guarantees `unlocked`.
- A rejected write still comes back as Modbus exception 4 (Slave Device
  Failure), j2mod raises `ModbusSlaveException`.

Registers to read, all input registers, no change of the existing ones:

| Register | Value | Meaning |
| --- | --- | --- |
| 109 `iregLastFailSequence` | `KEY_SEQUENCE` ordinal | `0` none since boot, `1` RefreshStatus, `2` PowerOn, `3` SetTargetTemp, `4` EHeatOn |
| 110 `iregLastFailStep` | step number | see tables below |
| 111 `iregLastFailMode` | `MODE` ordinal | display mode at the moment of failure, same enum as register 100 |

The three registers describe the **last** failure only and are overwritten by
the next one. They are set both when a step fails and when the 15 s sequence
timeout expires (then the step is the one the sequence was waiting in).

Steps 0–3 are the common prologue of every sequence:

| Step | Action |
| --- | --- |
| 0 | `ensureUnlocked()`: bring the display from off / locked / info / set screens to `unlocked` |
| 1 | press Vacation to probe the power state |
| 2 | read `setVacation` (power on) or `unlocked` (power off), Cancel if needed |
| 3 | wait for `unlocked` |

RefreshStatus:

| Step | Action |
| --- | --- |
| 4 | request eHeat flag read |
| 5 | wait for the eHeat flag (blinking icon), then press Down |
| 6 | read target temp in `setTemp`, Cancel |
| 7–8 | only when the value was 38: Up, read again, Cancel |
| 9 | long EHeater+Disinfect, enter the info view |
| 10 | press Down until all six temperature screens were seen |
| 11 | `ensureUnlocked()`: long EHeater+Disinfect to leave the info view |

PowerOn:

| Step | Action |
| --- | --- |
| 4 | press OnOff (or finish when the power already matches) |
| 5–7 | probe the power state again (Vacation, Cancel, wait `unlocked`), verify |

SetTargetTemp:

| Step | Action |
| --- | --- |
| 4 | press Up to open `setTemp` |
| 5 | Up/Down until the value matches, then Enter |
| 6 | wait for `unlocked` |

EHeatOn:

| Step | Action |
| --- | --- |
| 4 | press EHeater |
| 5 | press Enter |
| 6 | request eHeat flag read |
| 7 | wait for the eHeat flag, verify it is on |

Suggested changes in `BoilerController.java`:

1. Extend the input register range in the constructor from `iregTempTh` (108)
   to 111 and add the three constants to `ModbusRegisters`.
2. Add a `KEY_SEQUENCE` enum mirroring `types.h` (order matters, like `MODE`).
3. Append `lastFail: <sequence>/<step>/<mode>` to the debug line in
   `getState()`, or log it at WARN only when the value differs from the
   previous read, so a new failure stands out in the log.
4. Raise the Modbus TCP timeout from 20 s to 30 s: the gateway's own
   sequence timeouts are now 15 s and a request that arrives while another
   sequence runs waits for it first.

## Resolution

_Not resolved yet._
