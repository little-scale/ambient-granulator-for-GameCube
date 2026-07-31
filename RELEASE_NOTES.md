# Ambient Granulator for GameCube v0.14

Version 0.14 starts making sound without controller input. On boot it selects
one of the embedded samples at random, launches the configured grain burst
(eight grains by default), and enables Freeze after the complete burst has
populated the reverb. The result is a randomized sustained texture immediately
after startup.

The previous source-sample set has been replaced by five piano samples:
`01 - piano`, `03 - piano`, `05 - piano`, `07 - piano`, and `18 - piano`.
The copyright holder has dedicated these WAV files and their generated
sample-bank audio to the public domain under CC0 1.0 Universal. The application
source remains MIT licensed.

The release includes the standard DOL with the five-sample bank embedded, the
matching SD2SP2 bank, the self-contained offline browser patcher, licences, and
checksums. The build now discovers all WAV files in `samples/` and regenerates
the bank whenever those files change.

Verification covers the host granular engine, effects chain, output meter,
bank decoding/CRCs, input repeat logic, transient analysis, all five samples,
the browser patcher, archive integrity, and native big-endian PowerPC ELF
validation. Physical GameCube and SD2SP2 acceptance remains the documented
manual gate in `docs/HARDWARE_TEST.md`.
