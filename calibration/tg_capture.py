"""Generate a synchronous UMC1820 stimulus and measure a stereo return."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

TEST_FREQUENCIES = (100, 1000, 5000, 10000)
TEST_LEVELS = (-36, -24, -12, -6)
DELAY = 128
TAPS = 513


def db(value):
    return 20 * np.log10(np.maximum(np.abs(value), 1e-15))


def stimulus(rate):
    rng = np.random.default_rng(1820)
    sync = signal.chirp(np.arange(rate // 4) / rate, 350, 0.25, 12000)
    sync *= signal.windows.tukey(len(sync), 0.12) * 0.12
    freq = np.fft.rfftfreq(rate, 1 / rate)
    fft = np.zeros(len(freq), dtype=complex)
    band = (freq >= 12) & (freq <= min(22000, 0.48 * rate))
    fft[band] = np.exp(2j * np.pi * rng.random(np.count_nonzero(band))) / np.sqrt(freq[band])
    period = np.fft.irfft(fft, n=rate)
    period *= 0.12 / np.max(np.abs(period))
    chunks = [np.zeros(rate), sync, np.zeros(3 * rate // 4)]
    start = sum(map(len, chunks))
    chunks.extend([np.tile(period, 5), np.zeros(rate // 2)])
    tone_start = sum(map(len, chunks))
    time = np.arange(3 * rate // 2) / rate
    taper = signal.windows.tukey(len(time), 0.2)
    for level in TEST_LEVELS:
        for frequency in TEST_FREQUENCIES:
            chunks.append(10 ** (level / 20) * np.sin(2 * np.pi * frequency * time) * taper)
            chunks.append(np.zeros(rate // 2))
    end = sum(map(len, chunks))
    chunks.extend([sync, np.zeros(rate)])
    return np.concatenate(chunks), sync, start, tone_start, end


def capture_audio(path):
    rate, audio = wavfile.read(path)
    if audio.ndim != 2 or audio.shape[1] != 2:
        raise ValueError("Export exactly two synchronous channels: reference left, DUT right.")
    if np.issubdtype(audio.dtype, np.signedinteger):
        audio = audio.astype(float) / (np.iinfo(audio.dtype).max + 1)
    elif np.issubdtype(audio.dtype, np.floating):
        audio = audio.astype(float)
    else:
        raise ValueError("Export signed PCM or float WAV.")
    if not np.isfinite(audio).all() or np.any(np.abs(audio) >= 0.999):
        raise ValueError("Capture contains clipped/full-scale or invalid samples.")
    return rate, audio


def find_sync(reference, sync, expected_distance):
    correlation = signal.fftconvolve(reference, sync[::-1], mode="valid")
    limit = len(correlation) - expected_distance
    if limit < 1:
        raise ValueError("Capture is shorter than the test sequence.")
    first = np.argmax(np.abs(correlation[:limit]))
    second_expected = first + expected_distance
    lo, hi = max(0, second_expected - 100), min(len(correlation), second_expected + 101)
    second = lo + np.argmax(np.abs(correlation[lo:hi]))
    if abs(second - second_expected) > 1:
        raise ValueError("Sync markers drifted. Use the same audio clock and disable resampling.")
    for marker in (first, second):
        clip = reference[marker:marker + len(sync)]
        coherence = abs(np.dot(clip, sync)) / max(np.linalg.norm(clip) * np.linalg.norm(sync), 1e-15)
        if coherence < 0.55:
            raise ValueError("Sync marker too weak or distorted in the reference channel.")
    return first


def harmonics(block, rate, frequency):
    spectrum = np.fft.rfft(block, axis=0) * (2 / rate)
    orders = np.arange(1, min(20, (rate - 1) // (2 * frequency)) + 1)
    values = spectrum[orders * frequency]
    # Harmonic phase is relative to the fundamental of each channel.
    return values * np.exp(-1j * orders[:, None] * np.angle(values[0, :]))


def correction(freq, transfer, coherence, rate):
    band = (freq >= 20) & (freq <= min(20000, 0.45 * rate)) & (coherence >= 0.98)
    if np.count_nonzero(band) < 200 or np.mean(coherence[(freq >= 50) & (freq <= 18000)] >= 0.98) < 0.95:
        raise ValueError("Insufficient coherent broadband capture. Increase the test level or reference gain.")
    mid = np.median(np.abs(transfer[(freq >= 800) & (freq <= 1200) & band]))
    if not np.isfinite(mid) or mid <= 0:
        raise ValueError("Could not determine midband gain.")
    inverse = mid / transfer[band]
    magnitude = np.clip(np.abs(inverse), 10 ** (-9 / 20), 10 ** (9 / 20))
    phase = np.unwrap(np.angle(inverse))
    # Preserve polarity but elide an arbitrary integer wrap before blending.
    phase -= 2 * np.pi * round(np.interp(1000, freq[band], phase) / (2 * np.pi))
    lo = np.clip((freq - 12) / 16, 0, 1)
    hi = np.clip((min(22000, 0.49 * rate) - freq) / 2500, 0, 1)
    blend = (0.5 - 0.5 * np.cos(np.pi * lo)) * (0.5 - 0.5 * np.cos(np.pi * hi))
    ideal = np.exp(blend * (np.interp(freq, freq[band], np.log(magnitude)) +
                           1j * np.interp(freq, freq[band], phase)))
    impulse = np.fft.irfft(ideal * np.exp(-2j * np.pi * freq * DELAY / rate), n=rate)
    window = signal.windows.tukey(TAPS, 0.3)
    taps = impulse[:TAPS] * window
    response = np.fft.rfft(taps, n=rate) * np.exp(2j * np.pi * freq * DELAY / rate)
    valid = band & (freq >= 30) & (freq <= 18000)
    residual = transfer[valid] * response[valid] / mid
    return taps, mid, {
        "max_magnitude_error_30hz_to_18khz_db": float(np.max(np.abs(db(residual)))),
        "max_phase_error_30hz_to_18khz_deg": float(np.max(np.abs(np.angle(residual, deg=True)))),
    }


def analyse(path, output, reference_dbfs, reference_dbu, input_label):
    rate, audio = capture_audio(path)
    _, sync, start, tone_start, end_sync = stimulus(rate)
    shift = find_sync(audio[:, 0], sync, end_sync - rate) - rate
    if shift + start < 0 or shift + end_sync > len(audio):
        raise ValueError("Incomplete capture.")
    # Use equal, whole periods on both channels after one settling period.
    blocks = audio[shift + start + rate:shift + start + 5 * rate].reshape(4, rate, 2)
    spectrum = np.fft.rfft(blocks, axis=1)
    ref, dut = spectrum[:, :, 0], spectrum[:, :, 1]
    cross = np.sum(dut * ref.conj(), axis=0)
    ref_power = np.sum(abs(ref) ** 2, axis=0)
    dut_power = np.sum(abs(dut) ** 2, axis=0)
    transfer = cross / np.maximum(ref_power, 1e-30)
    coherence = np.abs(cross) ** 2 / np.maximum(ref_power * dut_power, 1e-30)
    freq = np.fft.rfftfreq(rate, 1 / rate)
    taps, mid, fit = correction(freq, transfer, coherence, rate)

    output.mkdir(parents=True, exist_ok=True)
    with (output / "frequency-response.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["hz", "relative_gain_db", "phase_deg", "coherence"])
        for index in np.where((freq >= 12) & (freq <= min(22000, 0.48 * rate)))[0]:
            writer.writerow([freq[index], db(transfer[index] / mid),
                             np.angle(transfer[index], deg=True), coherence[index]])
    offset = reference_dbu - reference_dbfs
    harmonic_rows, tone_rows = [], []
    for index, (level, frequency) in enumerate((l, f) for l in TEST_LEVELS for f in TEST_FREQUENCIES):
        start = shift + tone_start + 2 * rate * index + rate // 4
        values = harmonics(audio[start:start + rate], rate, frequency)
        if np.any(np.abs(values[0]) < 1e-7):
            raise ValueError(f"Tone {frequency} Hz at playback level {level} is missing or too weak.")
        input_dbu = db(values[0, 0]) + offset
        relative = values / abs(values[0])
        thd = 100 * np.sqrt(np.sum(abs(relative[1:]) ** 2, axis=0))
        tone_rows.append([level, frequency, input_dbu, db(values[0, 1]), *thd, len(relative)])
        for order, pair in enumerate(relative[1:], 2):
            harmonic_rows.append([level, frequency, input_dbu, order, db(pair[0]),
                                  np.angle(pair[0], deg=True), db(pair[1]), np.angle(pair[1], deg=True)])
    for name, headings, rows in (
        ("thd.csv", ["playback_dbfs_pk", "hz", "mic_input_dbu", "dut_fund_dbfs_pk",
                     "reference_thd_pct", "dut_thd_pct", "maximum_order"], tone_rows),
        ("harmonics.csv", ["playback_dbfs_pk", "hz", "mic_input_dbu", "order",
                           "reference_dbc", "reference_phase_deg", "dut_dbc", "dut_phase_deg"], harmonic_rows),
    ):
        with (output / name).open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(headings)
            writer.writerows(rows)
    # A sine at a recorded 0 dBFS peak corresponds to this analog mic-input dBu.
    digital_zero_dbu = offset - float(db(mid))
    with (output / "input.tgmatch").open("w") as stream:
        stream.write(f"TG_MATCH_V1 {rate} {len(taps)} {DELAY} {digital_zero_dbu:.9g}\n")
        stream.writelines(f"{value:.10g}\n" for value in taps)
    report = {"sample_rate": rate, "input": input_label,
              "mic_dbu_at_dut_0_dbfs_peak": digital_zero_dbu, **fit}
    (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    generate = commands.add_parser("generate")
    generate.add_argument("wav", type=Path)
    generate.add_argument("--rate", type=int, choices=(44100, 48000, 88200, 96000), default=44100)
    measure = commands.add_parser("measure")
    measure.add_argument("wav", type=Path)
    measure.add_argument("output", type=Path)
    measure.add_argument("--reference-dbfs-peak", type=float, required=True)
    measure.add_argument("--reference-dbu", type=float, required=True)
    measure.add_argument("--input-label", required=True)
    args = parser.parse_args()
    if args.command == "generate":
        audio, *_ = stimulus(args.rate)
        wavfile.write(args.wav, args.rate, audio.astype(np.float32))
        return
    try:
        if not np.isfinite([args.reference_dbfs_peak, args.reference_dbu]).all():
            raise ValueError("Reference calibration must be finite.")
        analyse(args.wav, args.output, args.reference_dbfs_peak, args.reference_dbu, args.input_label)
    except (ValueError, OSError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
