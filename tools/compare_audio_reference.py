#!/usr/bin/env python3
"""Compare a port audio dump against an emulator/hardware reference capture.

The existing compare_audio.py is a build-to-build regression guard. This tool is
for fidelity checks where the reference may have capture latency or a different
sample rate. It aligns by amplitude envelope, then compares broad frequency-band
energy rather than raw sample equality.
"""

import argparse
import json
import math
import os
import struct
import sys
import wave


BANDS_HZ = (20, 80, 160, 320, 640, 1280, 2560, 5120, 10000)
EPS = 1.0e-12


def dbfs_from_rms(value):
    if value <= 0:
        return -120.0
    return 20.0 * math.log10(value / 32768.0)


def db_delta(test, ref):
    return 10.0 * math.log10((test + EPS) / (ref + EPS))


def load_wav(path):
    with wave.open(path, "rb") as wf:
        channels = wf.getnchannels()
        rate = wf.getframerate()
        width = wf.getsampwidth()
        frames = wf.getnframes()
        raw = wf.readframes(frames)

    if channels <= 0:
        raise ValueError(f"{path}: invalid channel count {channels}")
    if width not in (1, 2, 3, 4):
        raise ValueError(f"{path}: unsupported PCM sample width {width}")

    samples = []
    stride = channels * width
    for off in range(0, len(raw) - stride + 1, stride):
        total = 0.0
        for ch in range(channels):
            p = off + ch * width
            if width == 1:
                sample = (raw[p] - 128) << 8
            elif width == 2:
                sample = struct.unpack_from("<h", raw, p)[0]
            elif width == 3:
                b0, b1, b2 = raw[p], raw[p + 1], raw[p + 2]
                sample = b0 | (b1 << 8) | (b2 << 16)
                if sample & 0x800000:
                    sample -= 0x1000000
                sample >>= 8
            else:
                sample = struct.unpack_from("<i", raw, p)[0] >> 16
            total += sample
        samples.append(total / channels)

    return samples, rate


def load_raw_s16le(path, rate, channels):
    if channels <= 0:
        raise ValueError("raw channel count must be positive")
    with open(path, "rb") as f:
        raw = f.read()
    usable = (len(raw) // (2 * channels)) * (2 * channels)
    if usable == 0:
        return [], rate
    ints = struct.unpack(f"<{usable // 2}h", raw[:usable])
    samples = []
    for i in range(0, len(ints), channels):
        samples.append(sum(ints[i:i + channels]) / channels)
    return samples, rate


def detect_format(path, requested):
    if requested != "auto":
        return requested
    try:
        with open(path, "rb") as f:
            header = f.read(12)
        if header[:4] == b"RIFF" and header[8:12] == b"WAVE":
            return "wav"
    except OSError:
        pass
    return "raw"


def load_audio(path, fmt, raw_rate, raw_channels):
    fmt = detect_format(path, fmt)
    if fmt == "wav":
        return load_wav(path)
    if fmt == "raw":
        return load_raw_s16le(path, raw_rate, raw_channels)
    raise ValueError(f"{path}: unknown format {fmt}")


def slice_seconds(samples, rate, start, duration):
    start_i = max(0, int(round(start * rate)))
    if duration is None:
        end_i = len(samples)
    else:
        end_i = min(len(samples), start_i + int(round(duration * rate)))
    return samples[start_i:end_i]


def resample_linear(samples, src_rate, dst_rate):
    if src_rate == dst_rate or not samples:
        return list(samples)
    ratio = src_rate / float(dst_rate)
    out_len = int(len(samples) / ratio)
    out = []
    for i in range(out_len):
        pos = i * ratio
        j = int(pos)
        frac = pos - j
        if j + 1 < len(samples):
            out.append(samples[j] * (1.0 - frac) + samples[j + 1] * frac)
        else:
            out.append(samples[-1])
    return out


def rms(samples):
    if not samples:
        return 0.0
    return math.sqrt(sum(s * s for s in samples) / len(samples))


def zero_crossing_rate(samples):
    if len(samples) < 2:
        return 0.0
    crossings = 0
    prev = samples[0] >= 0
    for sample in samples[1:]:
        sign = sample >= 0
        if sign != prev:
            crossings += 1
        prev = sign
    return crossings / float(len(samples))


def envelope(samples, window):
    if window <= 0:
        window = 1
    env = []
    for i in range(0, len(samples), window):
        chunk = samples[i:i + window]
        if chunk:
            env.append(sum(abs(s) for s in chunk) / len(chunk))
    return env


def normalized(values):
    if not values:
        return []
    mean = sum(values) / len(values)
    var = sum((v - mean) * (v - mean) for v in values) / len(values)
    std = math.sqrt(var)
    if std < EPS:
        return [0.0 for _ in values]
    return [(v - mean) / std for v in values]


def correlation_at(a, b, lag):
    if lag >= 0:
        a_start = 0
        b_start = lag
    else:
        a_start = -lag
        b_start = 0
    n = min(len(a) - a_start, len(b) - b_start)
    if n <= 2:
        return -1.0
    total = 0.0
    for i in range(n):
        total += a[a_start + i] * b[b_start + i]
    return total / n


def find_alignment(ref, test, rate, max_offset_seconds):
    window = max(1, int(round(rate * 0.05)))
    ref_env = normalized(envelope(ref, window))
    test_env = normalized(envelope(test, window))
    max_lag = int(round(max_offset_seconds * rate / window))
    best_lag = 0
    best_corr = -1.0
    for lag in range(-max_lag, max_lag + 1):
        corr = correlation_at(ref_env, test_env, lag)
        if corr > best_corr:
            best_corr = corr
            best_lag = lag
    return best_lag * window, best_corr


def crop_aligned(ref, test, lag_samples):
    if lag_samples >= 0:
        ref_start = 0
        test_start = lag_samples
    else:
        ref_start = -lag_samples
        test_start = 0
    n = min(len(ref) - ref_start, len(test) - test_start)
    if n <= 0:
        return [], []
    return ref[ref_start:ref_start + n], test[test_start:test_start + n]


def fft_inplace(values):
    n = len(values)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            values[i], values[j] = values[j], values[i]

    length = 2
    while length <= n:
        angle = -2.0 * math.pi / length
        wlen = complex(math.cos(angle), math.sin(angle))
        for i in range(0, n, length):
            w = 1.0 + 0.0j
            half = length // 2
            for j in range(i, i + half):
                u = values[j]
                v = values[j + half] * w
                values[j] = u + v
                values[j + half] = u - v
                w *= wlen
        length <<= 1


def band_indices(rate, fft_size):
    nyquist = rate / 2.0
    edges = [e for e in BANDS_HZ if e < nyquist]
    if not edges or edges[-1] < nyquist:
        edges.append(nyquist)
    indices = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        start = max(1, int(math.floor(lo * fft_size / rate)))
        end = min(fft_size // 2, int(math.ceil(hi * fft_size / rate)))
        if end <= start:
            end = start + 1
        indices.append((lo, hi, start, end))
    return indices


def spectral_bands(samples, rate, fft_size=2048, hop=1024):
    if len(samples) < fft_size:
        return [0.0 for _ in band_indices(rate, fft_size)]

    bands = band_indices(rate, fft_size)
    totals = [0.0 for _ in bands]
    frames = 0
    window = [
        0.5 - 0.5 * math.cos((2.0 * math.pi * i) / (fft_size - 1))
        for i in range(fft_size)
    ]

    for start in range(0, len(samples) - fft_size + 1, hop):
        frame = [
            complex((samples[start + i] / 32768.0) * window[i], 0.0)
            for i in range(fft_size)
        ]
        fft_inplace(frame)
        mags = [(frame[i].real * frame[i].real + frame[i].imag * frame[i].imag)
                for i in range(fft_size // 2 + 1)]
        for idx, (_lo, _hi, bin_start, bin_end) in enumerate(bands):
            totals[idx] += sum(mags[bin_start:bin_end]) / max(1, bin_end - bin_start)
        frames += 1

    if frames:
        totals = [v / frames for v in totals]
    return totals


def cosine_similarity(a, b):
    dot = sum(x * y for x, y in zip(a, b))
    aa = math.sqrt(sum(x * x for x in a))
    bb = math.sqrt(sum(y * y for y in b))
    if aa <= EPS or bb <= EPS:
        return 0.0
    return dot / (aa * bb)


def metric_summary(ref_aligned, test_aligned, rate):
    ref_rms = rms(ref_aligned)
    test_rms = rms(test_aligned)
    ref_bands = spectral_bands(ref_aligned, rate)
    test_bands = spectral_bands(test_aligned, rate)
    band_deltas = [db_delta(t, r) for r, t in zip(ref_bands, test_bands)]
    band_mae = sum(abs(v) for v in band_deltas) / len(band_deltas)
    spectral_cos = cosine_similarity(ref_bands, test_bands)
    high_signed_deltas = [
        delta
        for (lo, _hi, _start, _end), delta in zip(band_indices(rate, 2048), band_deltas)
        if lo >= 2560
    ]
    high_band_mae = (
        sum(abs(delta) for delta in high_signed_deltas) / len(high_signed_deltas)
        if high_signed_deltas
        else 0.0
    )
    high_band_delta = (
        sum(high_signed_deltas) / len(high_signed_deltas)
        if high_signed_deltas
        else 0.0
    )
    rms_delta_db = 20.0 * math.log10((test_rms + EPS) / (ref_rms + EPS))
    envelope_corr = correlation_at(
        normalized(envelope(ref_aligned, int(rate * 0.05))),
        normalized(envelope(test_aligned, int(rate * 0.05))),
        0,
    )

    return {
        "envelope_corr": envelope_corr,
        "reference_rms_dbfs": dbfs_from_rms(ref_rms),
        "test_rms_dbfs": dbfs_from_rms(test_rms),
        "rms_delta_db": rms_delta_db,
        "reference_zcr": zero_crossing_rate(ref_aligned),
        "test_zcr": zero_crossing_rate(test_aligned),
        "spectral_cosine": spectral_cos,
        "band_mae_db": band_mae,
        "high_band_mae_db": high_band_mae,
        "high_band_delta_db": high_band_delta,
        "bands": [
            {
                "lo_hz": lo,
                "hi_hz": hi,
                "reference_db": 10.0 * math.log10(ref_energy + EPS),
                "test_db": 10.0 * math.log10(test_energy + EPS),
                "delta_db": delta,
            }
            for (lo, hi, _start, _end), ref_energy, test_energy, delta
            in zip(band_indices(rate, 2048), ref_bands, test_bands, band_deltas)
        ],
    }


def segment_summaries(ref_aligned, test_aligned, rate, segment_seconds, hop_seconds):
    if segment_seconds <= 0:
        return []
    seg_len = int(round(segment_seconds * rate))
    hop_len = int(round((hop_seconds if hop_seconds > 0 else segment_seconds) * rate))
    if seg_len < rate:
        raise ValueError("segment window must be at least one second")
    if hop_len <= 0:
        raise ValueError("segment hop must be positive")

    total_len = min(len(ref_aligned), len(test_aligned))
    if total_len < seg_len:
        return []

    starts = list(range(0, total_len - seg_len + 1, hop_len))
    last_start = total_len - seg_len
    if starts[-1] != last_start:
        starts.append(last_start)

    segments = []
    for start in starts:
        end = start + seg_len
        summary = metric_summary(ref_aligned[start:end], test_aligned[start:end], rate)
        summary.update({
            "start_seconds": start / float(rate),
            "end_seconds": end / float(rate),
        })
        segments.append(summary)
    return segments


def compare(args):
    ref, ref_rate = load_audio(args.reference, args.reference_format,
                               args.reference_raw_rate, args.reference_raw_channels)
    test, test_rate = load_audio(args.test, args.test_format,
                                 args.test_raw_rate, args.test_raw_channels)

    ref = slice_seconds(ref, ref_rate, args.reference_start, args.duration)
    test = slice_seconds(test, test_rate, args.test_start, args.duration)
    ref = resample_linear(ref, ref_rate, args.target_rate)
    test = resample_linear(test, test_rate, args.target_rate)

    if len(ref) < args.target_rate or len(test) < args.target_rate:
        raise ValueError("need at least one second of reference and test audio")

    if args.no_align:
        lag_samples = 0
        align_corr = correlation_at(
            normalized(envelope(ref, int(args.target_rate * 0.05))),
            normalized(envelope(test, int(args.target_rate * 0.05))),
            0,
        )
    else:
        lag_samples, align_corr = find_alignment(
            ref, test, args.target_rate, args.max_offset_seconds)

    ref_aligned, test_aligned = crop_aligned(ref, test, lag_samples)
    if len(ref_aligned) < args.target_rate:
        raise ValueError("aligned overlap is shorter than one second")

    min_len = min(len(ref_aligned), len(test_aligned))
    ref_aligned = ref_aligned[:min_len]
    test_aligned = test_aligned[:min_len]

    metrics = metric_summary(ref_aligned, test_aligned, args.target_rate)
    segments = segment_summaries(
        ref_aligned,
        test_aligned,
        args.target_rate,
        args.segment_seconds,
        args.segment_hop_seconds,
    )

    failures = []
    if metrics["envelope_corr"] < args.min_envelope_corr:
        failures.append(f"envelope_corr {metrics['envelope_corr']:.3f} < {args.min_envelope_corr:.3f}")
    if metrics["spectral_cosine"] < args.min_spectral_cosine:
        failures.append(f"spectral_cosine {metrics['spectral_cosine']:.3f} < {args.min_spectral_cosine:.3f}")
    if metrics["band_mae_db"] > args.max_band_mae_db:
        failures.append(f"band_mae_db {metrics['band_mae_db']:.2f} > {args.max_band_mae_db:.2f}")
    if metrics["high_band_mae_db"] > args.max_high_band_mae_db:
        failures.append(f"high_band_mae_db {metrics['high_band_mae_db']:.2f} > {args.max_high_band_mae_db:.2f}")
    if abs(metrics["rms_delta_db"]) > args.max_rms_delta_db:
        failures.append(f"rms_delta_db {metrics['rms_delta_db']:.2f} outside +/-{args.max_rms_delta_db:.2f}")

    report = {
        "reference": os.path.abspath(args.reference),
        "test": os.path.abspath(args.test),
        "sample_rate": args.target_rate,
        "compared_seconds": min_len / float(args.target_rate),
        "lag_seconds": lag_samples / float(args.target_rate),
        "alignment_envelope_corr": align_corr,
        **metrics,
        "segments": segments,
        "failures": failures,
    }
    return report


def print_report(report, show_bands, show_segments):
    print(f"Compared:       {report['compared_seconds']:.2f}s at {report['sample_rate']} Hz")
    print(f"Alignment lag:  {report['lag_seconds']:+.3f}s "
          f"(envelope corr {report['alignment_envelope_corr']:.3f})")
    print(f"RMS dBFS:       reference={report['reference_rms_dbfs']:.2f} "
          f"test={report['test_rms_dbfs']:.2f} "
          f"delta={report['rms_delta_db']:+.2f} dB")
    print(f"Envelope corr:  {report['envelope_corr']:.3f}")
    print(f"Spectral cosine:{report['spectral_cosine']:.3f}")
    print(f"Band MAE:       {report['band_mae_db']:.2f} dB")
    print(f"High-band MAE:  {report['high_band_mae_db']:.2f} dB "
          f"(signed {report['high_band_delta_db']:+.2f} dB)")
    print(f"ZCR:            reference={report['reference_zcr']:.4f} "
          f"test={report['test_zcr']:.4f}")

    if show_bands:
        print("")
        print("Bands:")
        for band in report["bands"]:
            print(f"  {band['lo_hz']:>5.0f}-{band['hi_hz']:<5.0f} Hz "
                  f"delta={band['delta_db']:+6.2f} dB "
                  f"ref={band['reference_db']:7.2f} test={band['test_db']:7.2f}")

    if show_segments and report.get("segments"):
        print("")
        print("Worst Segments:")
        for segment in sorted(
            report["segments"],
            key=lambda s: (s["high_band_mae_db"], s["band_mae_db"]),
            reverse=True,
        )[:8]:
            print(
                f"  {segment['start_seconds']:6.2f}-{segment['end_seconds']:<6.2f}s "
                f"env={segment['envelope_corr']:.3f} "
                f"spec={segment['spectral_cosine']:.3f} "
                f"band={segment['band_mae_db']:.2f}dB "
                f"high={segment['high_band_mae_db']:.2f}dB "
                f"high_delta={segment['high_band_delta_db']:+.2f}dB "
                f"rms={segment['rms_delta_db']:+.2f}dB"
            )

    if report["failures"]:
        print("")
        print("FAIL")
        for failure in report["failures"]:
            print(f"  {failure}")
    else:
        print("")
        print("PASS")


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Compare port audio against an emulator/hardware reference capture.")
    parser.add_argument("reference", help="reference WAV or raw s16le PCM")
    parser.add_argument("test", help="test WAV or raw s16le PCM")
    parser.add_argument("--reference-format", choices=("auto", "wav", "raw"), default="auto")
    parser.add_argument("--test-format", choices=("auto", "wav", "raw"), default="auto")
    parser.add_argument("--reference-raw-rate", type=int, default=22050)
    parser.add_argument("--test-raw-rate", type=int, default=22050)
    parser.add_argument("--reference-raw-channels", type=int, default=2)
    parser.add_argument("--test-raw-channels", type=int, default=2)
    parser.add_argument("--target-rate", type=int, default=22050)
    parser.add_argument("--reference-start", type=float, default=0.0,
                        help="seconds to skip at the start of the reference")
    parser.add_argument("--test-start", type=float, default=0.0,
                        help="seconds to skip at the start of the test")
    parser.add_argument("--duration", type=float, default=None,
                        help="seconds to compare after start offsets")
    parser.add_argument("--max-offset-seconds", type=float, default=2.0,
                        help="maximum latency correction searched by envelope alignment")
    parser.add_argument("--no-align", action="store_true")
    parser.add_argument("--min-envelope-corr", type=float, default=0.55)
    parser.add_argument("--min-spectral-cosine", type=float, default=0.90)
    parser.add_argument("--max-band-mae-db", type=float, default=8.0)
    parser.add_argument("--max-high-band-mae-db", type=float, default=10.0)
    parser.add_argument("--max-rms-delta-db", type=float, default=8.0)
    parser.add_argument("--print-bands", action="store_true")
    parser.add_argument("--segment-seconds", type=float, default=0.0,
                        help="also compute metrics for fixed-size aligned windows")
    parser.add_argument("--segment-hop-seconds", type=float, default=0.0,
                        help="seconds between segment starts; defaults to segment length")
    parser.add_argument("--print-segments", action="store_true",
                        help="print worst segment metrics when --segment-seconds is set")
    parser.add_argument("--json-out", help="write full metrics to a JSON file")
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    try:
        report = compare(args)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    print_report(report, args.print_bands, args.print_segments)
    if args.json_out:
        with open(args.json_out, "w") as f:
            json.dump(report, f, indent=2, sort_keys=True)
            f.write("\n")
    return 1 if report["failures"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
