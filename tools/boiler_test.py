#!/usr/bin/env python3
"""Test harness for the coolwex ESP32 gateway.

Keeps the serial port open (every open resets the board), timestamps every log line
and interleaves it with Modbus TCP requests, so sequence timing can be measured.

    python3 tools/boiler_test.py read refresh read
    python3 tools/boiler_test.py key EHeaterPlusDisinfect 1100 sleep 2 read refresh
    python3 tools/boiler_test.py --no-serial --host boiler.local read

Scenarios: watch SEC | read | refresh | power on|off | temp N | key NAME MS |
           loop refresh N | sleep SEC
"""
import argparse
import datetime
import re
import socket
import struct
import sys
import threading
import time
from pathlib import Path

MODES = ["unknown", "displayOff", "locked", "invalid", "infoT5U", "infoT5L", "infoT3", "infoT4",
         "infoTP", "infoTh", "infoCE", "infoER1", "infoER2", "infoER3", "infoD7F", "setClock",
         "setTemp", "unlocked", "setVacation", "vacation"]
SEQUENCES = ["none", "RefreshStatus", "PowerOn", "SetTargetTemp", "EHeatOn"]
FLAGS = ["on", "hot", "eHeat", "pump", "vacation"]
KEYS = {"EHeater": 0x00, "Vacation": 0x01, "Disinfect": 0x02, "EHeaterPlusDisinfect": 0x03,
        "UpArrow": 0x10, "Enter": 0x11, "DownArrow": 0x12, "ClockTimer": 0x20, "Cancel": 0x21,
        "OnOff": 0x22}
IREG_FIRST, IREG_COUNT = 100, 12
HREG_TEMP_TARGET, HREG_PRESS_KEY = 300, 310
COIL_REFRESH, COIL_POWER = 200, 210
EXCEPTIONS = {1: "Illegal Function", 2: "Illegal Data Address", 3: "Illegal Data Value",
              4: "Slave Device Failure"}


class Log:
    def __init__(self, path):
        self.t0 = time.time()
        self.file = open(path, "w")
        self.lock = threading.Lock()

    def write(self, source, text):
        line = f"{time.time() - self.t0:8.3f} {source:4} {text}"
        with self.lock:
            print(line, flush=True)
            self.file.write(line + "\n")
            self.file.flush()


class SerialReader(threading.Thread):
    def __init__(self, port, log):
        super().__init__(daemon=True)
        import serial
        self.ser = serial.Serial()
        self.ser.port, self.ser.baudrate, self.ser.timeout = port, 921600, 0.05
        self.ser.dtr = self.ser.rts = False
        self.ser.open()
        self.log = log
        self.ip = None
        self.lines = []
        self.stop = False

    def run(self):
        buffer = b""
        while not self.stop:
            buffer += self.ser.read(1 << 16)
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                text = raw.decode("utf-8", "replace").strip()
                if not text or not text.isprintable():
                    continue
                self.lines.append(text)
                self.log.write("ESP", text)
                match = re.search(r"IP address: (\d+\.\d+\.\d+\.\d+)", text)
                if match:
                    self.ip = match.group(1)

    def wait_for_ip(self, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline and not self.ip:
            time.sleep(0.2)
        return self.ip


class Modbus:
    def __init__(self, host, log, timeout=40):
        self.sock = socket.create_connection((host, 502), timeout=timeout)
        self.log = log
        self.tid = 0

    def call(self, pdu, what):
        self.tid += 1
        self.sock.sendall(struct.pack(">HHHB", self.tid, 0, len(pdu) + 1, 1) + pdu)
        started = time.time()
        self.log.write("HOST", f"-> {what}")
        try:
            resp = self.sock.recv(300)[7:]
        except socket.timeout:
            self.log.write("HOST", f"<- {what}: TIMEOUT after {time.time() - started:.2f} s")
            return None
        elapsed = time.time() - started
        if resp and resp[0] & 0x80:
            code = resp[1] if len(resp) > 1 else -1
            self.log.write("HOST", f"<- {what}: EXCEPTION {code} {EXCEPTIONS.get(code, '?')} after {elapsed:.2f} s")
            return None
        self.log.write("HOST", f"<- {what}: {resp.hex()} after {elapsed:.2f} s")
        return resp

    def read_iregs(self):
        resp = self.call(struct.pack(">BHH", 4, IREG_FIRST, IREG_COUNT), f"read iregs {IREG_FIRST}..{IREG_FIRST + IREG_COUNT - 1}")
        if not resp:
            return None
        return struct.unpack(f">{IREG_COUNT}H", resp[2:2 + 2 * IREG_COUNT])

    def read_hreg(self, address):
        resp = self.call(struct.pack(">BHH", 3, address, 1), f"read hreg {address}")
        return struct.unpack(">H", resp[2:4])[0] if resp else None

    def write_coil(self, address, value, what):
        return self.call(struct.pack(">BHH", 5, address, 0xFF00 if value else 0), what) is not None

    def write_hreg(self, address, value, what):
        return self.call(struct.pack(">BHH", 6, address, value), what) is not None


def describe_state(iregs, target):
    mode = MODES[iregs[0]] if iregs[0] < len(MODES) else f"?{iregs[0]}"
    flags = [name for i, name in enumerate(FLAGS) if iregs[2] & (1 << i)]
    temps = ", ".join(f"{name}={value - 128}" for name, value in zip(["T5U", "T5L", "T3", "T4", "TP", "Th"], iregs[3:9]))
    fail_seq = SEQUENCES[iregs[9]] if iregs[9] < len(SEQUENCES) else f"?{iregs[9]}"
    fail_mode = MODES[iregs[11]] if iregs[11] < len(MODES) else f"?{iregs[11]}"
    target_text = "?" if target is None else target - 128
    return (f"mode={mode} statusAge={iregs[1]} flags=[{', '.join(flags)}] {temps} target={target_text} "
            f"lastFail={fail_seq}/step {iregs[10]}/{fail_mode}")


def run_scenarios(args, modbus, log):
    items = list(args.scenario)
    results = []
    while items:
        name = items.pop(0)
        if name == "sleep":
            time.sleep(float(items.pop(0)))
        elif name == "watch":
            log.write("HOST", f"watching serial for {items[0]} s")
            time.sleep(float(items.pop(0)))
        elif name == "read":
            iregs = modbus.read_iregs()
            target = modbus.read_hreg(HREG_TEMP_TARGET)
            if iregs:
                log.write("HOST", "state: " + describe_state(iregs, target))
        elif name == "refresh":
            started = time.time()
            ok = modbus.write_coil(COIL_REFRESH, True, "refresh status")
            results.append(("refresh", ok, time.time() - started))
        elif name == "power":
            value = items.pop(0) == "on"
            started = time.time()
            ok = modbus.write_coil(COIL_POWER, value, f"power {'on' if value else 'off'}")
            results.append(("power", ok, time.time() - started))
        elif name == "temp":
            value = int(items.pop(0))
            started = time.time()
            ok = modbus.write_hreg(HREG_TEMP_TARGET, value + 128, f"target temp {value}")
            results.append(("temp", ok, time.time() - started))
        elif name == "key":
            key, duration = items.pop(0), int(items.pop(0))
            modbus.write_hreg(HREG_PRESS_KEY, (KEYS[key] << 8) + duration // 100, f"press {key} for {duration} ms")
        elif name == "loop":
            what, count = items.pop(0), int(items.pop(0))
            items = [what, "sleep", "2"] * count + items
        else:
            sys.exit(f"unknown scenario: {name}")
    if results:
        log.write("HOST", "summary:")
        for what, ok, elapsed in results:
            log.write("HOST", f"  {what:8} {'OK  ' if ok else 'FAIL'} {elapsed:6.2f} s")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="/dev/cu.usbserial-0001")
    parser.add_argument("--host", help="gateway address; default: IP from the boot log, else boiler.local")
    parser.add_argument("--no-serial", action="store_true", help="do not open the serial port (no board reset)")
    parser.add_argument("--log", help="log file; default tools/logs/boiler-test-<timestamp>.log")
    parser.add_argument("scenario", nargs="*", default=["read"])
    args = parser.parse_args()

    log_path = args.log or Path(__file__).parent / "logs" / f"boiler-test-{datetime.datetime.now():%Y%m%d-%H%M%S}.log"
    log = Log(log_path)
    reader = None
    host = args.host
    if not args.no_serial:
        reader = SerialReader(args.port, log)
        reader.start()
        log.write("HOST", "serial open, board resets, waiting for WiFi")
        ip = reader.wait_for_ip(60)
        if not ip:
            sys.exit("board did not report an IP address")
        host = host or ip
        time.sleep(2)
    host = host or "boiler.local"
    modbus = Modbus(host, log)
    log.write("HOST", f"connected to {host}:502")
    try:
        run_scenarios(args, modbus, log)
    finally:
        time.sleep(1)
        if reader:
            reader.stop = True
        log.write("HOST", f"log saved to {log_path}")


if __name__ == "__main__":
    main()
