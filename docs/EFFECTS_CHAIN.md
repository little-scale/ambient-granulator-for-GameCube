# Stereo effects-chain contract

The native effects chain processes the granular engine's 48 kHz interleaved
stereo output before GameCube audio DMA submission. Grain voices accumulate
into a signed 32-bit scratch mix so polyphonic overload is still available to
the effects input rather than being hard-clamped prematurely to PCM16.

The signal order is:

```text
granular voices -> wide mix -> soft knee -> four-phaser ensemble
                -> input diffusion -> eight-line FDN + output taps
                -> wet/dry -> HPF -> LPF -> soft clip -> meter -> audio DMA
```

## Phaser

The `PHASE` control now drives four parallel four-stage phasers per stereo
channel. Their LFOs begin a quarter-cycle apart and run at 0.89x, 1.00x, 1.13x,
and 1.27x the displayed `P SPD`; the right channel receives an additional
phase rotation. The four notched signals are averaged before the Depth mix,
producing slow movement without a single obvious sweep. Depth runs from
0–100%; Speed runs from 0.01–1.00 Hz and defaults to 0.10 Hz. At zero Depth the
ensemble work and LFO update are bypassed entirely.

Placing it before the FDN lets the reverb soften the moving phase colour and
means Freeze captures a stable tail rather than continually sweeping the held
reverb. The phased signal is both the direct side of the wet/dry crossfade and
the signal that excites the FDN.

## Diffusion and FDN reverb

Four true delay allpasses per channel diffuse the phaser output before it
enters the reverb. Their mutually offset prime lengths spread a grain across
roughly 3–16 ms stages, creating a bloom before the long feedback lines become
audible.

The feedback network expands the DS-derived topology from four to eight delay
lines. An energy-normalized 8x8 Hadamard transform mixes feedback efficiently,
while the original Size curve is retained through 55. Above 55, a quadratic
extension reaches roughly 411–1261 ms at Size 100. Each line contributes its
main output plus two differently signed fractional-length taps, providing 24
decorrelated reverb taps without additional delay memory. Damping's one-pole
coefficient remains normalized for 48 kHz.

Wet/Dry affects only the output crossfade. The FDN continues receiving dry
excitation at zero Wet, matching the DS behaviour, so raising Wet can reveal an
existing tail.

Feedback is adjustable from 0.0% to 99.9% in 0.1% fine steps and 1% coarse
steps. The upper limit deliberately remains just below unity for a very long
decay; Freeze is the exact unity-feedback mode.

## Freeze

Freeze has the same state semantics as the reference implementation:

- no new dry excitation enters the FDN;
- feedback is forced to unity;
- damping is bypassed;
- saved Feedback and Damp parameter values remain untouched;
- dry grains remain audible according to Wet/Dry.

L toggles the same Freeze parameter that can be selected and edited in the
grid.

## Output filters

Independent first-order state is maintained for left and right channels. HPF
and LPF run after the reverb wet/dry mix, with coefficients recalculated for
48 kHz.
The control range remains DS-compatible: HPF 0–4 kHz, LPF 200 Hz–8 kHz, with
HPF 0 and LPF 8 kHz displayed as `OFF` and bypassed. D-pad Left/Right edits in
10 Hz steps; holding Z retains 500 Hz coarse sweeps.

## Output meter

The stereo bars and dBFS readout observe the final signed 16-bit PCM after both
filters and immediately before cache flush and audio DMA submission. Each
1024-frame audio block captures independent absolute L/R peaks. The full-width
stereo bars use fast attack and a roughly one-second visual decay, while the numerical
readout reports the louder channel in dBFS.

The wide grain input and non-frozen FDN use a cheap rational soft knee above
30000 rather than a flat hard clip. A separate unity-gain output soft clipper
is placed after both filters and outside the FDN feedback loop. It remains
bit-exact below its -2 dBFS knee (26028), then bends smoothly toward full scale
without calling floating-point `tanh`; it applies equally during Freeze without
altering or damping the held tail. If the raw grain mix, an internal FDN
operation, or pre-clipped final output exceeds signed 16-bit range, the inverted
`CLIP` block is held for approximately one second.
Consequently a 100%-wet reverb can now report an overloaded excitation even if
the delayed output peak itself is modest. Gain, Vol, density, reverb level, or
Feedback should be reduced when `CLIP` remains active.

## Acceptance limits

Host-rendered tests cover coefficients, dry identity, wide polyphonic sums,
internal and final soft overload behavior and telemetry, stereo phaser motion, stereo tail
generation, early-reflection density, live sample changes through a frozen tail, Freeze excitation
blocking/unity feedback, and filter responses. Dolphin's audio dump confirms
interactive PCM output. The Control view's `RT current MAX peak` readout measures
the time needed to render each 21.33 ms audio block. Feedback stability,
speaker presentation, and final CPU headroom still require a physical GameCube
test.
