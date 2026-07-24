# Ambient Granulator for GameCube v0.13

This first public GameCube release brings Ambient Granulator's granular engine
to libogc with 16 voices, a stereo effects chain, real-time output meters,
waveform/transient navigation, an embedded ten-sample bank, SD2SP2 override,
and a self-contained offline browser patcher.

The release includes the standard DOL, a matching SD2SP2 bank, and a browser
patcher that can prepare external banks or patch an embedded bank that fits the
existing DOL slot. The complete source WAV set is copyright-free and included
in the repository.

Verification covers the host granular engine, effects chain, output meter,
bank decoding/CRCs, input repeat logic, transient analysis, browser patcher,
and native PowerPC ELF validation. Dolphin boot is supported by the included
launcher. Physical GameCube checks remain the documented manual gate in
`docs/HARDWARE_TEST.md`.
