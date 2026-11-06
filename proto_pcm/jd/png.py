import numpy as np, zlib, struct
def write_png(path, img):           # img: HxW uint8 gray or HxWx3
    img = np.ascontiguousarray(img.astype(np.uint8))
    h, w = img.shape[:2]; c = 1 if img.ndim == 2 else 3
    raw = b"".join(b"\x00" + img[y].tobytes() for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0 if c == 1 else 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
def spec(x, sr, t0, t1, px_s=50, fmin=60, fmax=11000, H=300, nfft=2048):
    hop = int(sr / px_s); a = int(t0 * sr); b = int(t1 * sr)
    seg = x[a:b]; W = (len(seg) - nfft) // hop
    win = np.hanning(nfft)
    frames = np.lib.stride_tricks.sliding_window_view(seg, nfft)[::hop][:W] * win
    S = 20 * np.log10(np.abs(np.fft.rfft(frames, axis=1)) + 1e-7)
    f = np.fft.rfftfreq(nfft, 1 / sr)
    fy = fmin * (fmax / fmin) ** (np.arange(H)[::-1] / (H - 1))
    img = np.stack([np.interp(fy, f, s) for s in S], axis=1)
    ref = np.percentile(img, 99.7); img = np.clip((img - ref + 75) / 75, 0, 1) ** 1.3 * 255
    rgb = np.repeat(img[..., None], 3, axis=2)
    for s in range(int(np.ceil(t0)), int(t1)):       # second ticks (red), 5 s (yellow)
        col = int((s - t0) * px_s)
        if 0 <= col < rgb.shape[1]:
            rgb[:12 if s % 5 else 30, col] = [255, 60, 60] if s % 5 else [255, 220, 0]
    for fr in (100, 1000, 10000):                      # frequency guides (blue)
        row = int((H - 1) * (1 - np.log(fr / fmin) / np.log(fmax / fmin)))
        rgb[row, ::6] = [80, 140, 255]
    return rgb
