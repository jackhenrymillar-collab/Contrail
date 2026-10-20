# Contrail design notes

Status as of 2026-10-20: milestone 1 (sound engine and plugin shell) built. Next: the panel (milestone 2), then tuning by ear (milestone 3).

## Concept
- Tape-style pitch movement: wow, flutter and vintage character. It is not a delay.
- It should use the best publicly known algorithms.
- Sister plugin to Overcast (jackhenrymillar-collab/OvercastVST). It reuses Overcast's approach: per-mode skies, knob memory, presets, A/B, CI that builds an unsigned macOS artifact, and before/after audio previews before any sound change is pushed.

## Formats (like Overcast's machines)
Each format sets its own wow, flutter, tone and noise.

| # | Format | Character | Sky look |
|---|--------|-----------|----------|
| 0 | Studio Reel | 15 ips two-track, steady and warm, soft head bump | First light: deep blue, last stars, crisp trails |
| 1 | Home Reel | 7.5 ips, looser, darker highs, a touch of hiss | Golden hour: amber glow, honey cloud tops |
| 2 | Cassette | gentle wow, audible flutter, rolled-off top | Pastel morning: lilac and peach |
| 3 | Worn Cassette | deep wow, dropouts, grit | Faded photograph: sepia, heavy grain, light leak, smudged trails |
| 4 | VHS | woozy pitch, soft narrow top | Tracking lines: magenta sky, scanlines, rolling band, red/cyan sun |
| 5 | Dictaphone | fast warble, thin, lo-fi | Overcast dawn: grey, sun behind haze, drizzle, faint trails |

## Controls
- Knobs: Wow, Flutter, Drift, Drive, Age, Tone, Mix.
- Buttons: Tape Stop/Start, Sync (wow to tempo), Noise.
- Settings: Rate (Free/Sync), Stereo (Linked/Wide), Output.
- Presets with A/B and Save.

## DSP plan
- **Wow and flutter:** a modulated delay line read with high-quality fractional interpolation. The modulation sums slow reel and capstan rates with smooth randomness, so it never loops. A very fast, tiny scrape flutter adds grit.
- **Drive:** Jiles-Atherton hysteresis (J. Chowdhury, "Real-time physical modelling for analog tape machines", DAFx 2019). It runs oversampled and has a Bias control.
- **Age:** playback losses that depend on tape speed (Bertram head-gap and spacing loss), head bump, hiss, and random dropouts.
- **Tape Stop/Start:** a pitch ramp down to a halt, and back up again.
- **Stereo:** linked, or wide (decorrelated modulation per channel).

## Licensing (Jack agreed 2026-10-20)
- ChowTape is GPL, so none of its code is used.
- Implement from the published papers, plus the MIT-licensed Airwindows tape plugins, with credit given.

## Panel design (chosen: B, Above the clouds)
- A dawn sky above a sea of clouds, with jet contrails. The contrails bend more as Wow rises.
- The sun is the bypass button. When bypassed, the sun sinks into the clouds and the controls dim.
- Each format has its own sky, shown in the table above. Skies crossfade over 1.6 s.
- The sun climbs higher with each format, the way the moon changes phase across Overcast's machines. Sun top position by format: 214, 196, 176, 160, 140, 118 px. Bypassed: 236 px.
- Animation (rolling clouds, twinkling, the tracking band) stops when macOS Reduce Motion is on.
- Alternatives that were not chosen: A (Dawn over water) and C (Retro sunrise). They are kept in docs/mockup.

## Mockup files
- The `docs/mockup/*.dc.html` files are design-canvas boards: AboveClouds is the interactive panel, and AllFormats shows all six skies.
- Each board holds its sky data in a `SKIES` array, which the plugin should match.
- They need the canvas runtime (`support.js`) to render. The live version is the mockup link in the README.

## Decisions made while building milestone 1 (2026-10-20)
- Wow and Flutter knobs are peak speed deviation in % (0 to 2% and 0 to 1%).
- Drift is 0 to 100%, shown as Steady, Slow, Medium, Fast or Wander. Age is 0 to 100%, shown as New, Light, Medium or Heavy.
- Drive is -6 to +18 dB into the tape, with the same amount taken off after.
- The Sync button turns tempo sync on; the Rate setting is its note value, and reads "Free" when Sync is off.
- Bias is a host parameter (nominal 50) for now; the panel has no knob for it.
- Fixed 25 ms latency so Mix and bypass line up exactly.
- Presets and A/B leave Bypass and Tape Stop alone.
- Tuning 1 (Jack, 2026-10-20): VHS wobble made more obvious (Wow 0.75%, faster and lumpier wow). Distortion smoothed: narrower hysteresis loops (as properly biased tape), 3 dB more headroom before the knee, and a touch of second harmonic for warmth. Formats re-levelled.
