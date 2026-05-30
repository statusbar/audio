# statusbar-audio documentation

The `statusbar-audio` package is a set of audio modules built on
`statusbar-core`. Each module is documented in its own overview file;
pick a module to get a two-minute orientation, then dive into its
headers under `statusbar/<module>/`.

## Module overviews

| Module  | Purpose                                                                              | Link                                 |
|---------|--------------------------------------------------------------------------------------|--------------------------------------|
| audio   | Cross-platform low-latency audio I/O — devices, streams, format conversion, BW64.    | [AUDIO_MODULE.md](AUDIO_MODULE.md)   |
| dsp     | Real-time DSP primitives — biquads, smoothed gain, oscillator, levels, SIMD vec.     | [DSP_MODULE.md](DSP_MODULE.md)       |
| engine  | Tiered real-time DSP graph — Elements driven by a slow control tier via lock-free pipes. | [ENGINE_MODULE.md](ENGINE_MODULE.md) |
| ltc     | SMPTE Linear Timecode generation and playback with an audio-clock servo.             | [LTC_MODULE.md](LTC_MODULE.md)       |
| midi    | MIDI 1.0 messages, stream parser, SMF Type 0/1 read/write, processor pipeline.       | [MIDI_MODULE.md](MIDI_MODULE.md)     |
| osc     | Open Sound Control 1.0 wire-format codec — messages, bundles, typed arguments.       | [OSC_MODULE.md](OSC_MODULE.md)       |

## Topic guides

In-depth references that go beyond the per-module overviews:

- [LTC_DESIGN.md](LTC_DESIGN.md) — SMPTE LTC design notes: bit-level frame layout, biphase-mark encoding, drop-frame arithmetic, and the audio-clock servo loop.
