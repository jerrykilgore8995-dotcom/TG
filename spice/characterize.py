"""Generate the bounded Type D model and report the reference SPICE measurements."""
import argparse
import json
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory

import numpy as np
from scipy import signal

HERE = Path(__file__).resolve().parent
TRIM_RESISTORS = (1200, 650, 300, 100, 0)
SHUNT_RESISTORS = (1e12, 470)
BASE_HZ = 1000
LIMIT = 16
POINTS = 513


def simulate(executable, netlist, folder, resistance, shunt, drive, freq=1000,
             transient=True):
    for key, value in {"RGC": resistance, "TERM": shunt, "DRIVE": drive, "FREQ": freq}.items():
        netlist = re.sub(r"\b" + key + r"=[^\s]+", f"{key}={value:.12g}", netlist, count=1)
    script = folder / "reference.cir"
    # Capture exactly ten complete cycles after the feedback network has settled.
    duration = np.ceil(0.20 * freq) / freq
    timestep = 1 / max(500000, freq * 130)
    bench = f"""op
let bias = v(out)
wrdata bias.txt bias
ac dec 90 2 180000
wrdata ac.txt v(out) v(in) v(mic) i(Vsig)
"""
    if transient:
        bench += f"""tran {timestep:.12g} {duration + 11 / freq:.12g} {duration:.12g} {timestep:.12g}
wrdata wave.txt v(out)
"""
    script.write_text(netlist.rsplit(".end", 1)[0] + """
.control
set wr_singlescale
set wr_vecnames
set numdgt=14
""" + bench + "quit\n.endc\n.end\n")
    result = subprocess.run([executable, "-b", script.name], cwd=folder,
                            capture_output=True, text=True, check=True)
    try:
        ac = np.loadtxt(folder / "ac.txt", skiprows=1)
        bias = float(np.loadtxt(folder / "bias.txt", skiprows=1)[-1])
        wave = np.loadtxt(folder / "wave.txt", skiprows=1) if transient else None
    except (OSError, ValueError) as error:
        raise RuntimeError(result.stdout + result.stderr) from error
    if transient and wave[-1, 0] < duration + 10.99 / freq:
        raise RuntimeError(result.stdout + result.stderr)
    return ac, bias, wave, duration


def response_at(frequency, ac, column=1):
    return (np.interp(frequency, ac[:, 0], ac[:, column]) +
            1j * np.interp(frequency, ac[:, 0], ac[:, column + 1]))


def rational_fit(ac):
    # Equation-error least squares with Sanathanan-Koerner reweighting.
    frequency = ac[:, 0]
    response = ac[:, 1] + 1j * ac[:, 2]
    s = 1j * frequency / BASE_HZ
    best = None
    for order in (3, 4, 5, 6, 7):
        for num_order in range(order - 2, order):
            x = np.column_stack([s ** i for i in range(num_order + 1)] +
                                [-response * s ** i for i in range(1, order + 1)])
            den = np.ones(len(s), dtype=complex)
            for _ in range(9):
                w = 1 / np.maximum(np.abs(response * den), 1e-9)
                scaled = x * w[:, None]
                target = response * w
                matrix = np.vstack([scaled.real, scaled.imag])
                norms = np.linalg.norm(matrix, axis=0)
                fit, *_ = np.linalg.lstsq(matrix / norms, np.r_[target.real, target.imag], rcond=1e-13)
                fit /= norms
                b = fit[:num_order+1][::-1]
                a = np.r_[fit[num_order+1:][::-1], 1]
                den = np.polyval(a, s)
            zeros, poles, gain = signal.tf2zpk(b, a)
            if np.any(poles.real >= 0):
                continue
            try:
                sos = signal.zpk2sos(zeros, poles, gain, analog=True)
            except (IndexError, ValueError):
                continue
            residual = np.polyval(b, s) / den / response
            audio = (frequency >= 20) & (frequency <= 20000)
            score = float(np.max(np.abs(residual[audio] - 1)))
            if best is None or score < best[0]:
                best = score, sos
            if score < 1e-4:
                return best
    if best is None:
        raise RuntimeError("Stable analog fit failed.")
    return best


def wave_cycle(wave, duration, frequency):
    size = 1024
    time = duration + (9 + np.arange(size) / size) / frequency
    return np.interp(time, wave[:, 0], wave[:, 1])


def nonlinear_fit(executable, netlist, folder, ac, r, shunt):
    linear_gain = abs(response_at(1000, ac))
    # The low-level K2 vector has static and differentiating components.
    # Fit both against multiple tones so they preserve the measured K2 phase.
    matrix, targets = [], []
    for frequency in (100, 1000, 5000, 10000):
        response = response_at(frequency, ac)
        span = .45
        _, _, wave, duration = simulate(executable, netlist, folder, r, shunt,
                                        span / abs(response), frequency)
        angle = 2 * np.pi * np.arange(1024) / 1024 + np.angle(response)
        x = np.sin(angle) * span
        basis = np.column_stack([x * x, x * np.cos(angle) * span * frequency / 1000])
        vector = np.fft.rfft(basis, axis=0)[2]
        actual = np.fft.rfft(wave_cycle(wave, duration, frequency))[2]
        weight = 1 / max(abs(actual), .001)
        matrix.extend((weight * vector.real, weight * vector.imag))
        targets.extend((weight * actual.real, weight * actual.imag))
    quadratic = np.linalg.lstsq(matrix, targets, rcond=None)[0]

    # Fit the soft limiting from near the peaks of progressively driven
    # waveforms. A curve read through heavy-drive zero crossings folds large
    # bias shifts into the small-signal transfer and creates spurious K2.
    amplitude = np.r_[1.0, 1.4, 1.8, 2.2, 2.6, 3.0, 3.4, 3.8, 4.2,
                      4.7, 5.25, 6, 7, 8, 10, 12, 14, 16, 18]
    lower, upper = [], []
    tilt = np.angle(response_at(1000, ac))
    time = 2 * np.pi * np.arange(1024) / 1024 + tilt
    positive = np.argmax(np.sin(time))
    negative = np.argmin(np.sin(time))
    for span in amplitude:
        _, bias, wave, duration = simulate(executable, netlist, folder, r, shunt,
                                           span / linear_gain)
        y = wave_cycle(wave, duration, 1000) - bias
        upper.append(float(np.clip(y[positive] - quadratic[0] * span * span, 0, span)))
        lower.append(float(np.clip(y[negative] - quadratic[0] * span * span, -span, 0)))
    upper = np.maximum.accumulate(upper)
    lower = np.minimum.accumulate(lower)
    x = np.r_[-amplitude[::-1], 0, amplitude]
    y = np.r_[lower[::-1], 0, upper]
    v = np.linspace(-LIMIT, LIMIT, POINTS)
    curve = np.interp(v, x, y)
    curve[np.abs(v) < 1] = v[np.abs(v) < 1]
    return curve, quadratic

def header_line(values):
    return "{" + ", ".join(f"{value:.9e}f" for value in values) + "}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ngspice", default="ngspice")
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    netlist = (HERE / "type-d.cir").read_text()
    entries, report = [], []
    with TemporaryDirectory() as temporary:
        folder = Path(temporary)
        for shunt in SHUNT_RESISTORS:
            for r in TRIM_RESISTORS:
                ac, bias, _, _ = simulate(args.ngspice, netlist, folder, r, shunt, 0.001,
                                          transient=False)
                error, sos = rational_fit(ac)
                if error > 0.015:
                    raise RuntimeError(f"AC fit error too high for {r} ohms: {error:.2%}")
                linear_gain = abs(response_at(1000, ac))
                curve, quadratic = nonlinear_fit(args.ngspice, netlist, folder, ac, r, shunt)
                sections = [header_line(row) for row in sos]
                sections.extend(["{0, 0, 1, 0, 0, 1}"] * (4 - len(sos)))
                entries.append("    {{\n        " + ",\n        ".join(sections) +
                               "\n    }, {\n        " +
                               ",\n        ".join(", ".join(f"{value:.9e}f" for value in curve[i:i+8])
                                                   for i in range(0, POINTS, 8)) +
                               f"\n    }}, {linear_gain:.9e}f, {header_line(quadratic)}}}")
                bench = []
                for frequency in (100, 1000, 5000, 10000):
                    for dbu in (-65, -50, -40, -30, -20):
                        drive = np.sqrt(2) * .775 * 10 ** (dbu / 20)
                        _, _, wave, duration = simulate(args.ngspice, netlist, folder, r, shunt, drive,
                                                        frequency)
                        bins = abs(np.fft.rfft(wave_cycle(wave, duration, frequency)) / 512)
                        floor = np.maximum(bins[2:21] / bins[1], 1e-15)
                        bench.append({"hz": frequency, "input_dbu": dbu,
                                      "fundamental_peak_volts": float(bins[1]),
                                      "thd_percent": float(np.linalg.norm(floor) * 100),
                                      "k2_k20_dbc": (20 * np.log10(floor)).tolist()})
                z = -response_at(1000, ac, 5) / response_at(1000, ac, 7)
                report.append({"gain_control_ohms": r, "input_shunt_ohms": shunt,
                               "input_impedance_1khz_ohms": [z.real, z.imag],
                               "amplifier_gain_1khz_db": float(20*np.log10(linear_gain / abs(response_at(1000, ac, 3)))),
                               "full_circuit_gain_1khz_db": float(20*np.log10(linear_gain)),
                               "max_analog_complex_fit_error": error, "bias_volts": bias,
                               "operating_points": bench})
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text("""// Generated by spice/characterize.py
#pragma once
namespace tg {
inline constexpr int points = 513;
inline constexpr float curveLimit = 16.0f;
struct Model { float sos[4][6]; float curve[points]; float gain; float quadratic[2]; };
inline constexpr Model models[10] = {
""" + ",\n".join(entries) + "\n};\n}\n")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
