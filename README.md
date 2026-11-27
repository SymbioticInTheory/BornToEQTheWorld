# Born To EQ The World

A headphone correction EQ plugin (VST3, Windows) for use in a DAW: a nicer
in-DAW alternative to Peace / Equalizer APO. Pick a headphone, pick a target
(Harman, oratory1990, ...), and get the same correction Peace would apply.

## Status

Step 1 (DSP core) is done. Step 2 (headphone library) is in progress.

- `src/dsp/Biquad.h`: RBJ cookbook peaking / low shelf / high shelf filters in
  double precision, Direct Form I, computed at the host sample rate. This is the
  same approach Equalizer APO uses, which is why presets should sound identical.
- `src/dsp/ParametricEq.h`: parses AutoEQ `ParametricEQ.txt` files, runs the
  filter chain with preamp, correction amount (0 to 100%) and auto preamp.
- `tests/compare_with_autoeq.py`: renders an impulse through the C++ core and
  compares it with AutoEQ's own filter maths at 44.1 to 192 kHz.
- `data/headphones.tsv`: about 27,000 profiles for 6,200 headphones. For each
  headphone you get oratory1990's own presets (used exactly as he published
  them), AutoEQ's published results for every measurement source, and extra
  targets computed with AutoEQ's optimiser (Harman with and without bass,
  oratory1990 Optimum HiFi, diffuse field and more). Each has parametric and
  127-point GraphicEQ (Wavelet-style) curves.
- `src/dsp/HeadphoneLibrary.h`: loads that file, searches headphones by name
  ("hd650" finds "Sennheiser HD 650") and lists each headphone's profiles.
- `tools/compute_targets.py`: reruns AutoEQ on the raw measurements for the
  extra targets, with the same settings AutoEQ uses for its published results.
- `tools/build_library.py`: regenerates the library from AutoEQ, OPRA and the
  extra-target results.

## Your own background

Right-click anywhere in the plugin or standalone and pick a **Background**:
Forest, five built-in ones (Sunset, Ocean, Neon, Moss, Concrete, drawn by
`tools/make_backgrounds.py`), or **Your own image...**. BEQ keeps a copy next
to `settings.xml` (in
`%APPDATA%/Born To EQ The World`) and picks its colours from it
(`src/plugin/ImageTheme.h`): an accent for a fifth colour dot, a tint for the
haze over the image, and advanced-view curve colours that stay clear of the
accent. **Forest** is the original.

## Build and test

```
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Requires Python 3 with numpy and scipy for the test.

## Data

Headphone presets come from [AutoEQ](https://github.com/jaakkopasanen/AutoEq)
(MIT licence) and oratory1990's own presets via
[OPRA](https://github.com/opra-project/OPRA) (CC BY-SA 4.0). See
`data/ATTRIBUTION.md`.
