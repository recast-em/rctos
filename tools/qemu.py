#!/usr/bin/env python3
"""qemu.py - startet rctos in QEMU: zum Ansehen, zum Testen, für Bildschirmfotos.

  qemu.py --iso build/rctos-x86_64.iso                   Fenster, Log auf stdout
  qemu.py --iso ... --headless --expect "rctos: idle" --shot build/screen.png
  qemu.py --iso ... --bios                               ohne UEFI (SeaBIOS)

Im Testmodus wartet das Skript auf alle --expect-Zeilen im seriellen Log,
macht dann das Bildschirmfoto (QMP screendump) und beendet QEMU. Exit 1 bei
Zeitüberschreitung oder wenn "PANIC" im Log steht.
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

OVMF = ["/usr/share/ovmf/OVMF.fd", "/usr/share/OVMF/OVMF.fd", "/usr/share/qemu/OVMF.fd"]
# Die Testmaschine wacht wie jedes Gerät der Familie am 9. Oktober 2026 um 10:10 auf.
FAMILY_EPOCH = "2026-10-09T10:10:00"


def qmp(sock_path, *commands):
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock_path)
    f = s.makefile("rw")
    f.readline()  # Begrüßung
    replies = []
    for cmd in ({"execute": "qmp_capabilities"},) + commands:
        f.write(json.dumps(cmd) + "\n")
        f.flush()
        while True:
            reply = json.loads(f.readline())
            if "return" in reply or "error" in reply:
                replies.append(reply)
                break
    s.close()
    return replies[1:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True)
    ap.add_argument("--bios", action="store_true", help="SeaBIOS statt UEFI")
    ap.add_argument("--headless", action="store_true")
    ap.add_argument("--expect", action="append", default=[])
    ap.add_argument("--shot")
    ap.add_argument("--log")
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("--after", type=float, default=0.5, help="Sekunden nach den Erwartungen")
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--mem", default="1G")
    ap.add_argument("--rtc", default=None, help="Uhrzeit der Maschine, 'family' = 2026-10-09 10:10")
    args = ap.parse_args()

    cmd = ["qemu-system-x86_64", "-machine", "q35", "-smp", str(args.smp), "-m", args.mem,
           "-cdrom", args.iso, "-boot", "d", "-no-reboot"]
    cmd += ["-accel", "kvm", "-cpu", "host"] if os.access("/dev/kvm", os.W_OK) else ["-cpu", "max"]
    if not args.bios:
        firmware = next((p for p in OVMF if os.path.exists(p)), None)
        if not firmware:
            sys.exit("qemu.py: keine OVMF-Firmware gefunden (Paket ovmf)")
        cmd += ["-bios", firmware]
    if args.rtc:
        cmd += ["-rtc", "base=" + (FAMILY_EPOCH if args.rtc == "family" else args.rtc)]

    if not args.headless:
        cmd += ["-serial", "stdio"]
        return subprocess.call(cmd)

    tmp = tempfile.mkdtemp(prefix="rctos-qemu-")
    log_path = args.log or os.path.join(tmp, "serial.log")
    sock_path = os.path.join(tmp, "qmp.sock")
    cmd += ["-display", "none", "-serial", "file:" + log_path,
            "-qmp", "unix:%s,server=on,wait=off" % sock_path]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    deadline, status, seen = time.time() + args.timeout, 1, ""
    try:
        while time.time() < deadline:
            time.sleep(0.2)
            if os.path.exists(log_path):
                seen = open(log_path, "rb").read().decode("latin-1").replace("\r", "")
            if "PANIC" in seen:
                print("qemu.py: kernel panic", file=sys.stderr)
                time.sleep(args.after)
                if args.shot:   # auch der Fehler ist ein Exponat
                    qmp(sock_path, {"execute": "screendump", "arguments": {
                        "filename": os.path.abspath(args.shot), "format": "png"}})
                break
            if proc.poll() is not None:
                print("qemu.py: qemu ended early: %s" % proc.stderr.read().decode(), file=sys.stderr)
                break
            if all(e in seen for e in args.expect):
                time.sleep(args.after)
                if args.shot:
                    shot = os.path.abspath(args.shot)
                    r = qmp(sock_path, {"execute": "screendump",
                                        "arguments": {"filename": shot, "format": "png"}})
                    if "error" in r[0]:
                        print("qemu.py: screendump: %s" % r[0]["error"], file=sys.stderr)
                        break
                status = 0
                break
        else:
            missing = [e for e in args.expect if e not in seen]
            print("qemu.py: timeout, missing: %s" % missing, file=sys.stderr)
    finally:
        if proc.poll() is None:
            try:
                qmp(sock_path, {"execute": "quit"})
            except OSError:
                pass
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
        sys.stdout.write(seen)
        shutil.rmtree(tmp, ignore_errors=True)
    return status


if __name__ == "__main__":
    sys.exit(main())
