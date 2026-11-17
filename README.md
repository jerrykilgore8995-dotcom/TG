# TG Type D Preview

A Windows x64 VST3 project with a component-level EMI TG12345 Mk II **Type D** SPICE reconstruction, reduced real-time DSP and UMC1820 calibration. The unbranded panel follows the Chandler TG2-500 layout, with Mic/Line, Coarse Gain, Fine Gain, Impedance, Output, Polarity and Match.

## Reference and accuracy

The electrical reference is the **TG12345 Mk II microphone cassette**, including the Type D board through its direct-coupled output. The TG2-500 uses a different TG12428-derived circuit with a push-pull output stage and output transformer. Treat the preview as a Type D model. The TG1 is a separate limiter.

- The Type D reconstruction includes Q1–Q3, R1–R8 and AOT1, C1–C6, the external secondary damping network and a coupled-inductor input transformer. The EMI handbook specifies the 1:3.16 turns ratio, 200 Ω source, gain-control range and amplifier load. Winding losses, leakage, capacitance and the NPN compact model are estimates. Core saturation, hysteresis and a physical output transformer remain outside this reconstruction.
- The nominal 1200/300 markings follow the TG2-500 faceplate. The Type D preview uses the native and shunted input fixtures, measured in SPICE at roughly 836 and 301 Ω resistive at 1 kHz, respectively. The input switch models a source fixture and never changes the physical XLR load. Line mode uses a digital pad and disables input-load switching.
- The VST3 uses fitted complex AC sections and a bounded nonlinear stage at 4×, with a tracking correction filter at the session rate. It interpolates five physical fine-gain settings and normalizes the panel gain at 1 kHz. Line mode uses a 20 dB digital pad.
- The SPICE and DSP reports are published by the build. The analog filter fit agrees with the *simulated* complex AC response within 0.05% over 20 Hz–20 kHz before digitization. At 44.1 kHz with centered Fine Gain, the hosted preview stayed within 0.27 dB and 1.86° of SPICE at nine frequencies from 20 Hz to 20 kHz after host delay compensation. In 200 tone comparisons of the reduced core at 176.4 kHz, 197 landed within 5% fundamental amplitude and 48 landed within 10% THD. The remaining THD and harmonic differences exceed the target. No TG2-500 or UMC1820 hardware comparison has been run.

Sources

- [EMI TG12345 Mk II handbook, §§3.2–3.4 and 12.27](https://www.ladislavbrezovnik.com/diy/TG12413/EMI_Recording-Console-Handbook%2BMk2.pdf)
- [Chris Regent Type D drawing](https://drive.google.com/file/d/1eVn6wJiBm0iJ0ZLwB-YkMr97o2YpWXEI/view) and [schematic collection discussion](https://groupdiy.com/threads/tg-diagrams.77280/)
- [TG2-500 manufacturer manual and panel](https://chandlerlimited.com/wp-content/uploads/2018/01/Chandler_Limited_TG2_500_Pre_Amp_Manual.pdf)
- [TG2-500 independent bench review](https://www.soundonsound.com/reviews/chandler-limited-tg2-500) and [frequency/THD load comparison](https://umbrella-company.jp/contents/chandler-tg2-500-technical-note/)
- [UMC1820 input and output specifications](https://www.bhphotovideo.com/lit_files/155647.pdf)

## Capture a UMC1820 input at 44.1 kHz

**Direct connection.** In the plugin, right-click **Match** and choose **Test output attenuation**. Export the test WAV at your DAW rate. The pad is adjustable from 20 to 80 dB and defaults to 50 dB. With the UMC1820’s published +16 dBu maximum and the test’s −6 dBFS peak tone, the loudest tone is nominally −40 dBu. The default test spans approximately −70 to −40 dBu at the preamp input.

1. Switch phantom power **off** for the input bank and disconnect the microphone. Connect a spare fixed-level UMC1820 line output to the chosen mic input using a balanced TRS-to-XLR cable. Disable DAW monitoring, direct monitoring and any return-to-send routing.
2. Play the mono export at unity gain directly through that output, with no pan-law attenuation and record the mic input dry. Keep the gain and PAD positions you intend to use when tracking. Confirm the loudest test tone clears clipping. If the capture has insufficient signal, export with less attenuation and repeat. Use more attenuation if the return clips.
3. Export the raw mono return. Keep both sync markers and use the same sample rate for playback and record. Use the exact WAV you played:

```powershell
py -m pip install -r calibration/requirements.txt
py calibration/tg_capture.py measure-direct UMC1820-match-test.wav umc-return.wav capture --input-label "UMC1820 XLR 1"
```

The same attenuated test can be generated without opening the plugin:

```powershell
py calibration/tg_capture.py generate UMC1820-match-test.wav --rate 44100 --attenuation-db 50
```

The direct measurement uses the published +16 dBu maximum as its level reference. Use `--line-output-dbu` with the actual full-scale sine-equivalent output level if you have measured it for the same cable and load. Direct capture measures the playback DAC, cable and preamp/ADC together. Digital attenuation lowers the waveform before the DAC, so excess noise or a load-dependent response can limit precision.

**Analog reference.** For absolute input calibration and measured mic-pin voltage, record a synchronous calibrated sense path on the left and DUT on the right. Measure the analog reference level at the input using a steady sine. Supply the observed reference peak and measured dBu to the stereo measurement command. Keep the reference gain unchanged.

```powershell
py calibration/tg_capture.py measure umc-return-stereo.wav capture --reference-dbfs-peak -20 --reference-dbu -60 --input-label "UMC1820 XLR 1"
```

Use Mic mode and select **Load Match profile** from the Match menu to import `capture/input.tgmatch`. Click Match to toggle correction. The plugin stores the attenuation setting and profile in the DAW project. Match corrects the coherent measured frequency/phase response within bounded gain and calibrates the input level driving the Type D model. THD and harmonics are reported for inspection. Converter clipping, preamp noise and microphone interaction with the physical XLR load occur before the plugin. Control real phantom at the interface.

## Build

The GitHub Actions workflow generates the reduced model in ngspice, runs the C++ measurement harness and packages the Windows x64 VST3. Copy the complete `.vst3` bundle to `C:\Program Files\Common Files\VST3` and rescan in the DAW.

For a local Windows build, generate the lookup header with Python and ngspice first, then use CMake 3.25+ and Visual Studio with the Desktop C++ workload.

```powershell
py -m pip install -r calibration/requirements.txt
py spice/characterize.py --header build/generated/TypeDModel.h --report build/spice-report.json
cmake -S . -B build -A x64
cmake --build build --config Release --target TG_VST3
```
