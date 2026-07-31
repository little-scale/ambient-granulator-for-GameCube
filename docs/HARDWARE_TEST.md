# First GameCube hardware test

## Prepare the SD card

1. Format the card as FAT32 if Swiss does not already read it reliably.
2. Run `./tools/package-sd.sh` on the Mac.
3. Copy `sd-card/gamecube-ambient-granulator` to the SD-card root.
4. Insert the SD2SP2 adapter into Serial Port 2 while the console is off.
5. Start Swiss and launch `gamecube_ambient_granulator.dol`.

## What a successful boot looks like

- The upper-left heading says `GRAIN Sxx` and the upper-right shows `GC 0.14`.
- The Control view shows the current bank size and a randomly selected sample.
- The tracker font, both parameter columns, and meters are inside the TV safe
  area.
- The underrun count remains at zero while idle.
- `RT` shows current and worst 1024-frame render time; both should remain
  below the 21.3 ms buffer duration.
- At boot, `AUDIO TEST` appears, the configured grain burst begins from the
  randomly selected sample, and Freeze switches on after the complete burst
  has excited the reverb. The `GRAIN` count rises without controller input.

If the Control view says `EMBEDDED`, the DOL is running from its complete
fallback bank, but the external bank was not mounted or opened. Check the
exact path and filename first:

```text
/gamecube-ambient-granulator/sample_bank.bin
```

## Listening pass

1. Confirm the automatic startup texture sounds, the grain counter rises by
   the configured `GRAINS` amount, Freeze reads `ON`, `BUF` keeps advancing,
   and `UND` remains zero.
2. Tap B once. A short burst should sound on release and the grain counter
   should rise.
3. Hold A. Bursts should repeat continuously; release A to stop new grains.
4. Press START. The display should switch to the full-screen black waveform;
   press START again to return to Control view without interrupting audio.
5. In Control view, use the D-pad to move around both parameter columns. Hold
   a direction and confirm it repeats, then accelerates after about one second.
6. Hold B and press Left/Right for fine edits or Down/Up for coarse edits. B
   plus a direction must not also trigger a grain burst when released.
7. Move the C-stick left/right and tap B. The source position should change.
8. Move the main stick vertically. Pitch should move by up to one octave.
9. In Control view, press X and Y. The sample name and waveform should change
   without a crash.
10. Switch to Waveform view. Short ticks at the screen edges indicate detected
    transients. X and Y should jump to the next and previous ticks and wrap at
    the ends. If `TRANS 000` is shown, they should not move the playhead.
11. Freeze starts on after the startup burst. Press L to release the frozen
   tail; excite the reverb again, then press L to hold the new tail.
12. Leave A held for at least two minutes while moving both sticks and changing
   samples. Note the final underrun count and the highest `RT MAX` value.
13. Hold Z and press START to return to Swiss.

## Report back

The most useful first report is:

- console region/model and video cable;
- SD card capacity/brand and SD2SP2 adapter;
- whether the source line showed SD2SP2 or embedded;
- whether B and held-A both produced audio;
- final underrun count after two minutes;
- any control that felt inverted or too sensitive;
- a photo of the screen if text is cropped.
