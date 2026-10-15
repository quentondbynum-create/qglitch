# QGlitch — Headroom Audio

Live glitch/stutter FX. Track audio is captured into a 2-second circular buffer;
four trigger pads mangle it in real time:

- **STUTTER** — repeats a slice (1/4 to 1/64, host-tempo aware)
- **REVERSE** — plays the buffer backwards
- **TAPE STOP** — pitch/speed ramp down to a halt
- **CRUSH** — bitcrusher (bit depth + sample-rate reduction)

Mix knob blends dry/wet. When nothing is triggered, dry passes through.

UI direction: "Chaos" — dark, neon green/magenta digital artifacts, scanlines,
distorted type. The pads are the hero; four small knobs (Rate / Mix / Bits / SR).
Cymatics-style: one hero element, minimal controls, instant gratification.

## Build

```bash
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=<path-to-juce-7.0.12>
cmake --build build --config Release
```

VST3/AU/Standalone land in `build/QGlitch_artefacts/Release/`.
