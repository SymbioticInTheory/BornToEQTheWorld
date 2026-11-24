# Born To EQ The World

A headphone correction EQ plugin (VST3, Windows) for use in a DAW: a nicer
in-DAW alternative to Peace / Equalizer APO. Pick a headphone, pick a target
(Harman, oratory1990, ...), and get the same correction Peace would apply.

## Status

Step 1 of 4: DSP core.

- `src/dsp/Biquad.h`: RBJ cookbook peaking / low shelf / high shelf filters in
  double precision, Direct Form I, computed at the host sample rate. This is the
  same approach Equalizer APO uses, which is why presets should sound identical.
- `src/dsp/ParametricEq.h`: parses AutoEQ `ParametricEQ.txt` files, runs the
  filter chain with preamp, correction amount (0 to 100%) and auto preamp.
- `tests/compare_with_autoeq.py`: renders an impulse through the C++ core and
  compares it with AutoEQ's own filter maths at 44.1 to 192 kHz.

## Build and test

```
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Requires Python 3 with numpy and scipy for the test.

## Data

Headphone presets come from [AutoEQ](https://github.com/jaakkopasanen/AutoEq)
(MIT licence), which includes oratory1990's measurements.
