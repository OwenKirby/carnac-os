#!/usr/bin/env python3
"""PCM waves for the FM1 (2026-10-21): upload an image to one of the free flash areas the firmware reads PCM waves from
(src/dsp/pcm_store.h, src/hardware/pcm_store_fm1.c), over USB-MIDI. The images come from src/dsp/gen_pcm_waves.py
(src/dsp/pcm_area0.bin, pcm_area1.bin: the chimes, ensembles and their attacks, format version 2).

  pcm_store_upload.py info                             what each area holds
  pcm_store_upload.py upload IMAGE --area A            write IMAGE to area A (0: 36 KiB, 1: 56 KiB)
  pcm_store_upload.py clear --area A                   empty area A
  pcm_store_upload.py syx IMAGE OUT.syx --area A       no device: the upload's SysEx frames, for
                                                       src/host/test_pcm_store.c

Each image is made for its own area (a wave refers to its samples by area): upload pcm_area0.bin to area 0 and
pcm_area1.bin to area 1. Needs mido and a backend such as python-rtmidi for the device commands.
"""
import argparse
import struct
import sys
import time
import zlib
from pathlib import Path

HDR = [0x7D, 0x46, 0x4C]
PCM_BEGIN, PCM_WRITE, PCM_END, PCM_INFO = 100, 101, 102, 103
MAGIC, VERSION = 0x57505243, 2
HDR_SIZE, ENTRY_SIZE, NAME_LEN = 32, 36, 12
KINDS = {1: "loop", 2: "one-shot", 3: "wave"}
AREA_SIZE = {0: 0x9000, 1: 0xE000}
CHUNK = 256
RC = {1: "arguments", 2: "flash", 3: "order", 4: "too big for the area", 5: "refused by the image check"}
CHECK = {-1: "no image", -2: "header", -3: "an entry", -4: "data CRC"}


# -------------------------------------------------------------------------------------------------------- image ---
def parse_image(img):
    """-> (count, len, [(name, kind)]); SystemExit when not a version 2 image"""
    if len(img) < HDR_SIZE:
        sys.exit("too short for a PCM image")
    magic, ver, count, n, crc = struct.unpack_from("<IHHII", img)
    if magic != MAGIC or zlib.crc32(img[:28]) != struct.unpack_from("<I", img, 28)[0]:
        sys.exit("not a PCM wave image (src/dsp/gen_pcm_waves.py makes them)")
    if ver != VERSION:
        sys.exit(f"a version {ver} PCM image: the firmware reads version {VERSION}")
    if n != len(img) or zlib.crc32(img[HDR_SIZE:]) != crc:
        sys.exit("damaged PCM image (length or data CRC)")
    ents = [struct.unpack_from("<12sB", img, HDR_SIZE + i * ENTRY_SIZE) for i in range(count)]
    return count, n, [(e[0].rstrip(b"\0").decode("ascii", "replace"), KINDS.get(e[1], "?")) for e in ents]


# ------------------------------------------------------------------------------------------------------ the link ---
def pack7(b):
    """groups of up to 7 bytes, each preceded by their top bits (pcm_store_fm1.c pcm_unpack7)"""
    out = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        out += [x & 0x7F for x in g]
    return out


def u21(v):
    return [v & 0x7F, (v >> 7) & 0x7F, (v >> 14) & 0x7F]


def frames(img, area):
    """the upload as (cmd, args): BEGIN, the bytes after the header in order, then the header (END)"""
    yield PCM_BEGIN, [area]
    for off in range(HDR_SIZE, len(img), CHUNK):
        yield PCM_WRITE, u21(off) + pack7(img[off:off + CHUNK])
    yield PCM_END, pack7(img[:HDR_SIZE])


class Link:
    def __init__(self):
        try:
            import mido
        except ImportError:
            sys.exit("needs mido: pip install mido python-rtmidi")
        self.mido = mido
        fm1 = lambda n: any(k in n for k in ("FM-1", "X0X", "Felucca"))    # X0X's platform names it "X0X FM-1"
        outs = [n for n in mido.get_output_names() if fm1(n)]
        ins = [n for n in mido.get_input_names() if fm1(n)]
        if not outs or not ins:
            sys.exit("no FM1 MIDI port (X0X FM-1): connect the FM1 by USB")
        self.o = mido.open_output(outs[0])
        self.i = mido.open_input(ins[0])

    def req(self, cmd, args, timeout=3.0):
        for _ in self.i.iter_pending():
            pass
        self.o.send(self.mido.Message("sysex", data=HDR + [cmd] + list(args)))
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for m in self.i.iter_pending():
                d = list(m.data) if m.type == "sysex" else []
                if d[:4] == HDR + [cmd]:
                    return d[4:]
            time.sleep(0.002)
        raise TimeoutError(f"no reply to command {cmd}: is Carnac OS 0.9.2 or later on the FM1?")


def area_info(link, area):
    """-> (entries, size bytes, used bytes, names)"""
    r = link.req(PCM_INFO, [area, 0])
    if len(r) < 3 or r[1] != area or len(r) < 8:
        sys.exit(f"area {area}: no such area on the device")
    n, size, used = r[2], (r[3] | r[4] << 7) * 1024, r[5] | r[6] << 7 | r[7] << 14
    names, i, first = [], 8, 0
    while True:
        while len(names) < n and i < len(r):
            j = r.index(0, i)
            names.append(bytes(r[i:j]).decode("ascii", "replace"))
            i = j + 1
        if len(names) >= n:
            return n, size, used, names
        first = len(names)
        r, i = link.req(PCM_INFO, [area, first]), 8


def info(link):
    areas = link.req(PCM_INFO, [0, 0])[0]
    for a in range(areas):
        n, size, used, names = area_info(link, a)
        print(f"area {a}: {size // 1024} KiB, {'empty' if not used else f'{used} bytes used, {n} entries'}")
        for k, nm in enumerate(names):
            print(f"  {k:2d} {nm}")


def run(link, reqs):
    for k, (cmd, args) in enumerate(reqs):
        r = link.req(cmd, args, timeout=5.0)
        rc = r[3] if cmd == PCM_WRITE else r[1] if cmd == PCM_BEGIN else r[0]
        if rc:
            why = RC.get(rc, "?")
            if cmd == PCM_END and rc == 5:
                why += f" ({CHECK.get(8 - r[1], '?')})"
            sys.exit(f"\ncommand {cmd} failed: rc {rc} ({why})")
        print(f"\r{k + 1}/{len(reqs)}", end="", flush=True)
    print()


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("info")
    for name in ("upload", "syx"):
        p = sp.add_parser(name)
        p.add_argument("image")
        if name == "syx":
            p.add_argument("out")
        p.add_argument("--area", type=int, required=True, choices=sorted(AREA_SIZE))
    c = sp.add_parser("clear")
    c.add_argument("--area", type=int, required=True, choices=sorted(AREA_SIZE))
    a = ap.parse_args(argv)
    if a.cmd in ("upload", "syx"):
        img = Path(a.image).read_bytes()
        parse_image(img)
        if len(img) > AREA_SIZE[a.area]:
            sys.exit(f"{a.image}: {len(img)} bytes, area {a.area} holds {AREA_SIZE[a.area]}")
        reqs = list(frames(img, a.area))
        if a.cmd == "syx":
            out = b"".join(bytes([0xF0] + HDR + [cmd] + args + [0xF7]) for cmd, args in reqs)
            Path(a.out).write_bytes(out)
            print(f"wrote {a.out}: {len(reqs)} frames, {len(out)} bytes")
            return
        link = Link()
        run(link, reqs)
        info(link)
        return
    link = Link()
    if a.cmd == "clear":
        run(link, [(PCM_BEGIN, [a.area])])
    info(link)


if __name__ == "__main__":
    main(sys.argv[1:])
