# Local source samples

Source WAV files are included in Git. They are copyright-free source material
approved by the project owner for redistribution in both WAV form and as the
generated mono 48 kHz PCM16 banks at `data/sample_bank.bin` and
`sd-card/gamecube-ambient-granulator/sample_bank.bin`.

The current bank uses this order:

1. `1.wav`
2. `2.wav`
3. `3.wav`
4. `4.wav`
5. `5.wav`
6. `6.wav`
7. `110bpm F - 01 - Hiskee Vocalpack.wav`
8. `130bpm Am - 05 - Hiskee Vocalpack.wav`
9. `sample1.wav`
10. `piano.wav`

Stereo input is intentionally averaged to mono. The native granular engine
spatializes each grain at runtime with Pan and Pan Deviation.
