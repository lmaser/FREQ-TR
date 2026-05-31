# FREQ-TR v1.4

<br/><br/>

<img width="451" height="676" alt="image" src="https://github.com/user-attachments/assets/2bb952c0-e9c0-47a5-9aaa-b4dc6ee09589" />

<br/><br/>

FREQ-TR is a frequency shifter, ring modulator, and amplitude modulator built for spectral manipulation, inharmonic textures, and creative tonal movement.
It combines an FIR Hilbert-transform frequency shifter with AM and RM engines, MIDI note control, tempo-synced rates, feedback, filtering, tilt, limiter stages, and a compact CRT-inspired interface.

## Concept

FREQ-TR shifts audio by a fixed amount in hertz, not by ratio. Unlike pitch shifting, frequency shifting moves every partial by the same offset, breaking harmonic relationships and producing metallic, bell-like, or alien timbres.

The ENGINE control blends through three explicit points:
- 0% = unipolar AM
- 50% = bipolar ring modulation
- 100% = single-sideband frequency shift via FIR Hilbert transform
- in between = continuous AM -> RM -> frequency-shift blend

POLARITY changes the sign of the shift frequency and the AM carrier direction, so the plugin can move upward, downward, or collapse to zero effect around the center.

## Interface

FREQ-TR uses a text-based UI with horizontal bar sliders. All controls are visible in one main view, with the I/O section foldable from the top bar.

- Bar sliders: drag horizontally, or right-click for numeric entry where available.
- Toggle buttons: `SYNC`, `MIDI`, `ALIGN`, `PDC`, `SIDECHAIN`, `CHAOS F`, `CHAOS D`.
- Collapsible I/O section: click the top triangle bar to switch between the main modulation view and the I/O/filter/routing section.
- Filter bar: opens the HP/LP prompt.
- Gear icon: opens the info popup and graphics settings.
- Resize: bottom-right corner; new instances open at `360 x 752`, editor width persists, and height stays fixed.

The value column reflects the current context:
- `FREQ`: hertz, note name, or sync division
- `MOD`: frequency multiplier
- `COMB`: hertz
- `FBK`: percent
- `JIT`: percent
- `ENGINE`: percent blend
- `WIN`: Hilbert window size for the frequency-shift path
- `STYLE`: MONO / STEREO / WIDE / DUAL
- `HARM`: percent harmonic density
- `POLARITY`: -1 to +1
- `MIX`: percent or SEND dry/wet state
- `INPUT` / `OUTPUT`: dB
- `TILT`: dB
- `PAN`: stereo position
- `LIM`: threshold in dB

## Parameters

### INPUT (-INF to +24 dB)

Pre-effect wet gain. This affects only the wet path, not the dry reference.
The fader floor is -144 dB, displayed as -INF; 0 dB is centered on the control.

### OUTPUT (-INF to +24 dB)

Post-effect wet gain.

### MIX (0-100%)

Dry/wet balance in INSERT mode.

### MIX MODE

- `INSERT`: standard crossfade between dry and wet
- `SEND`: independent dry and wet gain control via the dual mix bar

### DRY / WET (SEND mode)

Independent send-style levels used only when `MIX MODE = SEND`.

### FREQ (0-5000 Hz)

Shift/modulation frequency. At 0 Hz the wet result collapses to the aligned input reference.

When MIDI is active, the effective frequency follows the incoming note. When SYNC is active, the effective frequency follows the selected DAW subdivision.

When `SIDECHAIN` is active, the internal oscillator carrier is replaced by the sidechain input, so `FREQ`, `MOD`, `SYNC`, `MIDI`, and `HARM` are disabled in the UI.

### FREQ SYNC

29 tempo divisions, ordered by real duration and capped at `8/1`, covering fast triplet / straight / dotted divisions through long musical values.

### MOD (x0.25-x4.0)

Frequency multiplier applied to the effective frequency:
- 0% = x0.25
- 50% = x1.0
- 100% = x4.0

### COMB (0-5000 Hz)

Resonant frequency of the feedback delay line. Higher values shorten the feedback delay and raise the comb resonance.

At 0 Hz the comb control is visually at its floor; when feedback is active, the internal feedback delay uses a safe 0.1 Hz effective minimum.

### FBK (-100 to +100%)

Feeds the wet output back into the Hilbert input. This creates barberpole-like stacks in frequency-shift mode and increasingly metallic recirculation in AM mode.

Negative values invert the feedback loop polarity. `POL` still controls the carrier / frequency-shift direction, so `POL -1` and `FBK -100%` are intentionally different controls.

The feedback path includes:
- smoothed control response
- a comb-tuned delay line
- DC blocking
- feedback low-pass conditioning
- safety limiting

### JIT (0-100%)

Internal motion for the frequency shifter and feedback network. It uses the same time-equivalent jitter model as the other TR modulation plugins:
- `FREQ` is modulated from its oscillator period, with an effective Hz floor so low rates still move perceptually
- `COMB` is modulated as feedback delay time
- `FBK` is modulated multiplicatively, without generating feedback from silence

At 0% the jitter path is bypassed. In stereo modes, deterministic lanes keep the movement repeatable while avoiding identical left/right modulation. In SYNC + RETRIG mode, the oscillator integrates the jittered frequency sample-accurately instead of applying a small phase-only offset.

### ENGINE (0-100%)

Blend between AM, ring modulation, and frequency shift:
- 0% = AM
- 50% = RM
- 100% = FREQ SHIFT

### WIN (128 / 256 / 512 / 1024 / 2048)

Selects the FIR Hilbert window used by the frequency-shift side of the engine. It is exposed only for the `RM -> FREQ SHIFT` region because AM and RM do not need Hilbert sideband separation.

Default `WIN` is `512`, with `MAX WIN` defaulting to `512`.

The plugin reports and aligns to the selected maximum Hilbert delay internally, so changing `WIN` does not change the effective host latency. Right-click `PDC` to set `MAX WIN`; `WIN` values above that maximum are processed at the selected cap. Window changes crossfade between the old and new Hilbert paths to avoid discontinuities.

### STYLE

- `MONO`: one processing path, duplicated to both channels
- `STEREO`: stereo input, shared modulation rate
- `WIDE`: opposite sidebands per channel for width enhancement
- `DUAL`: right channel runs at half the left modulation rate

### HARM (0-100%)

Controls harmonic density of the modulator.

- 0% = pure sine modulator
- higher values = progressively denser additive harmonic series
- internal cap = 16 partials total, including the fundamental
- effective count is also limited dynamically by Nyquist

This affects both AM and frequency-shift behavior. At low values the modulation is cleaner and more fundamental-driven; at high values it becomes richer and more spectrally dense.

### SIDECHAIN

Enables sidechain carrier mode. Instead of the internal sine/harmonic oscillator, the plugin uses the optional sidechain audio input as the modulation carrier.

- In AM mode, sidechain drives the unipolar amplitude envelope.
- In RM mode, sidechain is used as the bipolar multiplier.
- In FREQ SHIFT mode, sidechain is converted to a quadrature carrier through the same Hilbert window system, preserving sideband behavior.

If the sidechain bus is not connected or contains no incoming audio, `SIDECHAIN` does not fall back to the internal oscillator; the wet path collapses to the aligned clean reference and feedback is suppressed.

Right-click `SIDECHAIN` to open its carrier prompt:
- `TIME` ranges from `x0.00` to `x1.00`; default is `x0.25`. It controls sidechain entry/exit smoothing, partial carrier gain normalization response, and carrier inertia before AM/RM/frequency-shift processing.
- `TONE` ranges from `250 Hz` to `5000 Hz`; default is `5000 Hz`. It sets the useful upper carrier limit before AM/RM/frequency-shift processing; the internal third-order Butterworth conditioning is already significantly attenuated at the displayed value.

The sidechain carrier path includes DC blocking, Butterworth tone conditioning, TIME-dependent carrier inertia, and partial automatic gain stabilization before Hilbert quadrature. In AM mode, sidechain level controls modulation depth rather than simply reducing the wet output level.

### POLARITY (-1 to +1)

Continuous multiplier applied to shift direction and AM/RM carrier polarity.

- +1 = normal upward / positive behavior
- -1 = inverted / downward behavior
- 0 = no effective shift

In `SIDECHAIN` mode, `POLARITY` also controls the external carrier direction: negative values invert the sidechain quadrature for downward frequency-shift behavior, and the center position collapses cleanly to the aligned dry reference.

### SYNC

Locks the effective frequency to DAW tempo divisions.

### RETRIG

Available from the SYNC prompt. Locks oscillator phase to musical position so synced modulation restarts deterministically against transport.

### MIDI

Enables MIDI note control of the modulation frequency using standard A440 tuning.

Velocity also affects glide speed:
- high velocity = near-instant pitch transition
- low velocity = longer glide

The MIDI prompt exposes both `CHANNEL` and `DELAY`. `DELAY` shifts incoming MIDI control by `0-100 ms` relative to the audio without changing note pitch mapping or glide behaviour.

### ALIGN

Delays the dry reference to match the selected maximum Hilbert transform group delay, so dry and wet remain phase coherent when mixed.

### PDC

Reports plugin latency to the host when enabled.
Right-click `PDC` to set the maximum Hilbert window used for latency/align compensation.

### HP / LP FILTER

Wet-path filters, configurable from the filter prompt:
- HP frequency
- LP frequency
- HP slope: 6 / 12 / 24 dB per octave
- LP slope: 6 / 12 / 24 dB per octave
- independent enable switches

### FILTER POS

Defines whether the wet HP/LP filter block happens before or after the core modulation path.

### TILT (-6 to +6 dB)

Spectral tilt on the wet path, pivoted around 1 kHz.

### PAN

Stereo pan applied after the wet/dry sum routing stage.

### MODE IN

Input routing mode:
- `L+R`
- `MID`
- `SIDE`

### MODE OUT

Wet output routing mode:
- `L+R`
- `MID`
- `SIDE`

### SUM BUS

How the wet path is summed into the output:
- `ST`
- `->M`
- `->S`

### CHAOS

Two optional modulation targets:

- `CHAOS F`: filter cutoff drift
- `CHAOS D`: frequency drift / micro-delay style modulation

Each target uses its own amount and speed values and is smoothed/interpolated for organic motion.

### LIM

Limiter threshold:
- range: -36 to 0 dB

Limiter mode:
- `NONE`
- `WET`
- `GLOBAL`

The limiter is a stereo-linked transparent 2-stage design:
- Stage 1 catches sustained overs
- Stage 2 catches transient peaks

### INV POL / INV STR

Independent post-processing inversion controls for polarity and stereo swap, with wet/global modes.

## Technical Details

### DSP Architecture

- Hilbert transform: selectable 127 / 255 / 511 / 1023 / 2047-tap odd-length FIR with Blackman windowing
- Real path: matched to the selected maximum Hilbert delay for stable PDC/ALIGN behavior
- Oscillator: additive harmonic quadrature oscillator derived from a sine fundamental
- Sidechain carrier: optional external audio carrier with time/tone controls and Hilbert quadrature for frequency-shift operation
- Harmonic cap: 16 partials total, dynamically limited by Nyquist
- Normalization: RMS compensation keeps HARM sweeps reasonably level-stable
- Smoothing: one-pole EMA for the main continuous controls, plus dedicated smoothing where needed
- Feedback: comb-tuned delay line with DC blocking and low-pass conditioning
- Jitter: time-equivalent smooth S&H / flutter / tone modulation applied to oscillator frequency, comb delay, and feedback magnitude
- Safety: final hard safety clip at very high level only

### MIDI

- monophonic last-note priority
- OMNI or channel-specific operation
- `0-100 ms` MIDI delay available in the prompt
- note frequency follows standard `440 * 2^((note - 69) / 12)`
- priority order: MIDI > SYNC > manual FREQ

### State

- Parameters and UI state persist through APVTS
- `HARM` controls the additive harmonic density of the internal modulator

### Build

- JUCE
- C++17
- VST3
- Visual Studio 2022 / x64 Release

## Changelog

### v1.4

- Replaced `SHAPE` with `HARM`
- Harmonic modulator now starts from pure sine and adds density continuously up to 16 total partials
- Added RMS-normalized harmonic oscillator behavior
- Added TILT EQ
- Added CHAOS F / CHAOS D
- Added JIT for deterministic internal movement of frequency, comb, and feedback
- Added AM -> RM -> FREQ SHIFT engine mapping
- Added selectable `WIN` control for frequency-shift Hilbert window length
- Added `MAX WIN` cap from the `PDC` prompt for lower optional Hilbert latency
- Added optional `SIDECHAIN` carrier mode with time/tone prompt
- Added limiter with `WET` / `GLOBAL` modes
- Added COMB parameter for feedback resonance tuning
- Added prompt-based numeric entry refinements and smoothing improvements
