#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure how well the FM1 follows an external MIDI clock (Windows Python with mido + python-rtmidi + numpy).

The firmware's clock probe (midi_clock.h MidiClockLog, SysEx ED_SYNC 48) logs, for up to 1024 pulses, the frame each
received 0xF8 reached the clock at, the frame each pulse the clock played landed on, and the FM1's timer at each
0xF8's read. This tool either sends the clock itself, so the musically intended time of every pulse is known, or
reads the log after any other source (a DAW, a drum machine on the TRS jack) played into the FM1.

  fm1_clock.py send [options]     the PC is the clock: arm the probe, send Start + clock, read back, score
      --bpm 120 --beats 32        tempo and length (at most 42 beats: 1024 pulses)
      --model clean               clean     each pulse sent on time (busy-wait)
                                  daw256 / daw512 / daw1024
                                            sent on the next audio-buffer boundary at 48 kHz, as a DAW without
                                            timestamped MIDI out does (the usual Windows case)
                                  usb2      +0..2 ms random per pulse
                                  stall     clean, but every 2 s a 40 ms hiccup (the pulses queued, then sent at once)
      --ramp 140                  tempo ramps linearly from --bpm to this over the run
      --jump 150                  tempo jumps to this halfway through the run
      --pre 2                     clock running this many beats before Start (Bitwig / hardware style); 0 = Start
                                  then clock (Ableton style)
      --nostart                   no Start / Stop (the sequencers stay stopped: the clock path alone, no voice load)
      --save run.json             keep the raw log (for `replay`)
  fm1_clock.py arm                arm the probe and let go of the port: play your DAW into the FM1, then `read`
  fm1_clock.py read [--save f]    read and score whatever the probe holds (no intended times: scored against a
                                  local fit of the received pulses)
  fm1_clock.py show run.json      score a saved run again
  fm1_clock.py sim [send options] [--all]
                                  no FM1: the same run made up on the PC (the model's send times, USB's 1 ms frames,
                                  the 0.5 ms poll) through the host build; --all: every model at 90 / 120 / 174 BPM
  fm1_clock.py replay run.json [--exe clock_replay.exe]
                                  feed the run's received frames through the host build of midi_clock.c
                                  (src/host/clock_replay.c) and score what it plays next to what the FM1 played

Scores (ms; "err" = distance from where the pulse should be, after taking out a constant latency and the
two clocks' rate difference by a straight-line fit):
  sent     the PC's own send times against the intended ones (the model's jitter, on purpose)
  read     the FM1's read times against the send times (Windows + USB + the 0.5 ms poll)
  in       the frames the clock was handed against the intended times
  out      the frames the clock played against the intended times: what you hear. After the first beat.
  1/16     every 1/16 step's length against the intended (6 pulses): what sounds like swing or flamming
  beat1    the first beat after Start (the lock still measuring)
"""
import json
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
HDR = [0x7D, 0x46, 0x4C]
ED_SYNC = 48
NLOG = 1024
FS_NOM = 44117.647   # the FM1's I2S rate (frames are counted at this; the fit takes out the rest)


# ------------------------------------------------------------------------------------------------------- device ---
class Fm1:
    def __init__(self):
        import mido
        name = lambda ns: next((n for n in ns if "FM-1" in n or "X0X" in n), None)
        ni, no = name(mido.get_input_names()), name(mido.get_output_names())
        if not ni or not no:
            sys.exit("fm1_clock: no FM-1 MIDI port (plugged in? another program holding it?)")
        self.mido = mido
        self.i = mido.open_input(ni)
        self.o = mido.open_output(no)

    def close(self):
        self.i.close()
        self.o.close()

    def ask(self, args, timeout=1.0, tries=4):
        for _ in range(tries):
            while self.i.poll():
                pass
            self.o.send(self.mido.Message("sysex", data=HDR + [ED_SYNC] + args))
            end = time.time() + timeout
            while time.time() < end:
                m = self.i.poll()
                if m is None:
                    time.sleep(0.001)
                    continue
                if m.type == "sysex" and list(m.data[:4]) == HDR + [ED_SYNC]:
                    return list(m.data[4:])
        sys.exit("fm1_clock: no reply to the clock probe (firmware without it?)")

    def status(self):
        b = self.ask([0])
        v = [sum(b[i * 5 + k] << (7 * k) for k in range(5)) for i in range(len(b) // 5)]
        keys = ["armed", "nrx", "nplay", "nstamp", "nev", "relocks", "clipped", "owed", "acquires", "period_q8",
                "force", "external", "running", "ticks_us", "slips"]
        return dict(zip(keys, v))

    def health(self):
        """late halves since boot, peak load % (ED_STATS)"""
        while self.i.poll():
            pass
        self.o.send(self.mido.Message("sysex", data=HDR + [46]))
        end = time.time() + 2.0
        while time.time() < end:
            m = self.i.poll()
            if m is None:
                time.sleep(0.001)
                continue
            if m.type == "sysex" and list(m.data[:4]) == HDR + [46]:
                b = list(m.data[4:])
                v = [sum(b[i * 5 + k] << (7 * k) for k in range(5)) for i in range(len(b) // 5)]
                return v[1], v[4]
        return 0, 0

    def arm(self, force):
        self.ask([1, 1 if force else 0])

    def disarm(self):
        self.ask([3])

    def table(self, which, n):
        out, step = [], 16 if which == 3 else 32
        for i in range(0, n, step):
            b = self.ask([2, which, i & 127, i >> 7])[1:]
            v = [sum(b[j * 5 + k] << (7 * k) for k in range(5)) for j in range(len(b) // 5)]
            out += v
        return out[: 2 * n] if which == 3 else out[:n]

    def log(self):
        st = self.status()
        ev = self.table(3, st["nev"]) if st["nev"] else []
        return dict(status=st, rx=self.table(0, st["nrx"]), play=self.table(1, st["nplay"]),
                    stamp=self.table(2, st["nstamp"]),
                    events=[dict(frame=ev[2 * j], status=ev[2 * j + 1] & 0xFF, rx=ev[2 * j + 1] >> 8)
                            for j in range(len(ev) // 2)])


def unwrap(v, bits=32):
    """frame / tick counters wrap at 2^32 (frames: 27 h; ticks: 3 min): make them increase"""
    a = np.array(v, dtype=np.float64)
    if len(a) > 1:
        d = np.diff(a)
        d[d < -(2 ** (bits - 1))] += 2 ** bits
        a = np.concatenate([[a[0]], a[0] + np.cumsum(d)])
    return a


# ------------------------------------------------------------------------------------------------------ sending ---
def schedule(bpm, beats, ramp, pre, jump=0.0):
    """the intended time (s) of every pulse from the first: pre-roll pulses, then the run's; and the Start's index"""
    n_pre, n = int(round(pre * 24)), int(round(beats * 24))
    t, out, b = 0.0, [], bpm
    for k in range(n_pre + n):
        out.append(t)
        if k >= n_pre and ramp:
            b = bpm + (ramp - bpm) * (k - n_pre) / max(1, n - 1)
        if jump and k == n_pre + n // 2:
            b = jump
        t += 60.0 / (b * 24.0)
    return out, n_pre


def model_time(model, t, rng, stall_at):
    if model == "clean":
        return t
    if model.startswith("daw"):
        buf = int(model[3:]) / 48000.0
        return (np.floor(t / buf) + 1.0) * buf
    if model == "usb2":
        return t + rng.uniform(0.0, 0.002)
    if model == "stall":
        k = np.floor(t / 2.0)
        return max(t, k * 2.0 + stall_at) if (t - k * 2.0) < stall_at else t
    sys.exit(f"fm1_clock: no model {model}")


def send_run(dev, bpm, beats, model, ramp, pre, nostart=False, jump=0.0):
    ideal, n_pre = schedule(bpm, beats, ramp, pre, jump)
    if len(ideal) > NLOG:
        sys.exit(f"fm1_clock: {len(ideal)} pulses, the probe holds {NLOG}")
    rng = np.random.default_rng(1)
    clk, start, stop = (dev.mido.Message.from_bytes([b]) for b in (0xF8, 0xFA, 0xFC))
    if not nostart:
        dev.o.send(stop)
    time.sleep(0.3)
    health0 = dev.health()
    dev.arm(force=True)
    time.sleep(0.05)
    lead = 0.25                                        # the first pulse this long after now
    t0 = time.perf_counter() + lead
    sent, want = [], [t0 + model_time(model, t, rng, 0.040) for t in ideal]
    order = np.argsort(np.array(want), kind="stable")  # (the stall model sends a queue at once, in order)
    start_at = t0 + (ideal[n_pre] - 0.0005 if n_pre else -0.002)
    started = False
    for k in order:
        if not started and want[k] >= start_at:
            spin_until(start_at)
            if not nostart:
                dev.o.send(start)
            started = True
        spin_until(want[k])
        dev.o.send(clk)
        sent.append(time.perf_counter() - t0)
    time.sleep(0.15)
    if not nostart:
        dev.o.send(stop)
    time.sleep(0.2)
    log = dev.log()
    dev.disarm()
    health1 = dev.health()
    log.update(ideal=ideal, sent=sent, n_pre=n_pre, late=health1[0] - health0[0], peak=health1[1],
               run=dict(bpm=bpm, beats=beats, model=model, ramp=ramp, pre=pre, nostart=nostart, jump=jump))
    return log


def spin_until(t):
    while True:
        d = t - time.perf_counter()
        if d <= 0:
            return
        if d > 0.003:
            time.sleep(d - 0.002)


# ------------------------------------------------------------------------------------------------------ scoring ---
def fit_err(x, y):
    """y against x by a straight line; the residuals"""
    A = np.vstack([x, np.ones_like(x)]).T
    c, *_ = np.linalg.lstsq(A, y, rcond=None)
    return y - A @ c, c


def local_ref(y, half=24):
    """no intended times: each pulse's place from a straight line through its neighbours (+-1 beat)"""
    n, out = len(y), np.zeros(len(y))
    k = np.arange(n, dtype=np.float64)
    for i in range(n):
        a, b = max(0, i - half), min(n, i + half + 1)
        c = np.polyfit(k[a:b], y[a:b], 1)
        out[i] = c[0] * i + c[1]
    return out


def stats(e_ms):
    e = np.abs(np.asarray(e_ms))
    if len(e) == 0:
        return "-"
    return "rms %5.2f  p99 %5.2f  max %5.2f" % (np.sqrt(np.mean(e ** 2)), np.percentile(e, 99), e.max())


def slips_of(log):
    """the lost audio halves: the sim's list, or the device's slip events (status 0x10 | halves)"""
    if log.get("slips"):
        return log["slips"]
    return [[e["frame"], (e["status"] & 15) * 512] for e in log.get("events", []) if e["status"] & 0xF0 == 0x10]


def to_wall(log, frames):
    """sample frames to real-time frames: every half the render lost put back"""
    f = np.array(frames, dtype=np.float64)
    w = f.copy()
    for at, n in slips_of(log):
        w[f >= at] += n
    return w


def pair(log, played):
    """the intended times, received and played frames from the Start on (the run's pulse 0), paired by count; pulses
    played before the Start (pre-roll, or played ahead of one that came after it) are not the run's"""
    rx, pl = to_wall(log, unwrap(log["rx"])), to_wall(log, unwrap(played))
    n_pre = log.get("n_pre", 0)
    fa = [e for e in log.get("events", []) if e["status"] == 0xFA]
    ideal = log.get("ideal")
    if fa and not log.get("run", {}).get("nostart"):
        fs = to_wall(log, [fa[0]["frame"]])[0]
        pl = pl[pl >= fs]
    else:
        n_pre = 0
    rx = rx[n_pre:]
    I = np.array(ideal[n_pre:]) if ideal is not None else None
    S = np.array(log["sent"][n_pre:]) if ideal is not None else None
    n = min(len(rx), len(pl))
    return (I[:n] if I is not None else None), (S[:n] if S is not None else None), rx[:n], pl[:n], n_pre


def score(log, played=None, label="FM1"):
    st = log["status"]
    rx = unwrap(log["rx"])
    pl = unwrap(log["play"] if played is None else played)
    fs = FS_NOM
    lines = []
    run = log.get("run")
    if run:
        lines.append("run: %s BPM%s, %s beats, model %s, pre-roll %s beats" %
                     (run["bpm"], " -> %s" % run["ramp"] if run["ramp"] else (" jump to %s" % run["jump"]
                      if run.get("jump") else ""), run["beats"], run["model"],
                      run["pre"]) + (", no Start" if run.get("nostart") else ""))
    if "late" in log:
        lines.append("audio: %d late halves during the run%s" % (log["late"], "  <- the FM1 overloaded: every late "
                     "half moves its frame count 11.6 ms against real time" if log["late"] else ""))
    lines.append("%s: received %d, played %d; relocks %d, clipped %d, owed %d, runs locked %d" %
                 (label, len(rx), len(pl), st["relocks"], st["clipped"], st["owed"], st["acquires"]) if played is None
                 else "%s: received %d, played %d" % (label, len(rx), len(pl)))
    I, S, rx, pl, n_pre = pair(log, log["play"] if played is None else played)
    n = len(pl)
    if n < 48:
        lines.append("too few pulses to score")
        return "\n".join(lines)
    beat1 = slice(0, 24)
    after = slice(24, n)
    if I is not None:
        e_sent, _ = fit_err(I, S)
        lines.append("  sent   %s   (the model, on purpose)" % stats(e_sent * 1e3))
        if len(log["stamp"]) >= n + n_pre:
            T = unwrap(log["stamp"])[n_pre:n_pre + n] / (st["ticks_us"] or 24) / 1e6
            e_read, _ = fit_err(S, T)
            lines.append("  read   %s   (Windows + USB + poll)" % stats(e_read * 1e3))
        e_in, _ = fit_err(I, rx / fs)
        e_out, c = fit_err(I, pl / fs)
        lines.append("  in     %s" % stats(e_in[after] * 1e3))
        lines.append("  out    %s   <- what you hear" % stats(e_out[after] * 1e3))
        sixth = (pl[6:] - pl[:-6]) / fs / c[0] - (I[6:] - I[:-6])
        lines.append("  1/16   %s" % stats(sixth[24:] * 1e3))
        lines.append("  beat1  %s" % stats(e_out[beat1] * 1e3))
        lat = np.mean((pl - rx)[after]) / fs * 1e3
        bpm = 60.0 / (24.0 * np.median(np.diff(pl[after])) / fs) * (fs / FS_NOM)
        lines.append("  lock delay %.2f ms (played after received, on top of the fixed 23.2 ms in); tempo %.2f BPM"
                     % (lat, bpm))
    else:
        ref_rx = local_ref(rx)
        ref = local_ref(pl)
        e_in, e_out = (rx - ref_rx) / fs, (pl - ref) / fs
        lines.append("  in     %s   (against a +-1 beat fit)" % stats(e_in[24:] * 1e3))
        lines.append("  out    %s   (against a +-1 beat fit)" % stats(e_out[24:] * 1e3))
        sixth = (pl[6:] - pl[:-6]) - (ref[6:] - ref[:-6])
        lines.append("  1/16   %s" % stats(sixth[24:] / fs * 1e3))
        bpm = 60.0 / (24.0 * np.median(np.diff(pl)) / fs)
        lines.append("  tempo %.2f BPM" % bpm)
    for ev in log.get("events", []):
        if ev["status"] & 0xF0 == 0x10:
            lines.append("  slip: %d audio half(s) lost after %d pulses" % (ev["status"] & 15, ev["rx"]))
        else:
            lines.append("  event %02X after %d pulses" % (ev["status"], ev["rx"]))
    return "\n".join(lines)


def replay(log, exe):
    """the received frames (and the transport events in their places) through the host build; its played frames"""
    rx = [int(x) for x in unwrap(log["rx"])]
    base = rx[0] - 4096 if rx else 0
    items = [(f - base, 1, k, "p %d" % (f - base)) for k, f in enumerate(rx)]
    for ev in log.get("events", []):
        if ev["status"] >= 0xF8:
            items.append((max(0, int(ev["frame"]) - base), 0, ev["rx"] - 0.5, "e %d %d" % (max(0, int(ev["frame"]) - base),
                                                                                      ev["status"])))
    for at, n in slips_of(log):
        items.append((int(at) - base, -1, -1, "s %d %d" % (int(at) - base, n)))
    items.sort(key=lambda t: (t[2] if t[1] >= 0 else -1, t[0]) if False else (t[0], t[1]))
    lines = [t[3] for t in items]
    r = subprocess.run([str(exe)], input="\n".join(lines) + "\n", capture_output=True, text=True)
    if r.returncode:
        sys.exit("fm1_clock: replay failed: " + r.stderr)
    return [int(x) + base for x in r.stdout.split()]


def sim_log(bpm, beats, model, ramp, pre, seed=1, slips=0.0, jump=0.0):
    """a made-up capture: what the FM1 would have received for this run"""
    ideal, n_pre = schedule(bpm, beats, ramp, pre, jump)
    rng = np.random.default_rng(seed)
    t0 = 0.1 + rng.uniform(0, 0.01)
    sent = sorted(t0 + model_time(model, t, rng, 0.040) for t in ideal)
    usb = [np.ceil((s + rng.uniform(0.0002, 0.0006)) / 0.001) * 0.001 for s in sent]   # the next USB frame
    rd = [np.ceil(u / 0.0005) * 0.0005 + rng.uniform(0, 0.00002) for u in usb]          # the 2 kHz poll
    rd = list(np.maximum.accumulate(rd))                                                # (one link: in order)
    wall = [int(r * FS_NOM) + 50000 for r in rd]
    st = dict(relocks=0, clipped=0, owed=0, acquires=0, ticks_us=24)
    out_slips = []                                     # overload: `slips` lost halves a second, at random
    if slips:
        t, lost, end = 0.0, 0, rd[-1] + 0.5
        while True:
            t += rng.exponential(1.0 / slips)
            if t >= end:
                break
            w = int(t * FS_NOM) + 50000
            out_slips.append([w - lost - 512, 512])    # the render's own frame there (it fell behind by `lost`)
            lost += 512
    rx = []
    for w in wall:
        lost, f = 0, None
        for at, n in out_slips:
            if at + lost + n <= w:
                lost += n
            elif at + lost <= w:
                f = at                                 # came while that half was lost: its first frame after
                break
        rx.append(f if f is not None else w - lost)
    events = [dict(frame=rx[n_pre] - 1, status=0xFA, rx=n_pre)]
    return dict(status=st, rx=rx, play=[], stamp=[int(r * 24e6) for r in rd], events=events, ideal=ideal,
                sent=[x - t0 for x in sent], n_pre=n_pre, slips=out_slips,
                run=dict(bpm=bpm, beats=beats, model=model, ramp=ramp, pre=pre, jump=jump))


def score_line(log, played):
    """one line: out rms / max, 1/16 max, beat1 max (ms), received - played (from the Start)"""
    I, S, rx, pl, n_pre = pair(log, played)
    fa = [e for e in log.get("events", []) if e["status"] == 0xFA]
    n_played = (int(np.sum(to_wall(log, unwrap(played)) >= to_wall(log, [fa[0]["frame"]])[0]))
                if fa and not log.get("run", {}).get("nostart") else len(played))
    pl = pl / FS_NOM
    e, c = fit_err(I, pl)
    six = (pl[6:] - pl[:-6]) / c[0] - (I[6:] - I[:-6])
    return "out rms %5.2f max %5.2f | 1/16 max %5.2f | beat1 max %5.2f | rx-played %+d" % (
        np.sqrt(np.mean(e[24:] ** 2)) * 1e3, np.abs(e[24:]).max() * 1e3, np.abs(six[24:]).max() * 1e3,
        np.abs(e[:24]).max() * 1e3, len(log["rx"]) - n_pre - n_played)


def main():
    a = sys.argv[1:]
    opt = lambda k, d: type(d)(a[a.index(k) + 1]) if k in a else d
    if not a or a[0] not in ("send", "arm", "read", "show", "replay", "sim"):
        sys.exit(__doc__)
    exe = Path(opt("--exe", str(REPO / "build" / "clock_replay.exe")))
    if a[0] == "sim":
        if "--all" in a:
            for model in ("clean", "usb2", "daw256", "daw512", "daw1024", "stall"):
                for bpm in (90.0, 120.0, 174.0):
                    for pre in (0.0, 2.0):
                        log = sim_log(bpm, 40.0 if pre == 0 else 38.0, model, 0.0, pre)
                        print("%-8s %5.0f pre %d  %s" % (model, bpm, pre, score_line(log, replay(log, exe))))
            for bpm, to in ((120.0, 140.0), (140.0, 100.0)):
                log = sim_log(bpm, 40.0, "daw512", to, 0.0)
                print("%-8s %3.0f>%3.0f pre 0  %s" % ("ramp512", bpm, to, score_line(log, replay(log, exe))))
            return 0
        log = sim_log(opt("--bpm", 120.0), opt("--beats", 32.0), opt("--model", "clean"), opt("--ramp", 0.0),
                      opt("--pre", 0.0), slips=opt("--slips", 0.0), jump=opt("--jump", 0.0))
        print(score(log, replay(log, exe), "host"))
        return 0
    if a[0] in ("show", "replay"):
        log = json.loads(Path(a[1]).read_text())
        print(score(log))
        if a[0] == "replay":
            print(score(log, replay(log, exe), "host"))
        return 0
    dev = Fm1()
    try:
        if a[0] == "arm":
            dev.arm(force=False)
            print("probe armed: play the clock into the FM1 (CLOCK SOURCE = MIDI), then `fm1_clock.py read`")
            return 0
        if a[0] == "send":
            log = send_run(dev, opt("--bpm", 120.0), opt("--beats", 32.0), opt("--model", "clean"),
                           opt("--ramp", 0.0), opt("--pre", 0.0), "--nostart" in a, opt("--jump", 0.0))
        else:
            log = dev.log()
    finally:
        dev.close()
    if "--save" in a:
        Path(opt("--save", "")).write_text(json.dumps(log))
    print(score(log))
    return 0


if __name__ == "__main__":
    sys.exit(main())
