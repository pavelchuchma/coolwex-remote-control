# Project Notes

ESP32 (Adafruit Feather ESP32, PlatformIO + Arduino) attached to a Coolwex DSW-300 /
Sinclair SWH 300 heat pump. It sniffs the display SPI bus to read state and drives the
keyboard matrix to emulate key presses. State and commands are exposed over Modbus TCP
(emelianov/modbus-esp8266). Hardware wiring is in README.md.

## Credentials

`include/credentials.h` holds the WiFi SSID/password and is git-ignored. Never commit it,
never paste its values into docs, issues or commit messages. After a fresh clone:

    cp include/credentials.h.example include/credentials.h   # then fill in real values
    git config core.hooksPath .githooks                       # blocks committing credentials.h

The `.githooks/pre-commit` hook refuses any commit that stages `include/credentials.h`
(even via `git add -f`).

## Build, flash, monitor

    pio run                 # build
    pio run -t upload       # flash over USB serial
    pio device monitor      # 921600 baud

Device registers as `boiler.local` via mDNS; Modbus TCP on port 502.
Verbose logging flags (`CORE_DEBUG_LEVEL`, `MODBUSIP_DEBUG`) are commented out in
`platformio.ini`; enable them only locally, do not commit them.

## Where things live

- `src/main.cpp` - setup/loop, SPI slave read of the display frame, WiFi reconnect,
  Modbus FreeRTOS task
- `src/display.cpp` - decodes the 137-bit display frame into MODE, temperatures and flags
- `src/keyboard.cpp`, `include/keyboard.h` - single key press emulation; the ISR body is
  timing critical (microseconds), uses fast GPIO macros from `common.h`, no Serial or
  heavy calls inside
- `src/keySequences.cpp` - multi-step key sequences (power on/off, target temp, refresh,
  eHeat) driven from `loop()`; every write sequence requires display mode `unlocked`
- `src/modbusImpl.cpp` - register callbacks; a write blocks until its key sequence ends
- `include/types.h` - the Modbus register map and enums. It is the public interface
  documentation; update the comments there whenever a register or value changes.

## Conventions

- Temperatures in Modbus registers are stored +128 (`INVALID_TEMP` = -128).
- Source files use CRLF line endings; keep them when editing.
- Issues and TODOs are tracked as files in `doc/todo/` (see its README). Open an issue
  file there before larger changes; close it with a Resolution section, do not delete it.
- Hardware tests can only be done by Pavel on the real heat pump. When a change cannot be
  verified by `pio run`, say so and ask for a serial log instead of assuming it works.
