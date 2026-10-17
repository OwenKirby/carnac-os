# FM1 recovery (Windows)

Used for real on 2026-10-15, when a Carnac build corrupted its own RAM at start-up and looped through a crash every 31 s without answering USB. This puts M-VAVE's stock firmware (V15) back. From stock, any firmware installs normally again (`x0x-firmware/tools/fm1_install.py`).

Everything it needs is in `build/fm1-rescue/`:

| File | What | Check |
| --- | --- | --- |
| `FM-1.fwsc` | M-VAVE FM-1 V15 | sha256 `db1642b2…edb8a` (the script refuses any other file) |
| `wl82loader.bin` | JieLi's flash loader (kagaimiq/jl-uboot-tool) | sha256 `d41da612…5376` |
| `zadig.exe` | Zadig 2.9, USB driver installer | signed by Akeo Consulting |
| `rescue_win.py` | runs X0X's `tools/fm1_rescue.py` on Windows | |

The script needs `pip install pyusb libusb-package` (already installed on this PC).

## Steps

1. **Driver (once per PC, already done here).** Run `zadig.exe`, then Device → Create New Device. Name it `FM1 UBOOT`, enter USB ID `4C4A` `8057`, choose **WinUSB**, and click Install Driver.
2. **Start the script** in `build/fm1-rescue`:
   ```
   python rescue_win.py FM-1.fwsc --loader wl82loader.bin --write --wait 900
   ```
   Run it yourself: Claude Code's safety check does not let Claude run this flash-writing tool.
3. **Put the FM1 in the chip's update mode:** hold **OCT-** and **OCT+** together for 5 s on a running firmware. On the broken build, holding them while its calibration screen timed out was enough. The script finds the chip and checks its key and flash. It saves a full backup of the flash next to `FM-1.fwsc`. Then it writes only the firmware sectors that differ from V15 (never the bootloader below 0x4000, nor the data above 0x93000), reads them back and restarts the FM1.

## Why the broken build needed this

`build_fm1.py --dsp-ram` moved `.c1_text` to the top of RAM. X0X's start-up copies `.ram_text` up to `_rt_end` (= `_c1_end`), so it copied about 430 KB of flash over `.data`, `.bss` and `.pool`. Every variable started as 0xFF:
- the button state kept phantom high bits, so HARDWARE CALIBRATION waited out its 30 s;
- the main loop then decoded a "4 GB" SysEx frame (`sx_frame_len` = 0xFFFFFFFF) and faulted, before it reached the M-UPGRADE check;
- the fault handler's own re-entry flag was 0xFF, so it reset at once, with no crash screen and nothing counted towards safe mode.

The OCT- + OCT+ check runs earlier in the main loop than the fault, which is why it still worked.

Since then, `src/hardware/fm1_preflight.py` runs on every build. It simulates `fm1_cstart`'s zeroing and copy loops on the built image and refuses to write a package unless `.data`, `.bss`, `.pool` and the RAM code come out exactly as expected. Run on the broken image, it fails on five counts.
