#!/usr/bin/env python3
"""ReticulumWatch M11 USB-serial backup/recovery helper.

Typical safe repair workflow:
  uv run --with pyserial python tools/rw_recovery.py repair \
    --port /dev/cu.usbmodemXXXX --out backups/watch-YYYYMMDD

The tool will not issue FORMAT unless a complete RWB1 backup was captured and
verified first. Restore happens immediately after formatting in the same serial
session, and every file is checked against its CRC32 before the watch accepts it.
"""
from __future__ import annotations

import argparse
import base64
import json
import pathlib
import sys
import time
import zlib

def safe_rel(path: str) -> pathlib.Path:
    if not path.startswith("/") or ".." in path.split("/"):
        raise RuntimeError(f"unsafe device path: {path!r}")
    rel = pathlib.Path(path.lstrip("/"))
    if not rel.parts:
        raise RuntimeError("empty device path")
    return rel


def open_serial(port: str, baud: int, timeout: float):
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("pyserial is required; run with: uv run --with pyserial python tools/rw_recovery.py ...") from exc
    return serial.Serial(port, baudrate=baud, timeout=timeout, write_timeout=timeout)


def send_line(ser, text: str) -> None:
    ser.write((text + "\n").encode("utf-8"))
    ser.flush()


def read_line(ser, deadline: float) -> str:
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if line:
            print(line)
            return line
    raise TimeoutError("timed out waiting for watch")


def wait_prefix(ser, prefix: str, seconds: float = 30.0) -> str:
    deadline = time.monotonic() + seconds
    while True:
        line = read_line(ser, deadline)
        if line.startswith(prefix):
            return line


def capture_backup(ser, out_dir: pathlib.Path) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    send_line(ser, "M11 BACKUP")
    begin = wait_prefix(ser, "RWB1 BEGIN ", 30.0)
    _, _, proto, version = begin.split(maxsplit=3)
    if proto != "1":
        raise RuntimeError(f"unsupported backup protocol {proto}")

    files = []
    current = None
    current_fp = None
    crc = 0
    written = 0
    deadline = time.monotonic() + 120.0

    try:
        while True:
            line = read_line(ser, deadline)
            deadline = time.monotonic() + 120.0
            if line.startswith("RWB1 FILE "):
                if current is not None:
                    raise RuntimeError("new FILE before EOF")
                parts = line.split(" ", 4)
                if len(parts) != 5:
                    raise RuntimeError(f"bad FILE line: {line}")
                path, size_text, crc_text = parts[2], parts[3], parts[4]
                rel = safe_rel(path)
                target = out_dir / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                current = {"path": path, "size": int(size_text), "crc32": crc_text.upper()}
                current_fp = target.open("wb")
                crc = 0
                written = 0
            elif line.startswith("RWB1 DATA "):
                if current is None or current_fp is None:
                    raise RuntimeError("DATA outside file")
                payload = base64.b64decode(line[10:].encode("ascii"), validate=True)
                current_fp.write(payload)
                crc = zlib.crc32(payload, crc)
                written += len(payload)
            elif line.startswith("RWB1 EOF "):
                if current is None or current_fp is None:
                    raise RuntimeError("EOF outside file")
                eof_path = line[9:]
                current_fp.close(); current_fp = None
                if eof_path != current["path"]:
                    raise RuntimeError(f"EOF path mismatch {eof_path} != {current['path']}")
                actual_crc = f"{crc & 0xffffffff:08X}"
                if written != current["size"] or actual_crc != current["crc32"]:
                    raise RuntimeError(
                        f"verification failed {current['path']}: size {written}/{current['size']} crc {actual_crc}/{current['crc32']}"
                    )
                files.append(current)
                current = None
            elif line.startswith("RWB1 END "):
                if current is not None:
                    raise RuntimeError("END while file open")
                parts = line.split()
                if len(parts) != 6:
                    raise RuntimeError(f"bad END line: {line}")
                _, _, file_count, byte_count, manifest_crc, token = parts
                if int(file_count) != len(files):
                    raise RuntimeError(f"file-count mismatch {file_count} != {len(files)}")
                if int(byte_count) != sum(f["size"] for f in files):
                    raise RuntimeError("byte-count mismatch")
                result = {
                    "protocol": 1,
                    "firmware": version,
                    "files": files,
                    "file_count": len(files),
                    "byte_count": int(byte_count),
                    "manifest_crc32": manifest_crc.upper(),
                    "format_token": token.upper(),
                }
                (out_dir / "manifest.json").write_text(json.dumps(result, indent=2) + "\n")
                print(f"Backup verified: {len(files)} files, {byte_count} bytes, token {token}")
                return result
            elif line.startswith("RWB1 ERROR"):
                raise RuntimeError(line)
    finally:
        if current_fp is not None:
            current_fp.close()


def restore_backup_once(ser, backup_dir: pathlib.Path, manifest: dict, force: bool = False) -> None:
    token = manifest["format_token"]
    send_line(ser, f"M11 RESTORE BEGIN {'FORCE' if force else token}")
    wait_prefix(ser, "RWR1 RESTORE READY", 30.0)

    for item in manifest["files"]:
        path = item["path"]
        data = (backup_dir / safe_rel(path)).read_bytes()
        actual = f"{zlib.crc32(data) & 0xffffffff:08X}"
        if len(data) != item["size"] or actual != item["crc32"]:
            raise RuntimeError(f"local backup changed or corrupt: {path}")
        send_line(ser, f"M11 PUT {path} {len(data)} {actual}")
        wait_prefix(ser, f"RWR1 PUT READY {path}", 30.0)
        for offset in range(0, len(data), 96):
            chunk = base64.b64encode(data[offset:offset+96]).decode("ascii")
            send_line(ser, f"M11 DATA {chunk}")
            # M11-build2 acknowledges every chunk. This deliberately paces
            # USB CDC input so the ESP32 cannot be overrun by a host-side burst.
            wait_prefix(ser, "RWR1 DATA OK ", 15.0)
        send_line(ser, "M11 DONE")
        wait_prefix(ser, f"RWR1 FILE OK {path}", 30.0)

    send_line(ser, "M11 RESTORE END")
    wait_prefix(ser, "RWR1 RESTORE OK ", 30.0)
    print("Restore verified by watch; it should now reboot.")


def restore_backup(ser, backup_dir: pathlib.Path, manifest: dict, force: bool = False, retries: int = 3) -> None:
    """Restore with bounded whole-session retries.

    Re-running from file 1 is safe: the watch removes an existing destination
    before each PUT. On retry we explicitly use FORCE because the partition may
    already have been formatted by a prior interrupted attempt.
    """
    last_error = None
    for attempt in range(1, retries + 1):
        try:
            restore_backup_once(ser, backup_dir, manifest, force=(force or attempt > 1))
            return
        except (TimeoutError, RuntimeError) as exc:
            last_error = exc
            if attempt >= retries:
                break
            print(f"Restore attempt {attempt} interrupted: {exc}; retrying safely with FORCE...")
            try:
                ser.reset_input_buffer()
            except Exception:
                pass
            time.sleep(0.5)
    raise RuntimeError(f"restore failed after {retries} attempt(s): {last_error}")


def cmd_status(args):
    with open_serial(args.port, args.baud, args.timeout) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        send_line(ser, "M11 STATUS")
        wait_prefix(ser, "[m11:recovery] status ", 15.0)


def cmd_capture(args):
    with open_serial(args.port, args.baud, args.timeout) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        capture_backup(ser, pathlib.Path(args.out))


def cmd_repair(args):
    out = pathlib.Path(args.out)
    with open_serial(args.port, args.baud, args.timeout) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        manifest = capture_backup(ser, out)
        token = manifest["format_token"]
        print("Backup is locally verified; requesting explicit watch-side format with same-session token.")
        send_line(ser, f"M11 FORMAT {token}")
        wait_prefix(ser, f"RWR1 FORMAT OK {token}", 60.0)
        restore_backup(ser, out, manifest, force=False)


def cmd_restore(args):
    backup = pathlib.Path(args.backup)
    manifest = json.loads((backup / "manifest.json").read_text())
    with open_serial(args.port, args.baud, args.timeout) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        restore_backup(ser, backup, manifest, force=args.force)


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="ReticulumWatch M11 USB recovery helper")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--timeout", type=float, default=2.0)
    sub = p.add_subparsers(dest="cmd", required=True)

    q = sub.add_parser("status")
    q.add_argument("--port", required=True)
    q.set_defaults(func=cmd_status)

    q = sub.add_parser("capture")
    q.add_argument("--port", required=True)
    q.add_argument("--out", required=True)
    q.set_defaults(func=cmd_capture)

    q = sub.add_parser("repair", help="backup -> token-gated format -> verified restore")
    q.add_argument("--port", required=True)
    q.add_argument("--out", required=True)
    q.set_defaults(func=cmd_repair)

    q = sub.add_parser("restore", help="restore an existing backup; --force is for interrupted recovery")
    q.add_argument("--port", required=True)
    q.add_argument("--backup", required=True)
    q.add_argument("--force", action="store_true")
    q.set_defaults(func=cmd_restore)
    return p


def main() -> int:
    args = parser().parse_args()
    try:
        args.func(args)
        return 0
    except KeyboardInterrupt:
        return 130
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
