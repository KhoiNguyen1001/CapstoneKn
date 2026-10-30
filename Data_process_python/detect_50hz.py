#!/usr/bin/env python3
"""
Detect 50 Hz (mains) noise in time-ordered ADC data.

Usage:
    python detect_50hz.py data.csv                 # one column of values, fs=860
    python detect_50hz.py data.csv --fs 856.3      # use your measured rate
    python detect_50hz.py data.csv --mains 60      # look for 60 Hz instead

Input: text/CSV, time order top to bottom.
  - 1 column  -> values only (sample rate from --fs)
  - 2 columns -> time (seconds), value (sample rate estimated from time)
Header lines / non-numeric rows are skipped automatically.
"""
import argparse
import numpy as np


def load(path):
    rows = []
    with open(path) as f:
        for line in f:
            parts = line.replace(";", ",").replace("\t", ",").replace(" ", ",").split(",")
            parts = [p for p in parts if p.strip() != ""]
            try:
                rows.append([float(p) for p in parts])
            except ValueError:
                continue  # header or junk line
    if not rows:
        raise SystemExit("No numeric data found.")
    return np.array(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--fs", type=float, default=860.0, help="sample rate in Hz")
    ap.add_argument("--mains", type=float, default=50.0)
    ap.add_argument("--plot", action="store_true", help="save spectrum.png")
    a = ap.parse_args()

    d = load(a.file)
    if d.shape[1] >= 2:
        t, x = d[:, 0], d[:, 1]
        fs = (len(t) - 1) / (t[-1] - t[0])
        print(f"Sample rate estimated from timestamps: {fs:.2f} Hz")
    else:
        x = d[:, 0]
        fs = a.fs
        print(f"Using sample rate: {fs:.2f} Hz")

    n = len(x)
    print(f"Samples: {n}  ({n / fs:.2f} s),  resolution: {fs / n:.3f} Hz/bin")
    if n < fs:
        print("Warning: under 1 s of data; frequency resolution is coarse.")

    dc_voltage = np.mean(x)
    noise = x - dc_voltage
    noise_rms = np.sqrt(np.mean(noise**2))
    snr_dc = 20 * np.log10(abs(dc_voltage) / noise_rms)
    print("\nDC Measurement Quality:")
    print(f"DC voltage: {dc_voltage:.6f}")
    print(f"Noise RMS:  {noise_rms:.6g}")
    print(f"SNR:        {snr_dc:.2f} dB")

    x = x - np.mean(x)
    w = np.hanning(n)
    spec = np.fft.rfft(x * w)
    freq = np.fft.rfftfreq(n, 1 / fs)
    amp = 2 * np.abs(spec) / np.sum(w)  # amplitude in data units

    # Search +-5 Hz around mains (sample-clock error can shift the line)
    band = (freq > a.mains - 5) & (freq < a.mains + 5)
    i = np.argmax(amp * band)
    peak_f, peak_a = freq[i], amp[i]

    # Noise floor: median amplitude in 20-80 Hz excluding +-3 Hz around peak
    ref = (freq > 20) & (freq < 80) & (np.abs(freq - peak_f) > 3)
    floor = np.median(amp[ref]) if ref.any() else np.nan
    snr = peak_a / floor

    print(f"\nPeak near {a.mains:g} Hz: {peak_f:.2f} Hz, amplitude {peak_a:.4g} (data units)")
    print(f"Local noise floor (median): {floor:.4g}")
    print(f"Peak / floor: {snr:.1f}x ({20 * np.log10(snr):.1f} dB)")
    print("=> mains noise DETECTED" if snr > 5 else "=> no clear mains peak")

    print("\nHarmonics:")
    for k in range(2, 6):
        fk = k * peak_f
        if fk >= fs / 2:
            break
        m = (freq > fk - 2) & (freq < fk + 2)
        j = np.argmax(amp * m)
        print(f"  {k}x ~ {freq[j]:.1f} Hz: {amp[j]:.4g}")

    if a.plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        plt.figure(figsize=(9, 4))
        plt.semilogy(freq, amp + 1e-30)
        plt.axvline(a.mains, color="r", ls="--", alpha=0.5)
        plt.xlabel("Frequency (Hz)")
        plt.ylabel("Amplitude")
        plt.grid(True, which="both", alpha=0.3)
        plt.tight_layout()
        plt.savefig("spectrum1.png", dpi=120)
        print("\nSaved spectrum.png")


if __name__ == "__main__":
    main()