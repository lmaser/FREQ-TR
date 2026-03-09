# FREQ-TR v1.1

FREQ-TR is a frequency shifter and amplitude modulator built for spectral manipulation, inharmonic textures, and tonal effects.  
It combines an FIR Hilbert-transform frequency shifter with a ring/AM modulator, MIDI-controlled pitch, tempo-synced rates, and a minimal CRT-inspired interface.

## Concept

FREQ-TR shifts audio frequencies by a fixed amount in hertz — not a ratio. Unlike pitch shifting, frequency shifting moves every partial by the same offset, breaking harmonic relationships and producing metallic, bell-like, or alien timbres.

The ENGINE control blends between pure amplitude modulation (AM) and full frequency shifting. At 0% (AM), the carrier simply multiplies the input — producing tremolo at low rates and ring modulation at audio rates. At 100% (Freq Shift), the Hilbert transform removes the mirror image, producing a clean single-sideband shift.

POLARITY flips the sign of the shift frequency (allowing downward shifts) and inverts the AM carrier wave, enabling both upper and lower sideband selection.

## Interface

FREQ-TR uses a text-based UI with horizontal bar sliders. All controls are visible at once — no pages, tabs, or hidden menus.

- **Bar sliders**: Click and drag horizontally. Right-click for numeric entry (except STYLE, which is slider-only).
- **Toggle buttons**: SYNC, MIDI, ALIGN, PDC. Click to enable/disable.
- **Collapsible INPUT/OUTPUT/MIX section**: Click the toggle bar (triangle) at the top of the slider area to expand or collapse the INPUT, OUTPUT and MIX controls. The expanded/collapsed state persists across sessions and preset changes.
- **Gear icon** (top-right): Opens the info popup with version, credits, and a link to Graphics settings.
- **Graphics popup**: Toggle CRT post-processing effect and switch between default/custom colour palettes.
- **Resize**: Drag the bottom-right corner. Size persists across sessions.

The value column to the right of each slider shows the current state in context:
- FREQ shows hertz (or MIDI note name when active, or sync division name).
- MOD shows the frequency multiplier.
- ENGINE shows AM/FREQ SHIFT blend percentage.
- STYLE shows MONO/STEREO/WIDE.
- SHAPE shows the oscillator waveform name.
- POLARITY shows the current value (−1 to +1).
- MIX shows percentage.
- INPUT/OUTPUT show dB values.

## Parameters

### INPUT (−100 to 0 dB)

Pre-processing gain. Controls how much signal enters the frequency shifter / AM engine.  
Applied to the wet signal only — the dry signal is unaffected.

### OUTPUT (−100 to +24 dB)

Post-processing gain. Applied to the wet signal only.

### MIX (0–100%)

Dry/wet balance. 0% = fully dry, 100% = fully wet.

### FREQ (0–5,000 Hz)

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
- **WIDE**: Opposite frequency shifts per channel (L shifts up, R shifts down). Creates a wide stereo image through decorrelated sidebands. In AM mode, the right channel uses an inverted carrier.

### SHAPE (0–100%)

Oscillator waveform morphing through four shapes:
- **0%**: Sine
- **33%**: Triangle
- **66%**: Square
- **100%**: Sawtooth

Intermediate positions crossfade between adjacent waveforms. The display shows the blend, e.g. `TRI/SQR 50%`.

### POLARITY (−1 to +1)

Controls the direction of both the frequency shift and the AM carrier.  
- **+1** (default): Upward frequency shift / normal AM polarity.
- **−1**: Downward frequency shift / inverted AM polarity.
- **0**: No shift (mutes the effect).

Applied as a continuous multiplier on the shift frequency.

### MIX (0–100%)

Dry/wet balance. 0% = fully dry, 100% = fully wet.

### SYNC

Locks shift frequency to DAW tempo divisions. Disabled when MIDI is active (MIDI takes priority).

### RETRIG

Phase anchor for sync mode. When enabled (right-click on SYNC), the oscillator phase locks to the DAW transport position, so the modulation waveform restarts at each musical boundary. Hover over SYNC to see the current state (RETRIG: ON/OFF).

### MIDI

Enables MIDI note control of shift frequency. Incoming notes set frequency to `440 × 2^((note − 69) / 12)` Hz.

**Velocity → Glide**: Note velocity controls the portamento speed between pitch changes.
- vel 127 → instant transition.
- vel 1 → full glide (~200 ms).

**MIDI Channel**: Click the channel display to select channel 1–16, or OMNI (all channels).

### ALIGN (default: ON)

Phase alignment between dry and wet signals. When ON, the dry signal is delayed by 64 samples to match the Hilbert FIR group delay, so dry and wet are phase-coherent. When OFF, the dry signal is undelayed, producing comb-filtering at intermediate MIX values (a creative effect used by some frequency shifter plugins).

### PDC (default: ON)

Plugin Delay Compensation. When ON, reports 64 samples of latency to the DAW, allowing automatic delay compensation across tracks. When OFF, no latency is reported (useful if manual compensation is preferred or to reduce DAW latency overhead).

## Technical Details

### DSP Architecture
- **Hilbert Transform**: 128-tap FIR filter with Blackman windowing. Antisymmetric tap folding reduces 128 MACs to ~32 per channel per sample.
- **Matched delay**: Real path delayed by half the FIR order (64 samples) to align with the Hilbert output. The same buffer serves as latency-compensated dry signal for the mix (when ALIGN is ON).
- **Oscillator**: Band-limited morphing waveform (sine → triangle → square → sawtooth) with per-sample phase accumulation.
- **Smoothing**: One-pole EMA (~5 ms) per sample for frequency, engine, shape, and mix parameters.

### MIDI Implementation
- Standard A440 tuning: `frequency = 440 × 2^((note − 69) / 12)`.
- Monophonic last-note priority. Note-off falls back to manual FREQ knob.
- Channel filtering: OMNI (0) or specific channel (1–16).
- Priority: MIDI > SYNC > Manual FREQ.

### State Persistence
- All parameters saved via JUCE AudioProcessorValueTreeState.
- UI state (size, palette, CRT toggle, MIDI channel, IO section expanded/collapsed) persisted in the plugin state.
- Parameter IDs are stable across versions for preset compatibility.

### Performance
- Zero-allocation audio thread. All buffers pre-allocated in `prepareToPlay`.
- Lock-free atomic parameter reads (`std::memory_order_relaxed`).
- FIR convolution uses antisymmetric folding (128 taps → ~32 multiply-accumulate ops per channel).

### Build
- JUCE Framework, C++17, VST3 format.
- Visual Studio 2022 (MSBuild, x64 Release).
- Dependencies: JUCE modules only (no third-party libraries).