# TG Type D Preview

A Windows x64 VST3 project with a component-level EMI TG12345 Mk II **Type D** SPICE reconstruction, reduced real-time DSP and UMC1820 calibration. The unbranded panel follows the Chandler TG2-500 layout, with Mic/Line, Coarse Gain, Fine Gain, Impedance, Output, Polarity and Match.

## Reference and accuracy

The electrical reference is the **TG12345 Mk II microphone cassette**, including the Type D board through its direct-coupled output. The TG2-500 uses a different TG12428-derived circuit with a push-pull output stage and output transformer. Treat the preview as a Type D model. The TG1 is a separate limiter.

- The Type D reconstruction includes Q1–Q3, R1–R8 and AOT1, C1–C6, the external secondary damping network and a coupled-inductor input transformer. The EMI handbook specifies the 1:3.16 turns ratio, 200 Ω source, gain-control range and amplifier load. Winding losses, leakage, capacitance and the NPN compact model are estimates. Core saturation, hysteresis and a physical output transformer remain outside this reconstruction.
- The nominal 1200/300 markings follow the TG2-500 faceplate. The Type D preview uses the native and shunted input fixtures, measured in SPICE at roughly 836 and 301 Ω resistive at 1 kHz, respectively. The input switch models a source fixture and never changes the physical XLR load. Line mode uses a digital pad and disables input-load switching.
- The VST3 uses fitted complex AC sections and a bounded nonlinear stage at 4×, with a tracking correction filter at the session rate. It interpolates five physical fine-gain settings and normalizes the panel gain at 1 kHz. Line mode uses a 20 dB digital pad.
- The SPICE and DSP reports are published by the build. The analog filter fit agrees with the *simulated* complex AC response within 0.05% over 20 Hz–20 kHz before digitization. The nonlinear DSP has residual THD and harmonic errors outside a 5–10% tolerance. No TG2-500 or UMC1820 hardware comparison has been run.

Sources

- [EMI TG12345 Mk II handbook, §§3.2–3.4 and 12.27](https://www.ladislavbrezovnik.com/diy/TG12413/EMI_Recording-Console-Handbook%2BMk2.pdf)
- [Chris Regent Type D drawing](https://drive.google.com/file/d/1eVn6wJiBm0iJ0ZLwB-YkMr97o2YpWXEI/view) and [schematic collection discussion](https://groupdiy.com/threads/tg-diagrams.77280/)
- [TG2-500 manufacturer manual and panel](https://chandlerlimited.com/wp-content/uploads/2018/01/Chandler_Limited_TG2_500_Pre_Amp_Manual.pdf)
- [TG2-500 independent bench review](https://www.soundonsound.com/reviews/chandler-limited-tg2-500) and [frequency/THD load comparison](https://umbrella-company.jp/contents/chandler-tg2-500-technical-note/)
- [UMC1820 input and output specifications](https://www.bhphotovideo.com/lit_files/155647.pdf)

## Capture a UMC1820 input at 44.1 kHz

The tool generates a test WAV and extracts complex response, absolute input-level calibration, THD and available K2–K20 from a two-channel raw recording. Use an analyzer or calibrated low-distortion, high-impedance differential sense path to record the voltage at the DUT input alongside the DUT. A single DUT-only loopback includes the DAC and pad and gives no way to distinguish their errors from the UMC1820.

1. Route the test WAV to an unused line output. Feed it to the UMC1820 mic input through a balanced mic-level test pad. Turn phantom off for that bank, disconnect microphones and disable hardware and DAW monitoring on the return channels. Begin with output level down. Check the loudest tone stays clear of ADC clipping.
2. Record the calibrated input reference on the left and the UMC1820 mic channel on the right. Use a shared audio clock, preserve both sync markers and export a stereo WAV without gain changes, processing, resampling or time stretching.
3. With the same reference-channel gain, record a steady sine and independently measure its dBu at the DUT pins. Enter the recorded sample peak in dBFS and the measured dBu in the command below. The examples are placeholders for these two measured numbers. Preserve the UMC1820 gain, PAD state, sample rate and physical XLR channel for recordings using that profile.

```powershell
py -m pip install -r calibration/requirements.txt
py calibration/tg_capture.py generate umc-test.wav --rate 44100
py calibration/tg_capture.py measure umc-return.wav capture --reference-dbfs-peak -20 --reference-dbu -60 --input-label "UMC1820 XLR 1"
```

Use Mic mode and click **Match** to import `capture/input.tgmatch`. Once imported, click Match to toggle correction or Shift-click to replace the profile. The instance stores the profile in the DAW project. Profile correction inverts the coherent UMC1820 response within a bounded range, then calibrates the input level driving the Type D model. The reported THD and harmonic measurements describe the captured chain and reference. The processor applies linear correction and cannot undo converter clipping, input noise, nonlinear distortion or a microphone’s interaction with the UMC1820’s physical XLR load. The 48V position is visual only. Control actual phantom at the interface.

## Build

The GitHub Actions workflow generates the reduced model in ngspice, runs the C++ measurement harness and packages the Windows x64 VST3. Copy the complete `.vst3` bundle to `C:\Program Files\Common Files\VST3` and rescan in the DAW.

For a local Windows build, generate the lookup header with Python and ngspice first, then use CMake 3.25+ and Visual Studio with the Desktop C++ workload.

```powershell
py -m pip install -r calibration/requirements.txt
py spice/characterize.py --header build/generated/TypeDModel.h --report build/spice-report.json
cmake -S . -B build -A x64
cmake --build build --config Release --target TG_VST3
```
