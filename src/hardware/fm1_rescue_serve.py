#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install a package on an FM1 whose firmware no longer answers the installer's handshake, but whose USB interrupt
still runs: keep asking it to update, answer its package reads, then finish with the installer's own step 2.

X0X's tools/fm1_install.py starts with a handshake, which the firmware answers from its main loop. A firmware that
spends its time elsewhere (2026-10-15: a broken build sat 30 s in HARDWARE CALIBRATION, then crashed early in the
main loop and restarted) never answers. But the USB interrupt still takes M-UPGRADE (F0 22 24 35 7F F7) and sets
usb.ota_req, and the main loop's first pass starts the update session (ota_session: step 1, the firmware reads and
checks the package, then hands over to the update loader, which is not part of the firmware and resumes by itself).

So a worker opens the FM1's port, sends UPGRADE every 2 s until the device asks for data, answers each read
(3 tries x 5 s on the device's side), and exits when step 1 is done or the port goes away. The parent relaunches it
(the FM1 restarts every ~30 s) and, after step 1, runs the installer's step 2 (the loader writes the package) and
its check. Workers never close a MIDI port: on this PC closing one after the device vanished crashes Python
(rtmidi WinMM midiInUnprepareHeader, 0xC0000374); they leave with os._exit.

  python src/hardware/fm1_rescue_serve.py PACKAGE.fwsc [--minutes N]      (default 15)
"""
import os
import queue
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "x0x-firmware" / "tools"))
import fm1_install as fi  # noqa: E402

EXIT_STEP1, EXIT_LOST, EXIT_IDLE, EXIT_ANSWERS = 10, 11, 12, 13
# only M-UPGRADE, no handshake: a broken firmware's ed_service() parses every other SysEx frame on a corrupted
# buffer before its main loop reaches the update request (2026-10-15)
HANDSHAKE = "--handshake" in sys.argv
UPGRADE_EVERY = 0.5


def say(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def worker(path, secs):
    """one port session; exits with EXIT_*"""
    import mido
    raw = open(path, "rb").read()
    image = fi.logical_image(raw)
    names = [n for n in mido.get_input_names() if fi.PORT_RE.search(n)]
    if not names:
        os._exit(EXIT_LOST)
    out = fi.pair_output(names[0], mido.get_output_names())
    q = queue.Queue()
    inp = mido.open_input(names[0], callback=lambda m: m.type == "sysex" and q.put(bytes([0xF0, *m.data, 0xF7])))
    o = mido.open_output(out)
    send = lambda pkt: o.send(mido.Message("sysex", data=pkt[1:-1]))
    time.sleep(0.3)
    if HANDSHAKE:
        send(fi.HS_QUERY)
    served, last_up, last_seen, end = 0, 0.0, time.monotonic(), time.monotonic() + secs
    while time.monotonic() < end:
        if not served and time.monotonic() - last_up > UPGRADE_EVERY:
            send(fi.UPGRADE)
            last_up = time.monotonic()
        try:
            pkt = q.get(timeout=0.5)
        except queue.Empty:
            if names[0] not in mido.get_input_names():
                say(f"port gone after {served} reads")
                os._exit(EXIT_LOST)
            continue
        ident = fi.parse_identity(pkt)
        if ident and not served:
            say(f"the FM1 answers the handshake: {ident.text}")
            os._exit(EXIT_ANSWERS)
        r = fi.parse_request(pkt)
        if not r:
            continue
        fl, addr, n = r
        if addr in (fi.FINISH_CHECK, fi.FINISH_WRITE):
            send(fi.response(addr, b"success\0"))
            if addr == fi.FINISH_CHECK:
                say(f"step 1 done after {served} reads: the FM1 starts the update loader")
                time.sleep(1.0)
                os._exit(EXIT_STEP1)
            continue
        if n > fi.MAXDATA or addr + n > len(image):
            say(f"read outside the package: {addr:#x}+{n}")
            continue
        time.sleep(fi.DELAY["reply"])
        send(fi.response(addr, image[addr:addr + n], fl))
        served += 1
        if served == 1:
            say("the FM1 is reading the package (step 1)")
    os._exit(EXIT_IDLE)


def finish(path):
    """step 2 and the check, through the installer (the loader answers its handshake)"""
    raw = open(path, "rb").read()
    image, product = fi.logical_image(raw), fi.product_of(raw)
    up = fi.Updater(fi.MidoBackend())
    step = fi.Progress(sys.stdout)
    time.sleep(fi.DELAY["loader"])
    ota = up.wait_for(lambda i: i.loader, 60.0)
    if not ota:
        say("no update loader answered yet")
        return 1
    say(f"update loader up ({ota.id.text}): writing")
    up.write(ota, image, step)
    print()
    say("written; the FM1 restarts")
    say(f"the FM1 runs {up.verify(product, step)}")
    return 0


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--worker":
        worker(sys.argv[2], float(sys.argv[3]))
    if len(sys.argv) >= 3 and sys.argv[1] == "--finish":
        os._exit(finish(sys.argv[2]))
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    minutes = float(sys.argv[sys.argv.index("--minutes") + 1]) if "--minutes" in sys.argv else 15.0
    end = time.monotonic() + minutes * 60
    say(f"package {fi.product_of(open(path, 'rb').read())}: asking the FM1 to update, every 2 s")
    while time.monotonic() < end:
        rc = subprocess.run([sys.executable, "-u", __file__, "--worker", path, "120"]).returncode
        if rc == EXIT_ANSWERS:                      # it answers after all: the whole normal install
            rc2 = subprocess.run([sys.executable, str(REPO / "x0x-firmware" / "tools" / "fm1_install.py"), path,
                                  "--yes"]).returncode
            if rc2 == 0:
                say("rescue done (normal install)")
                return 0
            say(f"normal install returned {rc2}; trying again")
        elif rc == EXIT_STEP1:
            rc2 = subprocess.run([sys.executable, "-u", __file__, "--finish", path]).returncode
            if rc2 == 0:
                say("rescue done")
                return 0
            say(f"finish step returned {rc2}; trying again")
        elif rc not in (EXIT_LOST, EXIT_IDLE):
            say(f"worker exit {rc}")
        time.sleep(1.0)
    say("gave up")
    return 1


if __name__ == "__main__":
    sys.exit(main())
