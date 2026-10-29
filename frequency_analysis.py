#!/usr/bin/env python3
"""
Detect 50/60 Hz mains noise in time-ordered ADC data (time top to bottom).

    python detect_50hz.py data.csv                # auto: use timestamps if usable
    python detect_50hz.py data.csv --fs 860       # force a sample rate (rows/s)
    python detect_50hz.py data.csv --mains 60 --plot

Columns: 1 column = values only; 2+ columns = time(s), value (last column used).
Header/non-numeric lines are skipped.

The sample rate is only trusted from timestamps if they are fine-grained.
Whole-second timestamps (many rows share one value) cannot give a rate, so the
script falls back to --fs and ALSO scans the whole spectrum for the strongest
tone, printing the sample rate that tone implies for 50 Hz and for 60 Hz.
"""
import argparse
import numpy as np


def load(path):
    rows = []
    for line in open(path):
        p = [s for s in line.replace(";", ",").replace("\t", ",").replace(" ", ",").split(",") if s.strip()]
        try:
            rows.append([float(s) for s in p])
        except ValueError:
            continue
    if not rows:
        raise SystemExit("No numeric data found.")
    return np.array(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--fs", type=float, default=860.0, help="rows/s, used if timestamps are unusable")
    ap.add_argument("--mains", type=float, default=50.0)
    ap.add_argument("--plot", action="store_true")
    a = ap.parse_args()

    d = load(a.file)
    x = d[:, -1]
    n = len(x)
    fs, how = a.fs, "from --fs (default)"
    if d.shape[1] >= 2:
        t = d[:, 0]
        span = t[-1] - t[0]
        unique_ts = len(np.unique(t))
        # usable only if time really advances and timestamps are finer than ~1 s
        if span > 0 and unique_ts > n / 3:
            fs, how = (n - 1) / span, "from timestamps"
        else:
            print(f"WARNING: timestamps unusable (span {span:g} s, {unique_ts} unique values "
                  f"for {n} rows). Using --fs {a.fs:g}.")
    print(f"Sample rate: {fs:.2f} Hz ({how})")
    print(f"Samples: {n} ({n / fs:.2f} s), resolution {fs / n:.3f} Hz/bin")

    y = x - x.mean()
    w = np.hanning(n)
    amp = 2 * np.abs(np.fft.rfft(y * w)) / w.sum()
    f = np.fft.rfftfreq(n, 1 / fs)

    # 1) look near mains
    band = (f > a.mains - 5) & (f < a.mains + 5)
    i = np.argmax(amp * band)
    ref = (f > 20) & (f < 80) & (np.abs(f - f[i]) > 3)
    floor = np.median(amp[ref]) if ref.any() else np.nan
    ratio = amp[i] / floor
    print(f"\nNear {a.mains:g} Hz: peak {f[i]:.2f} Hz, amp {amp[i]:.3g}, floor {floor:.3g}, "
          f"ratio {ratio:.1f}x ({20 * np.log10(ratio):.1f} dB)")
    print("=> mains noise DETECTED" if ratio > 5 else "=> no clear mains peak at this sample rate")

    # 2) scan everything (skip lowest bins = drift) for the strongest tone
    lo = f > 3
    j = np.argmax(amp * lo)
    gfloor = np.median(amp[lo])
    print(f"\nStrongest tone overall: {f[j]:.2f} Hz, amp {amp[j]:.3g} ({amp[j] / gfloor:.1f}x median)")
    if amp[j] / gfloor > 5:
        cps = f[j] / fs  # cycles per row, independent of fs
        print(f"  = {cps:.5f} cycles/row -> if it is 50 Hz, rows/s = {50 / cps:.1f}; "
              f"if 60 Hz, rows/s = {60 / cps:.1f}")
        print("  (compare with your ADC's real data rate to decide which)")
    print(f"Quantization check: std = {y.std():.3g}, distinct values = {len(np.unique(x))}")

    if a.plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        plt.figure(figsize=(9, 4))
        plt.semilogy(f, amp + 1e-30)
        plt.axvline(a.mains, color="r", ls="--", alpha=0.5)
        plt.xlabel("Frequency (Hz)")
        plt.ylabel("Amplitude")
        plt.grid(True, which="both", alpha=0.3)
        plt.tight_layout()
        plt.savefig("spectrum.png", dpi=120)
        print("Saved spectrum.png")


if __name__ == "__main__":
    main()