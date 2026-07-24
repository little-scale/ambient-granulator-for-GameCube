# Ambient Granulator for GameCube

First playable Nintendo GameCube port of the 3DS Ambient Granulator. It keeps
the platform-neutral granular engine, stereo effects, output meter, waveform
analysis, and `NDSGRN01` sample-bank format, while replacing the 3DS services
with libogc video, controller, SD2SP2, and direct audio DMA code.

Microphone input is deliberately not part of this port.

## Try it in Dolphin

The repository includes the complete ten-sample bank inside the DOL, so all
samples work in emulation without a virtual SD card.

```sh
./tools/build.sh
./tools/run-dolphin.sh
```

The build is reproducible in the pinned official container
`devkitpro/devkitppc:20260503`. Docker Desktop is the only build dependency.
The launcher looks for Dolphin at `/Applications/Dolphin.app` by default; set
`DOLPHIN_BIN` if it is elsewhere.

In Dolphin, configure port 1 as a Standard Controller and map a real controller
or keyboard before testing the performance controls.

Dolphin's default macOS keyboard map is easy to misread: the keyboard letters
are not the same as the GameCube button labels. With the default map use:

| Keyboard | GameCube input / app action |
|---|---|
| Z | B — tap for a burst; hold with D-pad to edit |
| X | A — hold the grain gate |
| C / S | X / Y — samples on Control; transients on Waveform |
| Return | START — switch view |
| D + Return | Z + START — exit |
| T / G / F / H | D-pad Up / Down / Left / Right |
| Arrow keys | Main stick |
| I / K / J / L | C-stick |
| Q / W | L / R triggers |

The Control view's persistent `GRAIN` counter confirms whether a trigger
reached the synthesis engine. If it rises and the peak meters move, the ROM is
producing audio even if the desktop audio backend is muted or misconfigured.
Version 0.13 also performs one automatic grain audition shortly after boot so
the entire audio path can be checked without a controller mapping.

## Controls

| Control | Action |
|---|---|
| Hold A | Continuous grain gate |
| Tap B | Trigger one grain burst |
| D-pad | Navigate the two-column parameter grid |
| Hold B + Left/Right | Decrease/increase by the fine step |
| Hold B + Down/Up | Decrease/increase by the coarse step |
| Hold a direction | Repeat, then accelerate after one second |
| X / Y on Control | Next / previous sample |
| X / Y on Waveform | Next / previous detected transient |
| L | Toggle reverb Freeze |
| R | Toggle synced/free timing |
| Main stick X/Y | Live pan / pitch offset |
| C-stick X | Move the sample playhead |
| C-stick Y | Change grain position range |
| START | Switch control/waveform view |
| Z + START | Exit to the loader |

## Two-screen GUI port

The 3DS interface is represented as two full-TV views. The initial **Control**
view ports the original upper-screen tracker layout: black-on-white custom 5x7
font, two parameter columns, section rules, an inverted selected value, audio
status, and stereo meters. The **Waveform** view ports the lower screen: a
black performance surface with a full 320-column white waveform, solid
playhead, dashed grain-range boundaries, and cuts at actual grain launches.
Short edge ticks show the prominent transients found during sample loading.
X and Y jump the playhead to exact transient sample positions, wrapping at the
ends; a sample with no detected transients does not move.

Both pages use a 320x240 logical canvas expanded exactly 2x to the GameCube
framebuffer. Framebuffer updates are performed as aligned YUYV pixel pairs so
the monochrome palette and glyph pixels remain stable on hardware and Dolphin.

Press START at any time to switch views. Audio and all controller input remain
active during the switch.

The parameter controls follow the original 3DS grammar: directions alone move
through the spatial grid, while B plus a direction edits the selected value.
A quick B press with no direction remains the grain-burst gesture. Held
directions repeat after 15 frames, repeat every three frames, then advance once
per frame after a one-second hold.

## Try it on a GameCube with SD2SP2

Run the packaging helper:

```sh
./tools/package-sd.sh
```

Copy the resulting `sd-card/gamecube-ambient-granulator` directory to the root
of a FAT32 SD card. The finished layout is:

```text
/gamecube-ambient-granulator/
├── gamecube_ambient_granulator.dol
└── sample_bank.bin
```

Launch the DOL with Swiss. The app mounts Serial Port 2 through SD2SP2 and
loads `/gamecube-ambient-granulator/sample_bank.bin`. If that bank is absent or
invalid, it falls back to the complete bank embedded in the DOL.

The staged SD bank and embedded fallback contain the same ten mono 48 kHz
samples. Only the selected entry is decoded into a separate runtime buffer.

## Change the sample bank in a browser

Open
`browser-patcher/standalone/gamecube-granulator-patcher.html` directly in a
current browser. It is one offline file: no upload, account, server, or package
installation is involved. Add WAV/audio files or open an existing
`sample_bank.bin`, then preview, trim, rename, gain-adjust, reorder, or remove
samples.

For hardware, download `sample_bank.bin` and copy it to
`/gamecube-ambient-granulator/` on the SD2SP2 card. For Dolphin or a
self-contained image, open the standard DOL first and download a patched DOL.
Patched-DOL export preserves the original executable size and is only enabled
when the new bank fits its existing embedded slot. An external bank takes
priority, so remove it while testing embedded samples.

The browser patcher has its own dependency-free verification suite:

```sh
cd browser-patcher
npm test
```

## Build and verification

```sh
./tools/test.sh
./tools/build.sh
./tools/verify-build.sh
```

Outputs:

- `gamecube_ambient_granulator.dol` — launch this in Dolphin or Swiss;
- `gamecube_ambient_granulator.elf` — symbols and debugging information;
- `build/gamecube_ambient_granulator.elf.map` — linker map.

The host suite covers the granular scheduler and renderer, effects chain,
metering, CRC validation, and little-endian bank decoding. The native check
then confirms an ELF32, big-endian PowerPC executable.

To rebuild the embedded bank from one or more PCM WAV files:

```sh
node tools/build-sample-bank.mjs samples/1.wav
node tools/build-sample-bank.mjs \
  --output=data/sample_bank.bin samples/1.wav samples/another.wav
```

The builder accepts 16- or 24-bit integer PCM WAV audio, downmixes it to mono,
resamples to 48 kHz, writes little-endian PCM16, and adds per-sample CRCs.

## Current milestone

Working in this first port:

- callback-driven 48 kHz stereo libogc DMA with three aligned buffers;
- dedicated PowerPC audio-rendering thread, independent of display VBlank;
- 16-voice granular synthesis with free and tempo-synced scheduling;
- eight-line diffused reverb with 24 output taps and stable Freeze;
- four offset phasers, high-pass, low-pass, and output metering;
- live current/peak audio render-time diagnostics;
- switchable ports of the original 3DS control and waveform screens;
- custom framebuffer renderer with the original 5x7 tracker font;
- waveform, playhead, actual grain markers, parameters, and diagnostics;
- complete ten-sample embedded bank plus the matching SD2SP2 override;
- endian-safe reuse of 3DS/DS `NDSGRN01` banks;
- Dolphin boot and host/native automated verification.

Still to validate on physical hardware:

- sustained audio timing and underrun count;
- analog stick feel and parameter step sizes;
- PAL/NTSC console legibility and safe area;
- SD card/adapter compatibility across cold boots;
- DSP load at the most demanding effect settings.

See [docs/HARDWARE_TEST.md](docs/HARDWARE_TEST.md) for the first console test
pass and [docs/PORTING_NOTES.md](docs/PORTING_NOTES.md) for implementation
details.

## Licence and releases

The software is MIT licensed; see [LICENSE](LICENSE). The bundled WAV files
are copyright-free source material approved for redistribution; see
[samples/README.md](samples/README.md). See [RELEASING.md](RELEASING.md) for
the GitHub release procedure, checksums, and hardware-verification wording.
