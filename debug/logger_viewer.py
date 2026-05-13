#!/usr/bin/env python3
"""
logger_viewer.py

RP2040 / Pico EventLogger binary viewer.

Frame format (little endian):
    [magic 2B][seq 4B][event_id 2B][level 1B][length 1B][timestamp_us 4B][payload][crc32 4B]

Features:
- serial stream reading with automatic resync on magic (0xA5, 0x5A)
- CRC32 check compatible with EventLogger.cpp
- sequence continuity check (gap / duplicate / late detection)
- printable TEXT_LOG payload decoding
- optional CSV save
- optional quiet mode for periodic stats only
----------------------------------------------------------
>python logger_viewer.py --port /dev/ttyUSB0 --baud 115200
----
>python logger_viewer.py --port /dev/ttyUSB0 --baud 115200 --csv log.csv
----
>python logger_viewer.py --port /dev/ttyUSB0 --baud 460800 --csv adc_log.csv
>python logger_viewer.py --port /dev/ttyUSB0 --baud 460800 --csv adc_log.csv --quiet
---- 
read binary file
>python logger_viewer.py --file captured.bin

"""

from __future__ import annotations

import argparse
import binascii
import csv
import signal
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Optional
import re

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    serial = None

MAGIC0 = 0xA5
MAGIC1 = 0x5A
HEADER_LEN = 12  # seq(4)+event_id(2)+level(1)+length(1)+timestamp(4)
CRC_LEN = 4

LEVEL_NAMES = {
    0: "INFO",
    1: "WARN",
    2: "ERROR",
}

EVENT_NAMES = {
    1: "SYSTEM_START",
    2: "SYSTEM_READY",
    3: "SYSTEM_ERROR",
    4: "SENSOR_READING",
    100: "HEARTBEAT",
    101: "COUNTER_MARK",
    102: "CHECKPOINT",
    110: "UART_RX",
    111: "UART_TX",
    120: "DMA_DONE",
    200: "TEXT_LOG",
    201: "DEBUG_LOG",
    210: "LOGGER_STATS",
    300: "QUEUE_FULL",
    301: "CRC_ERROR",
    302: "UART_ERROR",
    303: "DMA_ERROR",
    304: "SENSOR_ERROR",
    390: "UNKNOWN_ERROR",
    391: "FATAL_ERROR",
    1000: "APP_EVENT_00",
    1001: "APP_EVENT_01",
}


@dataclass
class LogFrame:
    seq: int
    event_id: int
    level: int
    length: int
    timestamp_us: int
    payload: bytes
    crc_rx: int
    crc_calc: int

    @property
    def crc_ok(self) -> bool:
        return self.crc_rx == self.crc_calc


@dataclass
class Stats:
    ok: int = 0
    crc_ng: int = 0
    seq_gap: int = 0
    seq_late: int = 0
    seq_dup: int = 0
    sync: int = 0
    total_bytes: int = 0
    start_monotonic: float = 0.0
    expected_seq: Optional[int] = None

    def elapsed(self) -> float:
        if self.start_monotonic <= 0.0:
            return 0.0
        return time.monotonic() - self.start_monotonic


class ByteReader:
    def read(self, n: int) -> bytes:
        raise NotImplementedError

    def close(self) -> None:
        return None


class SerialByteReader(ByteReader):
    def __init__(self, port: str, baudrate: int, timeout: float) -> None:
        if serial is None:
            raise RuntimeError("pyserial が未インストールです。pip install pyserial を実行してください。")
        self.ser = serial.Serial(port=port, baudrate=baudrate, timeout=timeout)

    def read(self, n: int) -> bytes:
        return self.ser.read(n)

    def close(self) -> None:
        self.ser.close()


class FileByteReader(ByteReader):
    def __init__(self, fp: BinaryIO) -> None:
        self.fp = fp

    def read(self, n: int) -> bytes:
        return self.fp.read(n)

    def close(self) -> None:
        self.fp.close()


def u16le(b: bytes) -> int:
    return int.from_bytes(b, "little", signed=False)


def u32le(b: bytes) -> int:
    return int.from_bytes(b, "little", signed=False)

def color(s, c):
    return f"\033[{c}m{s}\033[0m"

def calc_crc(seq: int, event_id: int, level: int, length: int, timestamp_us: int, payload: bytes) -> int:
    buf = bytearray()
    buf += bytes([MAGIC0, MAGIC1])  
    buf += seq.to_bytes(4, "little")
    buf += event_id.to_bytes(2, "little")
    buf += level.to_bytes(1, "little")
    buf += length.to_bytes(1, "little")
    buf += timestamp_us.to_bytes(4, "little")
    buf += payload
    return binascii.crc32(buf) & 0xFFFFFFFF



def read_exact(reader: ByteReader, n: int) -> Optional[bytes]:
    chunks = bytearray()
    while len(chunks) < n:
        data = reader.read(n - len(chunks))
        if not data:
            return None if len(chunks) == 0 else None
        chunks.extend(data)
    return bytes(chunks)



def sync_to_magic(reader: ByteReader, stats: Stats) -> bool:
    prev = None
    while True:
        b = reader.read(1)
        if not b:
            return False
        stats.total_bytes += 1
        cur = b[0]
        if prev == MAGIC0 and cur == MAGIC1:
            stats.sync += 1
            return True
        prev = cur



def read_frame(reader: ByteReader, stats: Stats) -> Optional[LogFrame]:
    if not sync_to_magic(reader, stats):
        return None

    header = read_exact(reader, HEADER_LEN)
    if header is None:
        return None
    stats.total_bytes += len(header)

    seq = u32le(header[0:4])
    event_id = u16le(header[4:6])
    level = header[6]
    length = header[7]
    timestamp_us = u32le(header[8:12])

    payload = read_exact(reader, length)
    if payload is None:
        return None
    stats.total_bytes += len(payload)

    crc_bytes = read_exact(reader, CRC_LEN)
    if crc_bytes is None:
        return None
    stats.total_bytes += len(crc_bytes)

    crc_rx = u32le(crc_bytes)
    crc_calc = calc_crc(seq, event_id, level, length, timestamp_us, payload)
    return LogFrame(seq, event_id, level, length, timestamp_us, payload, crc_rx, crc_calc)



def is_printable_ascii(data: bytes) -> bool:
    return all((0x20 <= c <= 0x7E) or c in (0x09, 0x0A, 0x0D) for c in data)

def decode_logger_stats(payload: bytes) -> str:
    if len(payload) < 1:
        return "LOGGER_STATS invalid payload: too short"

    version = payload[0]

    if version == 1:
        expected_size = 24
        if len(payload) != expected_size:
            hex_part = " ".join(f"{b:02x}" for b in payload)
            return (
                f"LOGGER_STATS[v1] invalid size: "
                f"expected={expected_size} actual={len(payload)} "
                f"raw={hex_part}"
            )

        version = payload[0]
        reserved1 = payload[1]
        reserved2 = int.from_bytes(payload[2:4], "little")
        enqueue_ok_count   = int.from_bytes(payload[4:8], "little")
        enqueue_drop_count = int.from_bytes(payload[8:12], "little")
        uart_tx_count      = int.from_bytes(payload[12:16], "little")
        uart_tx_bytes      = int.from_bytes(payload[16:20], "little")
        high_water_mark    = int.from_bytes(payload[20:24], "little")

        return (
            f"LOGGER_STATS[v{version}] "
            f"ok={enqueue_ok_count} "
            f"drop={enqueue_drop_count} "
            f"tx_frames={uart_tx_count} "
            f"tx_bytes={uart_tx_bytes} "
            f"hwm={high_water_mark}"
        )

    hex_part = " ".join(f"{b:02x}" for b in payload)
    return f"LOGGER_STATS unknown version={version} raw={hex_part}"

def payload_to_text(frame: LogFrame) -> str:
    if frame.length == 0:
        return ""

    if not frame.crc_ok:
        return "<CRC_NG>"
    
    if frame.event_id == 210:
        return decode_logger_stats(frame.payload)

    if frame.event_id == 200:
        return frame.payload.decode("utf-8", errors="replace")

    if is_printable_ascii(frame.payload):
        return frame.payload.decode("utf-8", errors="replace")

    return " ".join(f"{b:02x}" for b in frame.payload)

ADC_RE = re.compile(
    r"ADC\s+raw=(?P<raw>\d+)\s+avg=(?P<avg>[0-9.]+)\s+voltage=(?P<voltage>[0-9.]+)"
)
def parse_adc_text(text: str):
    m = ADC_RE.search(text)
    if not m:
        return None

    return {
        "adc_raw": int(m.group("raw")),
        "adc_avg": float(m.group("avg")),
        "adc_voltage": float(m.group("voltage")),
    }

def level_name(level: int) -> str:
    return LEVEL_NAMES.get(level, f"LV{level}")



def event_name(event_id: int) -> str:
    return EVENT_NAMES.get(event_id, f"EVENT_{event_id}")



def update_seq_stats(frame: LogFrame, stats: Stats) -> tuple[bool, str]:
    if stats.expected_seq is None:
        stats.expected_seq = frame.seq + 1
        return True, "1"

    if frame.seq == stats.expected_seq:
        stats.expected_seq += 1
        return True, "1"

    if frame.seq > stats.expected_seq:
        gap = frame.seq - stats.expected_seq
        stats.seq_gap += gap
        stats.expected_seq = frame.seq + 1
        return False, f"gap={gap}"

    if frame.seq == stats.expected_seq - 1:
        stats.seq_dup += 1
        return False, "dup"

    stats.seq_late += 1
    return False, "late"


def format_line(frame: LogFrame, stats: Stats, ok_count: int, ng_count: int, seq_ok: bool, seq_note: str) -> str:
    base = f"[{frame.timestamp_us:10d} us] seq={frame.seq:6d} {level_name(frame.level):5s} {event_name(frame.event_id):12s}"

    body = payload_to_text(frame)
    if frame.event_id in (200, 210) and body:
        payload_part = f'msg="{body}"'
    elif body:
        payload_part = f"data={body}"
    else:
        payload_part = ""

    crc_part = color("crc=OK", 32)  if frame.crc_ok else color("crc=NG", 31)
    seq_part = f"seq_ok={1 if seq_ok else 0}"
    tail = f"ok={ok_count} ng={ng_count} gap={stats.seq_gap} late={stats.seq_late} dup={stats.seq_dup}"

    parts = [base]
    if payload_part:
        parts.append(payload_part)
    parts.append(crc_part)
    parts.append(seq_part)
    if not seq_ok:
        parts.append(seq_note)
    parts.append(tail)
    return " ".join(parts)


def open_csv(path: Optional[str]):
    if not path:
        return None, None
    fp = open(path, "w", newline="", encoding="utf-8")
    writer = csv.writer(fp, quoting=csv.QUOTE_ALL, escapechar='\\', doublequote=True)
    writer.writerow([
        "pc_time",
        "seq",
        "event_id",
        "event_name",
        "level",
        "level_name",
        "timestamp_us",
        "length",
        "payload_hex",
        "payload_text",
        "adc_raw",
        "adc_avg",
        "adc_voltage",
        "crc_rx",
        "crc_calc",
        "crc_ok",
    ])
    return fp, writer

def csv_safe_text(s: str) -> str:
    return (
        s.encode("utf-8", errors="replace")
         .decode("utf-8")
         .replace("\x00", "")
         .replace("\r", "\\r")
         .replace("\n", "\\n")
)

def write_csv(writer, frame: LogFrame) -> None:
    if writer is None:
        return

    payload_hex = csv_safe_text(frame.payload.hex(" "))

    if frame.crc_ok:
        payload_text = csv_safe_text(payload_to_text(frame))
    else:
        payload_text = "<CRC_NG>"
    
    adc_raw =""
    adc_avg =""
    adc_voltage =""

    if frame.crc_ok:
        adc = parse_adc_text(payload_text)
        if adc is not None:
            adc_raw = adc["adc_raw"]
            adc_avg = adc["adc_avg"]
            adc_voltage = adc["adc_voltage"]

    writer.writerow([
        time.strftime("%Y-%m-%dT%H:%M:%S"),
        frame.seq,
        frame.event_id,
        event_name(frame.event_id),
        frame.level,
        level_name(frame.level),
        frame.timestamp_us,
        frame.length,
        payload_hex,
        payload_text,
        adc_raw,
        adc_avg,
        adc_voltage,
        f"0x{frame.crc_rx:08X}",
        f"0x{frame.crc_calc:08X}",
        int(frame.crc_ok),
    ])

def print_summary(stats: Stats) -> None:
    elapsed = stats.elapsed()
    rate = (stats.ok / elapsed) if elapsed > 0 else 0.0
    print(
        f"\n--- summary --- ok={stats.ok} crc_ng={stats.crc_ng} "
        f"gap={stats.seq_gap} late={stats.seq_late} dup={stats.seq_dup} sync={stats.sync} "
        f"elapsed={elapsed:.1f}s rate={rate:.1f} frame/s bytes={stats.total_bytes}"
    )

def main() -> int:
    parser = argparse.ArgumentParser(description="Pico EventLogger binary viewer")
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial port, 例: /dev/ttyUSB0 or COM3")
    src.add_argument("--file", help="binary log file")
    parser.add_argument("--baud", type=int, default=115200, help="baudrate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=0.2, help="serial timeout seconds")
    parser.add_argument("--csv", help="save decoded frames to csv")
    parser.add_argument("--max", type=int, default=0, help="stop after N valid frames (0=unlimited)")
    parser.add_argument("--stats-interval", type=float, default=5.0, help="periodic summary interval seconds")
    parser.add_argument("--quiet", action="store_true", help="do not print every frame")
    args = parser.parse_args()

    stats = Stats(start_monotonic=time.monotonic())
    stop = False

    def _sigint(_signum, _frame):
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, _sigint)

    if args.port:
        reader: ByteReader = SerialByteReader(args.port, args.baud, args.timeout)
    else:
        reader = FileByteReader(open(args.file, "rb"))

    csv_fp, csv_writer = open_csv(args.csv)

    last_stats_print = time.monotonic()

    try:
        while not stop:
            frame = read_frame(reader, stats)
            if frame is None:
                if args.file:
                    break
                now = time.monotonic()
                if now - last_stats_print >= args.stats_interval:
                    print_summary(stats)
                    last_stats_print = now
                continue

            # まずCRC判定
            if frame.crc_ok:
                stats.ok += 1
                seq_ok, seq_note = update_seq_stats(frame, stats)
            else:
                stats.crc_ng += 1
                stats.expected_seq = None   # 壊れたseqを基準にしない
                seq_ok = False
                seq_note = "crc_ng"

            write_csv(csv_writer, frame)

            if csv_fp is not None:
                csv_fp.flush()

            if not args.quiet:
                print(format_line(frame, stats, stats.ok, stats.crc_ng, seq_ok, seq_note), flush=True)

            if args.max > 0 and stats.ok >= args.max:
                break

            now = time.monotonic()
            if now - last_stats_print >= args.stats_interval:
                print_summary(stats)
                last_stats_print = now

    finally:
        print_summary(stats)
        if csv_fp is not None:
            csv_fp.close()
        reader.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())