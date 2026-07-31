# Ambient Granulator for GameCube v0.15

Version 0.15 adds a hands-off Kiosk mode for installations and unattended
listening. The ROM now opens in Waveform view with `KIOSK MODE` visible. When
no controller activity is detected, it selects a random sample and chooses a
pitch from -12, -7, 0, +7, or +12 semitones.

Each kiosk texture thaws the reverb, waits briefly for the audio thread to
accept the change, launches the configured grain burst (eight grains by
default), and refreezes after every requested grain has launched. After a
newly randomized interval of 30–60 seconds, it repeats with a different
sample.

Kiosk mode is strictly idle-only. The first recognized button press or
release, held button, analog trigger, main-stick movement, or C-stick movement
on any of the four controller ports cancels it permanently for that run.
Pending kiosk grains are stopped if intervention occurs during a burst, and
the same input is then handled normally by the performance controls.

The release includes the standard DOL with the five CC0 piano samples embedded,
the matching SD2SP2 bank, the self-contained offline browser patcher, licences,
and checksums. Kiosk operation has been user-confirmed.

Verification covers the host granular engine, kiosk randomization and
cancellation state, effects chain, output meter, bank decoding/CRCs, input
repeat logic, transient analysis, all five samples, the browser patcher,
archive integrity, and native big-endian PowerPC ELF validation. Broader
physical GameCube and SD2SP2 acceptance remains the documented manual gate in
`docs/HARDWARE_TEST.md`.
