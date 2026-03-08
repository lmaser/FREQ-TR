# FREQ-TR v1.0c

FREQ-TR is a frequency shifter and amplitude modulator built for spectral manipulation, inharmonic textures, and tonal effects.  
It combines an FIR Hilbert-transform frequency shifter with a ring/AM modulator, MIDI-controlled pitch, tempo-synced rates, and a minimal CRT-inspired interface.

## Concept

FREQ-TR shifts audio frequencies by a fixed amount in hertz — not a ratio. Unlike pitch shifting, frequency shifting moves every partial by the same offset, breaking harmonic relationships and producing metallic, bell-like, or alien timbres.

The ENGINE control blends between pure amplitude modulation (AM) and full frequency shifting. At 0% (AM), the carrier simply multiplies the input — producing tremolo at low rates and ring modulation at audio rates. At 100% (Freq Shift), the Hilbert transform removes the mirror image, producing a clean single-sideband shift.

POLARITY flips the sign of the shift frequency (allowing downward shifts) and inverts the AM carrier wave, enabling both upper and lower sideband selection.

## Interface

FREQ-TR uses a text-based UI with horizontal bar sliders. All controls are visible at once — no pages, tabs, or hidden menus.

- **Bar sliders**: Click and drag horizontally. Right-click for numeric entry (except STYLE, which is slider-only).
- **Toggle buttons**: SYNC, MIDI. Click to enable/disable.
- **Gear icon** (top-right): Opens the info popup with version, credits, and a link to Graphics settings.
- **Graphics popup**: Toggle CRT post-processing effect and switch between default/custom colour palettes.
- **Resize**: Drag the bottom-right corner. Size persists across sessions.

The value column to the right of each slider shows the current state in context:
- FREQ shows hertz (or MIDI note name when active, or sync division name).
- MOD shows the frequency multiplier.
- ENGINE shows AM/FREQ SHIFT blend percentage.
- STYLE shows MONO/STEREO.
- SHAPE shows the oscillator waveform name.
- POLARITY shows the current value (−1 to +1).
- MIX shows percentage.

## Parameters

### FREQ (0–10,000 Hz)

Shift frequency. At 0 Hz the output equals the input. Higher values produce increasingly inharmonic results.  
The slider uses a skewed scale (0.35) so low frequencies have finer resolution.

When MIDI is active, FREQ shows the note name. When the note releases, the frequency glides back to the manual knob value.

### FREQ SYNC (30 divisions)

When SYNC is enabled, FREQ locks to DAW tempo. Provides 30 musical subdivisions:  
1/64 through 8/1, each with triplet, normal, and dotted variants.  
The sync value is converted to hertz via `1000 / durationMs`.

### MOD (×0.25–×4.0)

Frequency multiplier applied to the shift frequency.  
0% = ×0.25 (4× lower frequency), 50% = ×1.0 (no change), 100% = ×4.0 (4× higher frequency).

### ENGINE (0–100%)

Blend between amplitude modulation and frequency shifting.  
- **0% (AM)**: Ring/amplitude modulation. The oscillator multiplies the input directly.
- **100% (Freq Shift)**: Single-sideband frequency shifting via FIR Hilbert transform.
- **In between**: Crossfade between both processes.

### STYLE

Routing topology:
- **MONO**: Single processing path, summed to both channels.
- **STEREO**: Independent left/right processing with shared oscillator.

### SHAPE (0–100%)

Oscillator waveform morphing through four shapes:
- **0%**: Sine
- **33%**: Triangle
- **66%**: Square
- **100%**: Sawtooth

Intermediate positions crossfade between adjacent waveforms.

### POLARITY (−1 to +1)

Controls the direction of both the frequency shift and the AM carrier.  
- **+1** (default): Upward frequency shift / normal AM polarity.
- **−1**: Downward frequency shift / inverted AM polarity.
- **0**: No shift (mutes the effect).

The sign is applied as a simple multiplier: `polaritySign = (polarity >= 0) ? +1 : −1`.

### MIX (0–100%)

Dry/wet balance. 0% = fully dry, 100% = fully wet.

### SYNC

Locks shift frequency to DAW tempo divisions. Disabled when MIDI is active (MIDI takes priority).

### MIDI

Enables MIDI note control of shift frequency. Incoming notes set frequency to `440 × 2^((note − 69) / 12)` Hz.

**Velocity → Glide**: Note velocity controls the portamento speed between pitch changes.
- vel 127 → instant transition.
- vel 1 → full glide (~200 ms).

**MIDI Channel**: Click the channel display to select channel 1–16, or OMNI (all channels).

## Technical Details

### DSP Architecture
- **Hilbert Transform**: 128-tap FIR filter with Blackman windowing. Produces analytic signal (90° phase-shifted quadrature pair).
- **Matched delay**: Real path delayed by half the FIR order (64 samples) to align with the Hilbert output.
- **Dry latency compensation**: Separate delay line for the dry signal to match the wet path's latency.
- **Oscillator**: Band-limited morphing waveform (sine → triangle → square → sawtooth) with per-sample phase accumulation.
- **Smoothing**: One-pole EMA per sample for frequency, gain, and mix parameters.

### MIDI Implementation
- Standard A440 tuning: `frequency = 440 × 2^((note − 69) / 12)`.
- Monophonic last-note priority. Note-off falls back to manual FREQ knob.
- Channel filtering: OMNI (0) or specific channel (1–16).
- Priority: MIDI > SYNC > Manual FREQ.

### State Persistence
- All parameters saved via JUCE AudioProcessorValueTreeState.
- UI state (size, palette, CRT toggle, MIDI channel) persisted in the plugin state.
- Parameter IDs are stable across versions for preset compatibility.

### Performance
- Zero-allocation audio thread. All buffers pre-allocated in `prepareToPlay`.
- Lock-free atomic parameter reads (`std::memory_order_relaxed`).
- FIR convolution runs in the inner sample loop — no FFT partitioning (suitable for the 128-tap length).

### Build
- JUCE Framework, C++17, VST3 format.
- Visual Studio 2022 (MSBuild, x64 Release).
- Dependencies: JUCE modules only (no third-party libraries).