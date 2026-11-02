# SpectraEQ

24-band parametric EQ (VST3) inspired by FabFilter Pro-Q: interactive spectrum display, per-band Stereo / Left / Right / **Mid / Side** placement, dynamic EQ, steep cut slopes. Built with JUCE 8.

## Features
- 24 bands; double-click empty area to add a band, drag nodes (Shift = fine), mouse wheel on a node = Q, double-click node = delete, right-click node = shape / placement / slope / dynamic / delete
- Shapes: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt
- Cut slopes: 6, 12, 18, 24, 30, 36, 48, 72, 96 dB/oct
- Per-band stereo placement: Stereo, Left, Right, Mid, Side
- Per-band dynamic EQ: threshold, range (+/-), above/below, global attack/release
- Pre/Post spectrum analyzer (4.5 dB/oct tilt), display range 3/6/12/30 dB
- Output gain, output pan, EQ gain scale (0-200 %), polarity invert, bypass
- Zero latency (minimum phase IIR)

## Build (GitHub)
Push to GitHub -> Actions builds automatically -> download `SpectraEQ-Windows-VST3` from the run's Artifacts (or from Releases when you push a tag like `v1.0.0`).

## Install in FL Studio
1. Unzip, copy the `SpectraEQ.vst3` folder to `C:\Program Files\Common Files\VST3\`
2. FL Studio: Options > Manage plugins > Find plugins (Verify plugins)
3. Add it from the plugin database (Installed > Effects > VST3)

## Local build
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target SpectraEQ_VST3
```

## License
JUCE is AGPLv3/commercial. This open-source project is therefore licensed AGPL-3.0 (add the license file when creating the repo on GitHub).
Not affiliated with FabFilter.
