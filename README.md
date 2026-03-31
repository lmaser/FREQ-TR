# FREQ-TR v1.4

<br/><br/>

<img width="451" height="676" alt="image" src="https://github.com/user-attachments/assets/2bb952c0-e9c0-47a5-9aaa-b4dc6ee09589" />

<br/><br/>

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
- **Collapsible INPUT/OUTPUT/MIX section**: Click the toggle bar (triangle) at the top of the slider area to swap between main parameters and the INPUT, OUTPUT, MIX controls. The toggle bar stays fixed in place; only the arrow direction changes. State persists across sessions and preset changes.
- **Filter bar**: Visible in the INPUT/OUTPUT/MIX section. Click to open the HP/LP filter configuration prompt with frequency, slope, and enable/disable controls for each filter.
- **Gear icon** (top-right): Opens the info popup with version, credits, and a link to Graphics settings.
- **Graphics popup**: Toggle CRT post-processing effect and switch between default/custom colour palettes.
- **Resize**: Drag the bottom-right corner. Size persists across sessions.

The value column to the right of each slider shows the current state in context:
- FREQ shows hertz (or MIDI note name when active, or sync division name).
- MOD shows the frequency multiplier.
- FEEDBACK shows percentage.
- ENGINE shows AM/FREQ SHIFT blend percentage.
- STYLE shows MONO/STEREO/WIDE/DUAL.
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

### HP/LP FILTER

High-pass and low-pass filters applied to the wet signal, accessible via the filter bar in the IO section.

- **HP FREQ (20–20 000 Hz)**: High-pass cutoff frequency.
- **LP FREQ (20–20 000 Hz)**: Low-pass cutoff frequency.
- **HP SLOPE (6 dB / 12 dB / 24 dB)**: High-pass filter slope.
- **LP SLOPE (6 dB / 12 dB / 24 dB)**: Low-pass filter slope.
- **HP / LP toggles**: Enable or disable each filter independently. Click the HP/LP label or its checkbox to toggle.

Slope modes:
- **6 dB/oct**: Single-pole filter.
- **12 dB/oct**: Second-order Butterworth.
- **24 dB/oct**: Two cascaded second-order Butterworth stages.

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

### FEEDBACK (0–100%)

Feeds the wet output back into the Hilbert transform input. The feedback amount follows a smoothstep curve (3x²−2x³) for natural-feeling control response.

- **Freq Shift mode**: Creates cascading barberpole effects — each feedback iteration shifts the signal further, producing stacked inharmonic partials.
- **AM mode**: Produces increasingly metallic and inharmonic textures as the ring-modulated signal is re-modulated on each pass.

The Hilbert FIR's 64-sample group delay (~1.5 ms at 44.1 kHz) acts as the natural feedback delay, keeping the loop tight and responsive.

The feedback path includes a one-pole DC blocker at ~5 Hz and a safety limiter at ±4.0. The effective maximum feedback is capped at 97% internally to compensate for the Hilbert FIR's passband ripple, ensuring stable sustain without divergence.

### COMB (5–750 Hz)

Resonant frequency of the feedback delay line. Controls the delay length in the feedback path, producing comb-filter resonances at the set frequency and its harmonics.

Only active when FEEDBACK > 0. At the default value (5 Hz) the delay is long enough to be inaudible as a pitch — raising COMB shortens the delay and tunes the resonance upward.

The slider uses a logarithmic scale for fine control at low frequencies. The display shows the current frequency in Hz.

### ENGINE (0–100%)

Blend between amplitude modulation and frequency shifting.  
- **0% (AM)**: Ring/amplitude modulation. The oscillator multiplies the input directly.
- **100% (Freq Shift)**: Single-sideband frequency shifting via FIR Hilbert transform.
- **In between**: Crossfade between both processes.

### STYLE

Routing topology:
- **MONO**: Single processing path, summed to both channels.
- **STEREO**: Independent left/right processing with shared oscillator.
- **WIDE**: Opposite sidebands per channel (L = upper sideband, R = lower sideband) with cross-feedback. Acts as a dimension expander: the shift affects mainly the side signal (L−R) while leaving the mid (L+R) relatively intact. In AM mode, the right channel uses an inverted carrier for the same mid/side effect.
- **DUAL**: R channel shifts at half (×0.5) the L shift frequency, with independent per-channel feedback. Two separate frequency-shifted textures, one per channel.

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

### TILT (−6 to +6 dB)

Spectral tilt applied to the wet signal. A first-order symmetric shelf filter pivoted at 1 kHz.  
Positive values boost highs and cut lows; negative values cut highs and boost lows.  
Useful for reshaping the tonal balance of the frequency-shifted or AM-processed signal.

### CHAOS

Micro-variation engine that adds organic randomness to the effect. Two independent chaos targets:

- **CHAOS F (Filter)**: Modulates the HP/LP filter cutoff frequencies when filters are enabled. Creates evolving tonal movement.
- **CHAOS D (Frequency)**: Modulates the shift frequency. Produces drifting, detuned textures.

Each chaos target has its own toggle and shares two global controls:

- **AMOUNT (0–100%)**: Modulation depth — how far from the base value the parameter can drift. Default: 50%.
- **SPEED (0.01–100 Hz)**: Sample-and-hold rate — how often a new random target is picked. Default: 5 Hz.

Uses exponential smoothing between random targets for glitch-free transitions.

### LIM THRESHOLD (−36 to 0 dB)

Peak limiter threshold. Sets the ceiling above which the limiter engages.
At 0 dB (default) the limiter acts as a transparent safety net. Lower values compress the signal harder.

### LIM MODE

Limiter insertion point:
- **NONE**: Limiter disabled.
- **WET**: Limiter applied to the wet signal only (after processing, before dry/wet mix).
- **GLOBAL**: Limiter applied to the final output (after output gain and dry/wet mix).

The limiter is a dual-stage transparent peak limiter:
- **Stage 1 (Leveler)**: 2 ms attack, 10 ms release — catches sustained overs.
- **Stage 2 (Brickwall)**: Instant attack, 100 ms release — catches transient peaks.

Stereo-linked gain reduction ensures consistent imaging.

## Technical Details

### DSP Architecture
- **Hilbert Transform**: 128-tap FIR filter with Blackman windowing. Antisymmetric tap folding reduces 128 MACs to ~32 per channel per sample.
- **Matched delay**: Real path delayed by half the FIR order (64 samples) to align with the Hilbert output. The same buffer serves as latency-compensated dry signal for the mix (when ALIGN is ON).
- **Oscillator**: Band-limited morphing waveform (sine → triangle → square → sawtooth) with per-sample phase accumulation.
- **Smoothing**: One-pole EMA (~5 ms) per sample for frequency, engine, shape, and mix parameters.
- **Wet filter**: Biquad HP/LP on the wet signal. Transposed Direct Form II. Coefficients updated once per block (channel 0), shared across channels.

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

## Changelog

### v1.4
- Waveform SHAPE now affects the frequency shifter engine — non-sinusoidal waveforms produce rich harmonic shifts via shaped quadrature oscillators.
- RMS-based waveform normalization eliminates volume jumps when morphing between shapes (sine RMS as reference).
- Added TILT EQ (−6 to +6 dB) — first-order spectral tilt on the wet signal.
- Added CHAOS engine with two independent targets: CHAOS F (filter modulation) and CHAOS D (frequency modulation). Sample-and-hold with exponential smoothing.
- Negative feedback via POLARITY parameter — inverted carrier direction allows downward shifts and alternate modulation character.
- Added safety hard-limiter at +48 dBFS on output, catching NaN/Inf runaways without engaging during normal operation.
- Sine LUT (4096 entries) replaces real-time `std::sin` calls for oscillator and chaos engines.
- Tilt EQ coefficients cached with 32-sample update interval, reducing per-sample `std::exp` overhead.
- Numeric entry popup for percentage sliders: precision standardized to 1 decimal place.
- Ported `drawToggleButton` with automatic text-shrinking from CAB-TR for consistent toggle rendering.
- Fixed checkbox sizing and tick-box rendering to match TR-series style.
- Added COMB parameter (5–750 Hz) — tunes the feedback delay line resonant frequency, producing controllable comb-filter harmonics when FEEDBACK > 0.
- Added dual-stage transparent peak limiter with LIM THRESHOLD (−36 to 0 dB) and LIM MODE (NONE/WET/GLOBAL). Stereo-linked gain reduction with 2 ms/10 ms leveler + instant/100 ms brickwall stages.
