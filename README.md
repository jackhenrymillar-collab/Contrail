# Contrail

A Mac VST3/AU tape plugin for wow, flutter and vintage character (not a delay). It is a sister
plugin to Overcast and uses the same stack: JUCE 8, CMake, C++20, a JUCE-free DSP library, and
Catch2 tests.

- Design and decisions: [docs/DESIGN.md](docs/DESIGN.md)
- Panel mockup (live): https://claude.ai/artifact/5ad5qni5t34CJ2CtbY2Y8p
- Mockup sources: [docs/mockup/](docs/mockup/)

**Status: milestone 1 (sound engine and plugin shell).** All six formats, every knob and
switch, presets, A/B and per-format knob memory work. The panel is a plain placeholder
until the "Above the clouds" panel lands (milestone 2).

## Building on macOS

Requirements: Xcode 15 or later (command line tools are enough) and CMake 3.22+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
ctest --test-dir build --output-on-failure
```

JUCE and Catch2 are downloaded on the first configure. To use a local JUCE checkout, add
`-DCONTRAIL_JUCE_DIR=/path/to/JUCE`. The build makes a universal VST3, AU and Standalone app
in `build/Contrail_artefacts/Release/` and copies the VST3 and AU into `~/Library/Audio/Plug-Ins/`.

Audio previews: configure with `-DCONTRAIL_BUILD_RENDER=ON` and run
`build/contrail_render <out-dir>`. It renders a short electric piano clip dry, through each
format, and through a Tape Stop and Start.

## The signal path

```
Drive -> record pre-emphasis -> hysteresis (oversampled) -> de-emphasis
      -> transport (wow, flutter, drift, tape stop) -> hiss -> dropouts
      -> playback losses (Age) -> head bump and low cut -> Tone -> width -> Output
      -> Mix with the dry signal -> bypass
```

- **Wow and flutter:** a delay line read by a moving head, with a 16-tap Kaiser-windowed sinc
  interpolator. The head's motion is built as a speed deviation (the way wow and flutter are
  specified on real machines) and integrated: reel eccentricity with a second harmonic,
  smooth random swings so it never loops, capstan and pinch-roller flutter, random flutter,
  and a fast, tiny scrape flutter. **Wow** and **Flutter** are peak speed deviation in %.
- **Drift:** trades steady periodic wow for random swings, and makes the wow rate wander.
  The knob reads Steady, Slow, Medium, Fast or Wander.
- **Drive:** level into Jiles-Atherton hysteresis (Chowdhury, DAFx 2019), solved with RK4 at
  about 176 to 192 kHz (4x at 44.1/48 kHz, 2x at 88.2/96 kHz). Record pre-emphasis makes the
  highs saturate first, as on real tape. The same amount comes off afterwards, so Drive
  changes the saturation, not the level of quiet passages. **Bias** (host parameter, nominal
  at 50) narrows the loop and dulls the highs a little when raised.
- **Age:** extra head-to-tape spacing (Bertram spacing loss, worse on slower tape), more hiss,
  and random dropouts that dip and dull the sound. Reads New, Light, Medium or Heavy.
- **Tape Stop:** on slows the tape to a halt along a smooth curve; pitch, level and top end
  fall with speed. Off starts it again, then splices back into time with a 60 ms crossfade.
- **Sync:** locks the periodic wow to the host tempo at the **Rate** note value (4 bars to 1/16).
- **Stereo:** Linked (both sides move together) or Wide (each side has its own motion).
- **Noise:** tape hiss on or off. Hiss slows and stops with the tape.
- **Latency:** a fixed 25 ms at every sample rate, reported to the host. The dry signal is
  delayed to match, so Mix never combs and bypass is seamless.

## Formats

| Format | Speed | Character |
| --- | --- | --- |
| Studio Reel | 15 ips | Steady and warm, soft 55 Hz head bump, very low hiss |
| Home Reel | 7.5 ips | Looser, darker highs, a touch of hiss |
| Cassette | 1 7/8 ips | Gentle wow, audible flutter, rolled-off top |
| Worn Cassette | 1 7/8 ips | Deep wow, frequent dropouts, grit, heavy wear |
| VHS | 1.31 ips linear track | Slow, woozy pitch, soft narrow top, narrower image |
| Dictaphone | 0.94 ips micro-cassette | Fast warble, thin band (220 Hz to 5 kHz), nearly mono |

Each format sets its own transport rates, head and tape constants, saturation, hiss and
dropout rates, and a loudness trim. Switching format glides every value over 150 ms. Each
format remembers its own knobs.

## What the tests check

- Latency is 25 ms at every rate and the wet signal lines up with it exactly
- Mix at 0% and bypass are the dry signal bit for bit; bypass fades without clicks
- Silence in is silence out with Noise off; hiss sits at each format's level
- Formats are loudness matched within 1 dB; 44.1 and 96 kHz sound the same
- The Wow and Flutter knobs set the measured peak pitch deviation; Sync locks to tempo
- Tape Stop halves the pitch halfway down, goes silent, and Start comes back in time with no clicks
- Hysteresis saturates, has a real loop, and stays finite for extreme input
- Oversampling is flat to 18 kHz and keeps distortion from aliasing
- Ten seconds of random automation never produces NaN or runaway levels

CI builds a universal binary and runs `auval` and `pluginval` (strictness 10) on every pull request.
