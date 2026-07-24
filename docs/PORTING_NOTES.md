# GameCube porting notes

## Reused modules

The following modules came from the 3DS project with no platform services in
their public interfaces:

- granular engine and sample-clock scheduler;
- stereo effects chain;
- output peak/clip meter;
- waveform min/max analysis.

The sample-bank reader is also shared conceptually, but the GameCube port
decodes every little-endian PCM16 value explicitly. Directly casting the bank
bytes to `int16_t` worked on the little-endian 3DS CPU and is incorrect on the
big-endian PowerPC CPU.

## Audio path

The GameCube audio interface DMA plays 48 kHz, signed stereo PCM16 big-endian
buffers directly. Three 1024-frame, 32-byte-aligned buffers are used:

1. DMA starts with the first pre-rendered buffer.
2. Its interrupt callback submits the next pre-rendered buffer.
3. The third pre-rendered buffer provides startup headroom, after which a
   semaphore wakes the audio thread to refill each freed buffer.
4. The audio thread snapshots controls, renders grains and effects, updates
   diagnostics, flushes the data cache, and publishes the buffer.

The callback performs no synthesis, allocation, file access, or locking.

## Sample loading and memory

The DOL embeds the complete ten-sample bank for a zero-setup Dolphin boot. On
hardware the app first mounts `__io_gcsd2` as `sd2:` and opens:

```text
sd2:/gamecube-ambient-granulator/sample_bank.bin
```

The bank directory stays open, but only the selected entry is allocated and
decoded into native-endian RAM. The audio-state mutex makes a sample pointer
swap atomic relative to rendering, after which the previous sample is freed.
Reverb state is intentionally retained across sample changes.

Each newly loaded sample is also scanned once on the UI thread for adaptive
amplitude-envelope rises. A prominence pass rejects small rises inside a
larger decay and suppresses competing peaks around a dominant attack. Detected
onset positions are stored as sample offsets, not screen columns. Transient
stepping therefore feeds the granular engine an exact source offset while
keeping the 320-column playhead only as a display coordinate. Silence produces
an empty map and leaves the playhead unchanged.

The full embedded bank makes the static linked image about 12.1 MiB, leaving
roughly 11.9 MiB of the GameCube's 24 MiB main RAM for the heap and system
allocations. Runtime allocations include only the selected decoded sample,
roughly 1.0 MiB of eight-line FDN and diffuser memory, the framebuffers, three
4 KiB output buffers, and one render scratch buffer. An external SD bank does
not allocate the whole bank again.

## Deliberate differences from 3DS

- no microphone capture, gain control, RAM punching, touch input, or 3DS DSP
  firmware dependency;
- the two simultaneous 3DS displays become switchable Control and Waveform
  views on one TV, selected with START;
- a direct double-buffered grayscale XFB renderer replaces Citro2D while
  retaining the original 5x7 font, tracker grid, waveform contrast, range
  guides, and grain-position marks;
- GameCube PAD grammar in place of touch controls, while retaining the 3DS
  two-column D-pad navigation, B-plus-direction fine/coarse editing, tap-B
  burst, and accelerating held-direction repeat;
- samples stream from an SD2SP2 file one entry at a time instead of preloading
  the full library;
- libogc audio DMA in place of NDSP/libctru.
