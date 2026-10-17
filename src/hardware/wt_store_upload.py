#!/usr/bin/env python3
"""Upload the wavetable store image to an FM1 running the synth (on X0X's platform), over USB-MIDI (spec 6.5).

  wt_store_upload.py info                       what the device holds: attached or not, its bank list
  wt_store_upload.py upload [IMAGE]             write IMAGE (default src/dsp/wavetables_image.bin) to the store
  wt_store_upload.py syx [IMAGE] OUT.syx        no device: write the upload's SysEx frames to a file
                                                (src/host/test_wt_store.c replays it)

The image comes from src/dsp/gen_wavetables.py. It lands in Felucca's store at flash 0xA0000 (the three user sample
slots) and the synth reads it in place; the firmware installer never writes there, so a fresh install plays a sine
on every bank until this has run once. A new image is only needed when the bank list changes (the device refuses an
image whose set id is not its own). Protocol: src/hardware/wt_store_fm1.c, cmds 96..99. Needs mido and a backend
such as python-rtmidi for the device commands.
"""
import struct
import sys
import time
import zlib
from pathlib import Path

HDR = [0x7D, 0x46, 0x4C]
WT_BEGIN, WT_WRITE, WT_END, WT_INFO = 96, 97, 98, 99
DATA_OFF = 0x1000
CHUNK = 256
DEFAULT_IMAGE = Path(__file__).resolve().parent.parent / "dsp" / "wavetables_image.bin"
RC = {1: "arguments", 2: "erase / header", 3: "write / data CRC", 4: "store in use / flash", 5: "refused on attach"}


def pack7(b):
    """groups of up to 7 bytes, each preceded by their top bits (editor.c ed_unpack7)"""
    out = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        out += [x & 0x7F for x in g]
    return out


def u21(v):
    return [v & 0x7F, (v >> 7) & 0x7F, (v >> 14) & 0x7F]


def load_image(path):
    """the image and its header fields; SystemExit when it is not a wavetable store image"""
    img = Path(path).read_bytes()
    if len(img) < 32:
        sys.exit(f"{path}: too short for a wavetable image")
    (magic, version, banks, frames, frame_len, levels, _, data_off, data_len, crc, set_id,
     hcrc) = struct.unpack_from("<IHBBHBBIIIII", img)
    if magic != 0x42545746 or zlib.crc32(img[:28]) != hcrc:
        sys.exit(f"{path}: not a wavetable store image (run src/dsp/gen_wavetables.py)")
    if data_off != DATA_OFF or len(img) != data_off + data_len or zlib.crc32(img[data_off:]) != crc:
        sys.exit(f"{path}: damaged image (sizes or data CRC)")
    return img, {"banks": banks, "set_id": set_id, "bytes": len(img)}


def frames(img):
    """the upload as (cmd, args) requests: BEGIN, the data in 256-byte writes, then the header (END)"""
    yield WT_BEGIN, []
    for off in range(DATA_OFF, len(img), CHUNK):
        yield WT_WRITE, u21(off) + pack7(img[off:off + CHUNK])
    yield WT_END, pack7(img[:32])


def info_lines(r):
    """the WT_INFO reply -> text; ValueError when malformed"""
    try:
        attached, banks = r[0], r[1]
        set_id = sum(r[2 + i] << (7 * i) for i in range(5)) & 0xFFFFFFFF
        names, i = [], 8
        for _ in range(banks):
            j = r.index(0, i)
            names.append("".join(chr(c) if 32 <= c < 127 else "?" for c in r[i:j]))
            i = j + 1
        return attached, set_id, [f"store {r[7]} KiB, {'attached' if attached else 'NO IMAGE: every bank plays a sine'}",
                                  f"firmware bank list (set id 0x{set_id:08X}):"] + \
            [f"  {b:2d} {n}" for b, n in enumerate(names)]
    except (IndexError, ValueError):
        raise ValueError("malformed WT_INFO reply from the device") from None


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
            sys.exit("no FM1 MIDI port (X0X FM-1 / Felucca): connect the FM1 by USB")
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
        raise TimeoutError(f"no reply to command {cmd}")


def info(link):
    attached, set_id, lines = info_lines(link.req(WT_INFO, []))
    print("\n".join(lines))
    return attached, set_id


def upload(link, path):
    img, h = load_image(path)
    _, dev_id = info(link)
    if dev_id != h["set_id"]:
        sys.exit(f"{path} holds set id 0x{h['set_id']:08X}, the firmware wants 0x{dev_id:08X}: "
                 "regenerate the image from the same gen_wavetables.py as the firmware")
    reqs = list(frames(img))
    for n, (cmd, args) in enumerate(reqs):
        r = link.req(cmd, args, timeout=5.0)
        rc = r[3] if cmd == WT_WRITE else r[0]
        if rc:
            sys.exit(f"\ncommand {cmd} failed: rc {rc} ({RC.get(rc, '?')})")
        print(f"\r{n + 1}/{len(reqs)}", end="", flush=True)
    print()
    attached, _ = info(link)
    sys.exit(0 if attached else "the device did not attach the image")


def main(argv):
    if not argv or argv[0] not in ("info", "upload", "syx"):
        sys.exit(__doc__)
    if argv[0] == "syx":
        if len(argv) not in (2, 3):
            sys.exit(__doc__)
        img, _ = load_image(argv[1] if len(argv) == 3 else DEFAULT_IMAGE)
        out = bytearray()
        for cmd, args in frames(img):
            out += bytes([0xF0] + HDR + [cmd] + args + [0xF7])
        Path(argv[-1]).write_bytes(out)
        print(f"wrote {argv[-1]}: {sum(1 for _ in frames(img))} frames, {len(out)} bytes")
        return
    link = Link()
    if argv[0] == "info":
        info(link)
    else:
        upload(link, argv[1] if len(argv) > 1 else DEFAULT_IMAGE)


if __name__ == "__main__":
    main(sys.argv[1:])
