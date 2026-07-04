<!-- Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com> -->


# SMPTE LTC Generator Module Design

## Overview

This module generates SMPTE Linear Timecode (LTC) audio signals per SMPTE 12M specification. It supports multiple frame rates including drop frame formats.

## Requirements

1. **Timecode Format**: SMPTE 12M LTC (80-bit biphase mark code per frame)
2. **Frame Rates**: 23.976, 24, 25, 29.97 (drop/non-drop), 30 (drop/non-drop)
3. **Sample Rates**: 44.1 kHz, 48 kHz, 96 kHz
4. **Clock Synchronization**: Asynchronous clock servo to handle drift between audio clock and SMPTE reference
5. **Timing Association**: Map SMPTE time to video frame boundaries

## Technical Background

### SMPTE LTC Format
- **Bit Rate**: 2400 bits/second (80 bits × 30 fps)
- **Encoding**: Biphase mark (Manchester) encoding
- **Frame Structure**: 80 bits per frame containing:
  - Frame number (0-29)
  - Seconds (0-59)
  - Minutes (0-59)
  - Hours (0-23)
  - User bits (32 bits)
  - Sync word (0x3FFD)
  - Drop frame flag, color frame flag

### Sample Rate Calculations
- At 44.1 kHz: 1470 samples/frame (44100 / 30)
- At 48 kHz: 1600 samples/frame (48000 / 30)
- At 96 kHz: 3200 samples/frame (96000 / 30)

Each bit in LTC requires: sample_rate / 2400 samples

## Module Architecture

### File Structure
```
statusbar/ltc/
├── ltc.hpp                     # Main module export
├── ltc_timecode.hpp           # Timecode data structure
├── ltc_timecode.cpp           # Timecode implementation
├── ltc_frame.hpp              # 80-bit LTC frame encoder
├── ltc_generator.hpp          # Audio sample generation
├── ltc_generator.cpp          # Generator implementation
├── ltc_servo.hpp              # Clock drift compensation
├── ltc_servo.cpp              # Servo implementation
├── ltc_playback.hpp           # Playback support
├── ltc_playback.cpp           # Playback implementation
├── ltc_gen_tool.cpp           # Command-line LTC generator tool
└── ltc_test.cpp               # Unit tests
```

### Core Components

#### 1. Timecode Structure (`ltc_timecode.hpp`)
```cpp
struct Timecode {
    uint8_t hours{0};                        // 0-23
    uint8_t minutes{0};                      // 0-59
    uint8_t seconds{0};                      // 0-59
    uint8_t frames{0};                       // 0 to fps-1
    uint32_t user_bits{0};                   // 32-bit user data
    FrameRate rate{FrameRate::Rate_30_NDF};  // Frame rate (drop-frame derived from this)
    bool color_frame{false};                 // Color frame flag
};
```

Drop-frame state is derived from `rate` via `is_drop_frame(rate)`; there is no
separate boolean. Frame counting helpers — `is_valid()`, `increment_frame()`,
`decrement_frame()`, `to_frame_count()`, `from_frame_count()`, and the spaceship
`operator<=>` — are all defined on the struct and `constexpr` where possible.

#### 2. LTC Frame Encoder (`ltc_frame.hpp`)
- Encodes 80-bit LTC word from Timecode
- Implements BCD encoding for time values
- Generates sync word (0x3FFD)
- Calculates parity bits

#### 3. Audio Generator (`ltc_generator.hpp`)
- Converts 80-bit LTC frame to audio samples
- Implements biphase mark encoding:
  - Bit 0: Transition at bit center
  - Bit 1: Transition at bit start and center
- Generates square wave at appropriate frequency
- Manages sample buffer output

#### 4. Clock Servo (`ltc_servo.hpp`)
- Tracks phase difference between audio clock and SMPTE reference
- Implements a proportional-integral (PI) controller
- Adjusts the read rate within a ±0.1% clamp to maintain sync
- Does **not** insert or delete samples; it stretches/compresses playback
  timing within the clamp (see LTC_MODULE.md)

## Implementation Phases

### Phase 1: Core LTC Encoding (Minimal Viable)
1. Timecode structure with validation
2. 80-bit LTC frame encoding
3. Biphase mark encoding to samples
4. Basic sample generation at fixed rate

### Phase 2: Sample Rate Support
1. Support for 44.1 kHz, 48 kHz, 96 kHz
2. Proper sample calculation per bit time
3. Buffer management for continuous output

### Phase 3: Clock Servo
1. Phase tracking between clocks
2. Drift detection
3. Sample rate adjustment algorithm
4. Smooth transition handling

## API Design

### Basic Usage
```cpp
#include "statusbar/ltc/ltc.hpp"

#include <vector>

using namespace statusbar::ltc;

// Create generator with SMPTE 12M compliant rise/fall time
// Default frame rate: 30fps non-drop; default rise time: 40µs (professional standard)
// Rise time range: 25-250µs per SMPTE 12M specification
Generator gen{Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF, 40.0};

// Set initial timecode (rate must match the generator's frame rate)
Timecode tc{.hours = 1, .minutes = 30, .seconds = 0, .frames = 15,
            .rate = FrameRate::Rate_30_NDF};

// Generate samples for one frame (caller controls memory allocation)
std::vector<float> samples;
samples.reserve(gen.samples_per_frame());  // Pre-allocate to avoid reallocations
gen.generate_frame(tc, samples);           // generate_frame() resizes to samples_per_frame()

// For continuous generation with servo
ServoGenerator servo{Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF, 40.0};
servo.set_timecode(tc);

// Optionally called periodically with measured vs. expected wall-clock timestamps
// (both arguments are seconds; the servo nudges its phase to converge).
double actual_timestamp = 0.0;
double expected_timestamp = 0.0;
servo.update_timing(actual_timestamp, expected_timestamp);

// Pull samples into a caller-owned buffer (resized to `count`).
size_t const count = 1024;
std::vector<float> output;
output.reserve(count);
servo.get_samples(count, output);
```

### Rise/Fall Time Control

The generator implements SMPTE 12M bandwidth limiting through configurable rise/fall times:

- **Default**: 40 microseconds (typical professional equipment)
- **Range**: 25-250 microseconds (SMPTE 12M specification)
- **Purpose**: Limits high-frequency content to prevent crosstalk in multi-pair cables
- **Implementation**: Linear interpolation between levels

```cpp
using namespace statusbar::ltc;

// Fast transitions (minimum SMPTE compliant)
Generator gen_fast{Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF, 25.0};

// Slow transitions (maximum SMPTE compliant)
Generator gen_slow{Generator::SampleRate::Rate_48000, FrameRate::Rate_30_NDF, 250.0};
```

## Command-Line Tool Usage

The `ltc_gen_tool` utility generates LTC audio samples to raw binary or WAV files, and can optionally play audio directly through speakers.

### Basic Usage
```bash
# Generate 10 seconds of LTC starting at 01:30:45:15, 48kHz sample rate
ltc_gen_tool --timecode=01:30:45:15 --rate=48000 --duration=10.0 --output=ltc_output.raw

# Generate 25fps LTC as WAV file
ltc_gen_tool --timecode=00:00:00:00 --fps=25 --duration=5.0 --format=wav --output=ltc.wav

# Play LTC directly through speakers
ltc_gen_tool --fps=25 --duration=10.0 --play

# Realtime clock mode - derive timecode from system clock
ltc_gen_tool --play --realtime-clock --fps=29.97df

# With time offset (9:00 AM = TC 00:00:00:00)
ltc_gen_tool --play --realtime-clock --time-offset=09:00:00 --fps=30ndf
```

### Command-Line Options
- `--timecode=TIME` - Start timecode in HH:MM:SS:FF format (default: 00:00:00:00)
- `--fps=CHOICE` - Frame rate: 23.976, 24, 25, 29.97df, 29.97, 30df, 30ndf (default: 30ndf)
- `--rate=CHOICE` - Sample rate: 44100, 48000, or 96000 (default: 48000)
- `--duration=FLOAT` - Duration in seconds (default: 10.0)
- `--output=FILE` - Output filename (default: ltc_output.raw)
- `--format=CHOICE` - Output format: raw, wav (default: raw)
- `--rise-time=FLOAT` - Rise/fall time in microseconds, 25-250 (default: 40.0)
- `--play` - Play audio through speakers instead of writing to file
- `--device=DEVICE` - Audio device name (default: default)
- `--realtime-clock` - Use CLOCK_REALTIME for timecode (requires --play)
- `--time-offset=TIME` - Time-of-day offset HH:MM:SS mapping to SMPTE 00:00:00:00
- `--quiet` - Suppress generation info output
- `--help` - Show help message
- `--config-load=FILE` - Load configuration from TOML file
- `--config-save=FILE` - Save configuration to TOML file

### Output Formats

**Raw format (default)**:
- 32-bit floating-point samples (little-endian)
- Suitable for import into Audacity: File > Import > Raw Data
- Select encoding: **32-bit float**, Byte order: **Little-endian**, Channels: **1 (Mono)**
- File size = samples × 4 bytes

**WAV format**:
- Standard 16-bit PCM mono audio
- Can be opened directly by most audio software without manual import settings

### Drop Frame Timecodes

Drop frame timecodes use semicolon separator (01:00:00;00) and skip frames 0 and 1 at the start of each minute except 0, 10, 20, 30, 40, 50. Use `--fps=29.97df` or `--fps=30df` for drop frame modes.

### Realtime Clock Mode

When `--realtime-clock` is used with `--play`, the timecode is derived from CLOCK_REALTIME. The servo mechanism compensates for drift between the system clock and audio clock. Use `--time-offset` to specify a time-of-day that maps to SMPTE 00:00:00:00.

## Testing Strategy

1. **Unit Tests**:
   - Timecode validation
   - BCD encoding correctness
   - 80-bit frame structure
   - Biphase encoding patterns
   - Sample count per frame

2. **Integration Tests**:
   - Decode generated LTC with reference decoder
   - Verify timing accuracy
   - Test all sample rates

3. **Performance Tests**:
   - Real-time generation capability
   - Servo response time
   - Memory usage

## References

- SMPTE 12M-1999: Television, Audio and Film — Time and Control Code
- IEC 60461: Time and control code for video tape recorders
- EBU Tech 3097: Specification of the interface for Time and Control Code data with ancillary data packets
