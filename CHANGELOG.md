# Changelog

## 0.14 — 2026-07-31

- Select a random sample at boot, launch the configured grain burst
  automatically, and enable Freeze after the burst has populated the reverb.
- Replace the previous ten source samples with five piano samples and dedicate
  the WAVs and generated sample-bank audio to the public domain under CC0 1.0
  Universal.
- Rebuild both the embedded and SD2SP2 banks automatically when WAV files in
  `samples/` change.
- Update sample-bank, transient-analysis, and browser-patcher verification for
  content-independent banks and the new five-sample release.

## 0.13 — 2026-07-24

- First public GameCube release with a 16-voice granular engine, stereo
  effects chain, two-view GameCube interface, embedded/SD2SP2 sample bank, and
  offline browser patcher.
- Add host granular, effects, meter, sample-bank, edit-repeat, and transient
  analysis tests, plus browser patcher bank, WAV, DOL, and standalone tests.
- Package the complete ten-sample bank in the DOL and as an SD2SP2 override.
- Document Dolphin and first physical-console test procedures.
