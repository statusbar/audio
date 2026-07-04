# statusbar-audio

Audio toolkit — DSP primitives, the real-time data-flow engine, low-latency cross-platform audio I/O, SMPTE timecode, MIDI, and Open Sound Control.

Version 1.4.0.

> Portions of this repository were developed with assistance from Claude,
> an AI model by Anthropic. All reference material used in this process
> came from my own original open source implementations of these audio
> and DSP modules, with Claude assisting in refactoring and in validating
> conformance against the relevant MIDI, Open Sound Control, SMPTE, and
> BW64 specifications. All architectural decisions, final implementations,
> and engineering judgments are my own, and any errors are mine alone.

## Overview

`statusbar-audio` is an audio toolkit that sits on top of `statusbar-core`,
reaching from the hardware up to a real-time processing graph. It bundles
cross-platform low-latency device I/O (with sample-format conversion and BW64
capture), a set of real-time DSP primitives, and a tiered data-flow engine
that drives those primitives with lock-free parameter updates.

Alongside the DSP core it carries the media-protocol pieces an audio
application tends to need: SMPTE Linear Timecode generation and playback with
an audio-clock servo, MIDI 1.0 messaging with Standard MIDI File read/write,
and an Open Sound Control 1.0 codec. The DSP layer is SIMD-accelerated (NEON,
AVX).

It is aimed at developers building audio applications and devices, and
depends only on `statusbar-core`.

## Quick start

```bash
# Local build + unit tests (Clang+libc++ toolchain is mandatory; applied automatically)
./local-build.sh && ctest --test-dir build

# Reproducible Debian .deb in ../deb-output/ (statusbar-core .debs must already be there)
./container-build.sh

# Sanitizers (mutually exclusive — use separate build dirs)
./local-build.sh -DENABLE_ASAN=ON     # AddressSanitizer
./local-build.sh -DENABLE_UBSAN=ON    # UndefinedBehaviorSanitizer
./local-build.sh -DENABLE_TSAN=ON     # ThreadSanitizer
```

Fuzz harnesses cover the MIDI and OSC parsers (build with `-DENABLE_FUZZING=ON`; see [Fuzz testing](#fuzz-testing)). Depends on `statusbar-core`. See the sections below for details.

## Modules

Per-module overviews live in [`docs/`](docs/README.md):

- [`audio`](docs/AUDIO_MODULE.md) — cross-platform low-latency audio I/O, sample-format conversion, BW64 capture.
- [`dsp`](docs/DSP_MODULE.md) — real-time DSP primitives plus a SIMD vector abstraction (NEON, AVX).
- [`engine`](docs/ENGINE_MODULE.md) — tiered real-time DSP data-flow graph with lock-free parameter updates.
- [`ltc`](docs/LTC_MODULE.md) — SMPTE Linear Timecode generation and playback with an audio-clock servo.
- [`midi`](docs/MIDI_MODULE.md) — MIDI 1.0 messages, stream parser, SMF Type 0/1 read/write, processing pipeline.
- [`osc`](docs/OSC_MODULE.md) — Open Sound Control 1.0 wire-format codec.

## Building

This package depends on other statusbar packages. Export every
package into one directory so the trees sit side by side, then
build the dependencies first — each builds on its own (see its
README) — in this order:

    statusbar-core -> statusbar-audio

This package then finds each dependency through its **local
build tree** — nothing is installed, so your system stays clean.

**Quick path:** run `./local-build.sh`. It looks for each dependency's
build dir in a sibling checkout (`../<dep>/build/`), threads the `-Dstatusbar-<dep>_DIR` flags into
cmake automatically, and falls back to `$STATUSBAR_<DEP>_DIR` when the
layout differs. Extra args are forwarded to cmake configure
(`./local-build.sh -DENABLE_ASAN=ON`). The script echoes its final cmake
invocation via `set -x` so users configuring an IDE can copy the exact
flags.

**Manual / IDE configuration.** The explicit cmake invocation is:

```
cmake -S . -B build -G Ninja --toolchain cmake/toolchain-clang.cmake \
  -Dstatusbar-core_DIR=../core/build
cmake --build build
ctest --test-dir build
```

Each `-Dstatusbar-<dep>_DIR=<path>` points `find_package` at a
dependency's build directory; the package exports its build tree,
so it need not be installed. The `../<dep>/build` paths
assume the packages were exported as siblings — use absolute paths
otherwise. In an IDE's CMake settings, add one cache entry per
dependency (`statusbar-core_DIR`) pointing at the dep's build
directory; this is exactly what the script generates.

## Benchmarks

Micro-benchmark executables ship with audio:

| Binary | Source | CMake target |
|---|---|---|
| `statusbar-dsp-bench` | `statusbar/dsp/dsp_bench_tool.cpp` | `run-dsp-bench` |
| `statusbar-engine-bench` | `statusbar/engine/engine_bench_tool.cpp` | `run-engine-bench` |

Run via CMake (rebuilds if stale, then executes):

```
cmake --build build --target run-dsp-bench
cmake --build build --target run-engine-bench
```

Or directly after a build:

```
./build/statusbar/dsp/statusbar-dsp-bench
./build/statusbar/engine/statusbar-engine-bench
```

The dsp bench covers biquads, gain, oscillators, and level meters; the
engine bench covers tiered real-time graph processing. Each reports
mean / median / std-dev / min / max / p95 / p99 for every named case.

## Coverage

Line/region coverage is wired through LLVM source-based profiling
(`-fprofile-instr-generate -fcoverage-mapping`). Enable via
`-DENABLE_COVERAGE=ON`, ideally in a separate build directory:

```
cmake -S . -B build-cov -G Ninja --toolchain cmake/toolchain-clang.cmake \
  -DENABLE_COVERAGE=ON
```

Three CMake custom targets become available after configure (they
require `llvm-profdata` and `llvm-cov` on `PATH`):

| Target | What it does |
|---|---|
| `coverage-collect` | Builds and runs `statusbar_test`, merges `.profraw` into `combined.profdata` |
| `coverage-report` | Prints a text line-coverage report |
| `coverage-html`   | Generates an HTML report under `build-cov/coverage/html/` |

Each target depends on the previous, so a single invocation runs the
whole pipeline:

```
cmake --build build-cov --target coverage-html
xdg-open build-cov/coverage/html/index.html   # or `open` on macOS
```

`*_test.cpp` / `test.hpp` files are excluded by default. Override with
`-DSTATUSBAR_COVERAGE_IGNORE='<regex>'` if you want a different filter.

## Fuzz testing

libFuzzer harnesses cover the untrusted-byte parsers: the MIDI Standard-MIDI-File
reader (`midi_file_reader_fuzzer`), the byte-by-byte MIDI stream parser
(`midi_parser_fuzzer`), and the OSC wire-format decoder (`osc_deserializer_fuzzer`).
They build only under `-DENABLE_FUZZING=ON` and run via the repo's
`container-fuzz.sh` (or the auto-discovered `fuzz-smoke` / `fuzz-all` targets).

## Sanitizers

`cmake/sanitizers.cmake` exposes three mutually exclusive sanitizer options.
Use a **separate build directory per sanitizer** — the instrumentation
flags change ABI / runtime expectations:

```
cmake -S . -B build-asan -G Ninja --toolchain cmake/toolchain-clang.cmake \
  -DENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan
```

| Option | Adds | Use for |
|---|---|---|
| `-DENABLE_ASAN=ON`  | `-fsanitize=address`                       | Out-of-bounds, use-after-free, leaks |
| `-DENABLE_UBSAN=ON` | `-fsanitize=undefined -fno-sanitize-recover` | Integer overflow, alignment, UB |
| `-DENABLE_TSAN=ON`  | `-fsanitize=thread`                        | Data races (useful for SPSC + triple-buffer code) |

Enabling more than one of the three at configure time is a hard error.

## Static analysis (clang-tidy)

Two modes are available:

**In-compile** — `clang-tidy` runs as part of every translation unit:

```
cmake -S . -B build-tidy -G Ninja --toolchain cmake/toolchain-clang.cmake \
  -DENABLE_CLANG_TIDY=ON
cmake --build build-tidy
```

Slow (every TU lints), but findings show up alongside the compile output.

**Separate aggregate run** — runs `clang-tidy` in parallel against the
existing build's `compile_commands.json`:

```
cmake --build build                         # normal build first
cmake --build build --target clang-tidy     # runs against build/compile_commands.json
```

The `clang-tidy` target uses `cmake/run-clang-tidy-all.sh` to fan out
across cores (8 jobs by default), skips `*_test.cpp` / `*_tool.cpp` /
`*_example.cpp` / `*_fuzzer.cpp`, and writes combined findings to
`build/clang-tidy-findings.txt`. The `.clang-tidy` config at the
submodule root drives check selection.

## Installing

```
cmake --install build --prefix <prefix>
```

## Packaging

A standalone build configures CPack — TGZ and ZIP archives on every
platform, plus DEB and (when `rpmbuild` is present) RPM on Linux.
Run `cpack` from the build directory:

```
cd build && cpack
```

This produces `statusbar-audio` (runtime: tools) and `statusbar-audio-dev` / `-devel` (headers, static libraries, CMake config).

### Reproducible Debian packages

`./container-build.sh` builds this package's `.deb`s inside a Debian
container — no local toolchain needed. Output lands in `../deb-output/`
(override with `DEB_OUTPUT`); the base image is configurable with
`DEBIAN_VERSION=...`.

The `statusbar-core` `.deb`s must already be present in
`../deb-output/` — build them by running `./container-build.sh` in a
sibling clone of the `statusbar-core` repo first.
