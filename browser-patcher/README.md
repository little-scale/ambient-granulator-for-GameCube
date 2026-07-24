# GameCube Ambient Granulator standalone sample-bank patcher

This is a single-file, offline browser editor for GameCube Ambient Granulator.
It does not upload audio, call an API, require an account, or need a local web
server.

Open `standalone/gamecube-granulator-patcher.html` directly in a current
browser. You can begin by adding audio, open an existing `sample_bank.bin`, or
open `gamecube_ambient_granulator.dol` to edit its embedded bank. The patcher
can:

- decode multiple browser-supported audio files and downmix them to mono;
- preview, rename, trim, gain-adjust, reorder, remove, and clear samples;
- convert to signed PCM16 mono at 48 kHz with a cached 32-tap
  Blackman-windowed sinc resampler;
- open legacy 16.384 kHz banks and upgrade them to 48 kHz on export;
- CRC-check opened samples and CRC-protect every exported sample;
- display the 16 MiB external-bank limit, 64-entry limit, and the separate
  fixed capacity of an opened DOL;
- export a compact external `sample_bank.bin`; or
- safely replace the DOL's reserved embedded-bank slot without resizing or
  changing bytes outside that slot.

## Recommended hardware workflow

Download `sample_bank.bin` and copy it to this directory on the SD2SP2 card:

```text
/gamecube-ambient-granulator/sample_bank.bin
```

Keep the standard `gamecube_ambient_granulator.dol` beside it. The external
bank loads first, so changing samples does not require rebuilding the
application.

## Patched DOL workflow

Open the standard DOL, edit its samples, then choose **DOWNLOAD PATCHED DOL**.
This is useful for Dolphin or a self-contained hardware build. The patcher only
enables this export when the edited bank fits the exact slot already reserved
inside that DOL. Remove or rename an external `sample_bank.bin` while testing a
patched DOL because the external bank intentionally has priority.

## Rebuilding and testing

The finished HTML is generated in `standalone/`. Rebuilding and testing the
authoring sources requires only Node.js; there are no packages to install:

```sh
npm test
npm run build
```

To regenerate the repository's native bank from its ordered WAV files:

```sh
npm run bank
```
