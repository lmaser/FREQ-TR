#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

class FREQTRAudioProcessor : public juce::AudioProcessor
{
public:
	FREQTRAudioProcessor();
	~FREQTRAudioProcessor() override;

	// Parameter IDs
	static constexpr const char* kParamFreq      = "freq";
	static constexpr const char* kParamFreqSync   = "freq_sync";
	static constexpr const char* kParamMod       = "mod";
	static constexpr const char* kParamFeedback  = "feedback";
	static constexpr const char* kParamEngine    = "engine";
	static constexpr const char* kParamStyle     = "style";
	static constexpr const char* kParamShape     = "shape";
	static constexpr const char* kParamPolarity  = "polarity";
	static constexpr const char* kParamMix       = "mix";
	static constexpr const char* kParamInput     = "input";
	static constexpr const char* kParamOutput    = "output";
	static constexpr const char* kParamSync      = "sync";
	static constexpr const char* kParamRetrig    = "retrig";
	static constexpr const char* kParamMidi      = "midi";
	static constexpr const char* kParamAlign     = "align";
	static constexpr const char* kParamPdc       = "pdc";

	static constexpr const char* kParamFilterHpFreq  = "filter_hp_freq";
	static constexpr const char* kParamFilterLpFreq  = "filter_lp_freq";
	static constexpr const char* kParamFilterHpSlope = "filter_hp_slope";
	static constexpr const char* kParamFilterLpSlope = "filter_lp_slope";
	static constexpr const char* kParamFilterHpOn    = "filter_hp_on";
	static constexpr const char* kParamFilterLpOn    = "filter_lp_on";

	// Tilt
	static constexpr const char* kParamTilt          = "tilt";

	// Chaos
	static constexpr const char* kParamChaos         = "chaos";
	static constexpr const char* kParamChaosD        = "chaos_d";
	static constexpr const char* kParamChaosAmt      = "chaos_amt";
	static constexpr const char* kParamChaosSpd      = "chaos_spd";
	static constexpr const char* kParamChaosAmtFilter = "chaos_amt_filter";
	static constexpr const char* kParamChaosSpdFilter = "chaos_spd_filter";

	// UI state parameters (hidden from DAW automation)
	static constexpr const char* kParamUiWidth   = "ui_width";
	static constexpr const char* kParamUiHeight  = "ui_height";
	static constexpr const char* kParamUiPalette = "ui_palette";
	static constexpr const char* kParamUiCrt     = "ui_fx_tail";
	static constexpr const char* kParamUiColor0  = "ui_color0";
	static constexpr const char* kParamUiColor1  = "ui_color1";

	// Parameter ranges and defaults
	static constexpr float kFreqMin     =  0.0f;
	static constexpr float kFreqMax     =  5000.0f;
	static constexpr float kFreqDefault =  0.0f;

	static constexpr int kFreqSyncMin     = 0;
	static constexpr int kFreqSyncMax     = 29;       // 30 divisions (0-29)
	static constexpr int kFreqSyncDefault = 10;        // = "1/8" (index 10)

	static constexpr float kModMin     = 0.0f;
	static constexpr float kModMax     = 1.0f;
	static constexpr float kModDefault = 0.5f;

	static constexpr float kFeedbackMin     = 0.0f;
	static constexpr float kFeedbackMax     = 1.0f;
	static constexpr float kFeedbackDefault = 0.0f;

	static constexpr float kEngineMin     = 0.0f;
	static constexpr float kEngineMax     = 1.0f;
	static constexpr float kEngineDefault = 0.0f;   // 0 = AM, 1 = Freq Shift

	static constexpr int kStyleMin     = 0;
	static constexpr int kStyleMax     = 3;         // 0 = MONO, 1 = STEREO, 2 = WIDE, 3 = DUAL
	static constexpr float kStyleDefault = 1.0f;

	static constexpr float kShapeMin     = 0.0f;
	static constexpr float kShapeMax     = 1.0f;
	static constexpr float kShapeDefault = 0.0f;    // 0=Sine, 0.33=Tri, 0.66=Square, 1=Saw

	static constexpr float kPolarityMin     = -1.0f;
	static constexpr float kPolarityMax     =  1.0f;
	static constexpr float kPolarityDefault =  1.0f;

	static constexpr float kMixMin     = 0.0f;
	static constexpr float kMixMax     = 1.0f;
	static constexpr float kMixDefault = 1.0f;

	static constexpr float kInputMin     = -100.0f;
	static constexpr float kInputMax     = 0.0f;
	static constexpr float kInputDefault = 0.0f;

	static constexpr float kOutputMin     = -100.0f;
	static constexpr float kOutputMax     = 24.0f;
	static constexpr float kOutputDefault = 0.0f;

	static constexpr float kFilterFreqMin     = 20.0f;
	static constexpr float kFilterFreqMax     = 20000.0f;
	static constexpr float kFilterHpFreqDefault = 250.0f;
	static constexpr float kFilterLpFreqDefault = 2000.0f;
	static constexpr int   kFilterSlopeMin     = 0;       // 6 dB/oct
	static constexpr int   kFilterSlopeMax     = 2;       // 24 dB/oct
	static constexpr int   kFilterSlopeDefault = 1;       // 12 dB/oct

	// Tilt range
	static constexpr float kTiltMin     = -6.0f;
	static constexpr float kTiltMax     =  6.0f;
	static constexpr float kTiltDefault =  0.0f;

	// Chaos ranges
	static constexpr float kChaosAmtMin     = 0.0f;
	static constexpr float kChaosAmtMax     = 100.0f;
	static constexpr float kChaosAmtDefault = 50.0f;
	static constexpr float kChaosSpdMin     = 0.01f;
	static constexpr float kChaosSpdMax     = 100.0f;
	static constexpr float kChaosSpdDefault = 5.0f;

	static juce::String getMidiNoteName (int midiNote);
	juce::String getCurrentFreqDisplay() const;

	static juce::StringArray getFreqSyncChoices();
	static juce::String getFreqSyncName (int index);
	float tempoSyncToHz (int syncIndex, double bpm) const;

	void prepareToPlay (double sampleRate, int samplesPerBlock) override;
	void releaseResources() override;

#if ! JucePlugin_PreferredChannelConfigurations
	bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

	void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

	juce::AudioProcessorEditor* createEditor() override;
	bool hasEditor() const override;

	const juce::String getName() const override;

	bool acceptsMidi() const override;
	bool producesMidi() const override;
	bool isMidiEffect() const override;
	double getTailLengthSeconds() const override;

	int getNumPrograms() override;
	int getCurrentProgram() override;
	void setCurrentProgram (int index) override;
	const juce::String getProgramName (int index) override;
	void changeProgramName (int index, const juce::String& newName) override;

	void getStateInformation (juce::MemoryBlock& destData) override;
	void setStateInformation (const void* data, int sizeInBytes) override;
	void getCurrentProgramStateInformation (juce::MemoryBlock& destData) override;
	void setCurrentProgramStateInformation (const void* data, int sizeInBytes) override;

	// UI state management
	void setUiEditorSize (int width, int height);
	int getUiEditorWidth() const noexcept;
	int getUiEditorHeight() const noexcept;

	void setUiUseCustomPalette (bool shouldUseCustomPalette);
	bool getUiUseCustomPalette() const noexcept;

	void setUiCrtEnabled (bool enabled);
	bool getUiCrtEnabled() const noexcept;

	void setMidiChannel (int channel);
	int getMidiChannel() const noexcept;

	void setUiIoExpanded (bool expanded);
	bool getUiIoExpanded() const noexcept;

	void setUiCustomPaletteColour (int index, juce::Colour colour);
	juce::Colour getUiCustomPaletteColour (int index) const noexcept;

	juce::AudioProcessorValueTreeState apvts;
	static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

	struct BiquadState  { float s1 = 0.0f, s2 = 0.0f; };
	struct BiquadCoeffs { float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };

	// ── Wet filter (HP + LP) ──
	struct WetFilterBiquadCoeffs { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };
	struct WetFilterBiquadState  { float z1 = 0.0f, z2 = 0.0f; };

private:
	struct UiStateKeys
	{
		static constexpr const char* editorWidth = "uiEditorWidth";
		static constexpr const char* editorHeight = "uiEditorHeight";
		static constexpr const char* useCustomPalette = "uiUseCustomPalette";
		static constexpr const char* crtEnabled = "uiFxTailEnabled";
		static constexpr const char* midiPort = "midiPort";
		static constexpr const char* ioExpanded = "uiIoExpanded";
		static constexpr std::array<const char*, 2> customPalette {
			"uiCustomPalette0", "uiCustomPalette1"
		};
	};

	double currentSampleRate = 44100.0;

	// ── Hilbert transform (FIR, linear phase) ──
	// 90° FIR + matched delay for real (0°) path.
	// At 0 Hz shift the output equals the delayed input exactly.
	static constexpr int kHilbertOrder = 128;          // FIR taps (even, power of 2)
	static constexpr int kHilbertDelay = kHilbertOrder / 2;  // group delay in samples
	std::vector<float> hilbertBufL, hilbertBufR;       // circular FIR input buffers
	int hilbertPos = 0;                                // write position in circular buffers

	// Folded Hilbert taps: exploit antisymmetry + zero-skip (128 → ~32 MACs)
	struct HilbertTap { int offset; float coeff; };
	std::vector<HilbertTap> hilbertFoldedTaps;

	// ── Shape peak-normalization table ──
	static constexpr int kShapeTableSize = 256;
	std::array<float, kShapeTableSize + 1> shapeGainTable {};
	void buildShapeGainTable();

	// ── Oscillator state ──
	double oscPhase = 0.0;         // 0..1 normalised phase
	double oscPhaseR = 0.0;        // R-channel phase (WIDE/DUAL: different rate)

	// ── Sine look-up table (linear interpolation, 4096 entries) ──
	static constexpr int kSineLutSize = 4096;
	float sineLut_[kSineLutSize + 1] {};   // +1 for lerp guard
	void buildSineLut() noexcept;
	inline float fastSin (float phase01) const noexcept
	{
		const float idx = phase01 * (float) kSineLutSize;
		const int i0 = (int) idx & (kSineLutSize - 1);
		const float frac = idx - std::floor (idx);
		return sineLut_[i0] + frac * (sineLut_[i0 + 1] - sineLut_[i0]);
	}
	inline float fastCos (float phase01) const noexcept
	{
		return fastSin (phase01 + 0.25f);
	}

	float fastMorphedWave (float phase, float shape) const noexcept;

	float smoothedFreq = 0.0f;     // EMA-smoothed frequency target
	float smoothedEngine = 0.0f;   // EMA-smoothed AM↔FreqShift blend
	float smoothedShape = 0.0f;    // EMA-smoothed waveform morph
	float smoothedMix = 1.0f;      // EMA-smoothed dry/wet
	float smoothedInputGain = 1.0f;  // EMA-smoothed input gain (linear)
	float smoothedOutputGain = 1.0f; // EMA-smoothed output gain (linear)

	// ── Feedback state ──
	juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoothed;
	static constexpr double kFeedbackSmoothingSeconds = 0.05;
	float feedbackLastL = 0.0f;
	float feedbackLastR = 0.0f;

	// ── Feedback DC blocker (one-pole HP ~5 Hz) ──
	static constexpr float kFbkDcBlockHz = 5.0f;
	float fbkDcStateInL  = 0.0f;
	float fbkDcStateInR  = 0.0f;
	float fbkDcStateOutL = 0.0f;
	float fbkDcStateOutR = 0.0f;
	float fbkDcCoeff     = 0.999f;

	// ── Feedback LPF (2nd-order Butterworth) ──
	BiquadState  fbkLpStateL, fbkLpStateR;
	BiquadCoeffs fbkLpCoeffs;

	// ── Wet filter state (HP + LP) ──
	struct WetFilterChannelState
	{
		WetFilterBiquadState hp[2];   // up to 2 cascaded sections (24 dB/oct)
		WetFilterBiquadState lp[2];
		void reset() { *this = {}; }
	};
	WetFilterChannelState wetFilterState_[2];       // L, R
	WetFilterBiquadCoeffs hpCoeffs_[2];             // section 0, 1
	WetFilterBiquadCoeffs lpCoeffs_[2];
	float smoothedFilterHpFreq_ = kFilterHpFreqDefault;
	float smoothedFilterLpFreq_ = kFilterLpFreqDefault;
	float lastCalcHpFreq_ = -1.0f;
	float lastCalcLpFreq_ = -1.0f;
	int   lastCalcHpSlope_ = -1;
	int   lastCalcLpSlope_ = -1;
	static constexpr int kFilterCoeffUpdateInterval = 32;
	int   filterCoeffCountdown_ = 0;

	void updateFilterCoeffs (bool forceHp, bool forceLp);

	// ── Retrig (sync phase anchor) ──
	double syncRetrigPhase = 0.0;  // phase derived from PPQ
	bool useSyncRetrigPhase = false;

	// ── MIDI note tracking ──
	std::atomic<float> currentMidiFrequency { 0.0f };
	std::atomic<int>   lastMidiNote { -1 };
	std::atomic<int>   lastMidiVelocity { 127 };
	std::atomic<int>   midiChannel { 0 };

	// ── Cached parameter pointers ──
	std::atomic<float>* freqParam    = nullptr;
	std::atomic<float>* modParam     = nullptr;
	std::atomic<float>* feedbackParam = nullptr;
	std::atomic<float>* engineParam  = nullptr;
	std::atomic<float>* styleParam   = nullptr;
	std::atomic<float>* shapeParam   = nullptr;
	std::atomic<float>* polarityParam = nullptr;
	std::atomic<float>* mixParam     = nullptr;
	std::atomic<float>* inputParam   = nullptr;
	std::atomic<float>* outputParam  = nullptr;
	std::atomic<float>* syncParam    = nullptr;
	std::atomic<float>* retrigParam  = nullptr;
	std::atomic<float>* midiParam    = nullptr;
	std::atomic<float>* alignParam   = nullptr;
	std::atomic<float>* pdcParam     = nullptr;
	std::atomic<float>* filterHpFreqParam  = nullptr;
	std::atomic<float>* filterLpFreqParam  = nullptr;
	std::atomic<float>* filterHpSlopeParam = nullptr;
	std::atomic<float>* filterLpSlopeParam = nullptr;
	std::atomic<float>* filterHpOnParam    = nullptr;
	std::atomic<float>* filterLpOnParam    = nullptr;

	std::atomic<float>* tiltParam          = nullptr;
	std::atomic<float>* chaosParam         = nullptr;
	std::atomic<float>* chaosDelayParam    = nullptr;
	std::atomic<float>* chaosAmtParam      = nullptr;
	std::atomic<float>* chaosSpdParam      = nullptr;
	std::atomic<float>* chaosAmtFilterParam = nullptr;
	std::atomic<float>* chaosSpdFilterParam = nullptr;

	std::atomic<float>* uiWidthParam   = nullptr;
	std::atomic<float>* uiHeightParam  = nullptr;
	std::atomic<float>* uiPaletteParam = nullptr;
	std::atomic<float>* uiCrtParam     = nullptr;
	std::array<std::atomic<float>*, 2> uiColorParams { nullptr, nullptr };

	// UI state atomics
	std::atomic<int> uiEditorWidth { 360 };
	std::atomic<int> uiEditorHeight { 480 };
	std::atomic<int> uiUseCustomPalette { 0 };
	std::atomic<int> uiCrtEnabled { 0 };
	std::array<std::atomic<juce::uint32>, 2> uiCustomPalette {
		std::atomic<juce::uint32> { juce::Colours::white.getARGB() },
		std::atomic<juce::uint32> { juce::Colours::black.getARGB() }
	};

	// ── Tilt EQ state (1st-order shelving, 1 kHz pivot) ──
	float tiltDb_ = 0.0f;
	float smoothedTiltDb_ = 0.0f;
	float tiltB0_ = 1.0f, tiltB1_ = 0.0f, tiltA1_ = 0.0f;
	float tiltStateL_ = 0.0f, tiltStateR_ = 0.0f;
	static constexpr int kTiltCoeffUpdateInterval = 32;
	int tiltCoeffCountdown_ = 0;

	void computeTiltCoeffs (float tiltDb, float fc, float fs) noexcept
	{
		if (std::abs (tiltDb) < 0.01f)
		{
			tiltB0_ = 1.0f; tiltB1_ = 0.0f; tiltA1_ = 0.0f;
			return;
		}
		const float gain = std::exp2 (std::abs (tiltDb) * 0.16609640474f);  // log2(10)/20
		const float K = std::tan (juce::MathConstants<float>::pi * fc / fs);
		if (tiltDb > 0.0f)
		{
			const float norm   = 1.0f / (1.0f + K);
			const float center = 1.0f / std::sqrt (gain);
			tiltB0_ = (gain + K) * norm * center;
			tiltB1_ = (K - gain) * norm * center;
			tiltA1_ = (K - 1.0f) * norm;
		}
		else
		{
			const float norm   = 1.0f / (gain + K);
			const float center = std::sqrt (gain);
			tiltB0_ = (1.0f + K) * norm * center;
			tiltB1_ = (K - 1.0f) * norm * center;
			tiltA1_ = (K - gain) * norm;
		}
	}

	// ── Chaos state ──
	bool  chaosFilterEnabled_ = false;
	bool  chaosDelayEnabled_  = false;

	// CHS D parameters
	float chaosAmtD_                    = 0.0f;
	float chaosShPeriodD_               = 8820.0f;
	float smoothedChaosShPeriodD_       = 8820.0f;
	float chaosDelayMaxSamples_         = 0.0f;
	float smoothedChaosDelayMaxSamples_ = 0.0f;
	float chaosGainMaxDb_               = 0.0f;
	float smoothedChaosGainMaxDb_       = 0.0f;

	// CHS D S&H: delay
	float chaosDPhase_       = 0.0f;
	float chaosDTarget_      = 0.0f;
	float chaosDSmoothed_    = 0.0f;
	float chaosDSmoothCoeff_ = 0.999f;
	juce::Random chaosDRng_;

	// CHS D S&H: gain (decorrelated)
	float chaosGPhase_       = 0.0f;
	float chaosGTarget_      = 0.0f;
	float chaosGSmoothed_    = 0.0f;
	float chaosGSmoothCoeff_ = 0.999f;
	juce::Random chaosGRng_;

	// CHS F parameters
	float chaosAmtF_                  = 0.0f;
	float chaosShPeriodF_             = 8820.0f;
	float smoothedChaosShPeriodF_     = 8820.0f;
	float chaosFilterMaxOct_          = 0.0f;
	float smoothedChaosFilterMaxOct_  = 0.0f;

	// CHS F S&H: filter
	float chaosFPhase_       = 0.0f;
	float chaosFTarget_      = 0.0f;
	float chaosFSmoothed_    = 0.0f;
	float chaosFSmoothCoeff_ = 0.999f;
	juce::Random chaosFRng_;

	// Chaos per-sample param smoothing
	float chaosParamSmoothCoeff_ = 0.999f;

	// Chaos micro-delay buffer
	static constexpr int kChaosDelayBufLen = 1024;
	float chaosDelayBuf_[2][kChaosDelayBufLen] = {};
	int   chaosDelayWritePos_ = 0;

	inline void advanceChaosD() noexcept
	{
		smoothedChaosDelayMaxSamples_ += (chaosDelayMaxSamples_ - smoothedChaosDelayMaxSamples_) * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosGainMaxDb_       += (chaosGainMaxDb_       - smoothedChaosGainMaxDb_)       * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosShPeriodD_       += (chaosShPeriodD_       - smoothedChaosShPeriodD_)       * (1.0f - chaosParamSmoothCoeff_);

		chaosDPhase_ += 1.0f;
		if (chaosDPhase_ >= smoothedChaosShPeriodD_)
		{
			chaosDPhase_ -= smoothedChaosShPeriodD_;
			chaosDTarget_ = chaosDRng_.nextFloat() * 2.0f - 1.0f;
		}
		chaosDSmoothed_ = chaosDSmoothCoeff_ * chaosDSmoothed_
		                + (1.0f - chaosDSmoothCoeff_) * chaosDTarget_;

		chaosGPhase_ += 1.0f;
		if (chaosGPhase_ >= smoothedChaosShPeriodD_)
		{
			chaosGPhase_ -= smoothedChaosShPeriodD_;
			chaosGTarget_ = chaosGRng_.nextFloat() * 2.0f - 1.0f;
		}
		chaosGSmoothed_ = chaosGSmoothCoeff_ * chaosGSmoothed_
		                + (1.0f - chaosGSmoothCoeff_) * chaosGTarget_;
	}

	inline void advanceChaosF() noexcept
	{
		smoothedChaosFilterMaxOct_  += (chaosFilterMaxOct_  - smoothedChaosFilterMaxOct_)  * (1.0f - chaosParamSmoothCoeff_);
		smoothedChaosShPeriodF_     += (chaosShPeriodF_     - smoothedChaosShPeriodF_)     * (1.0f - chaosParamSmoothCoeff_);

		chaosFPhase_ += 1.0f;
		if (chaosFPhase_ >= smoothedChaosShPeriodF_)
		{
			chaosFPhase_ -= smoothedChaosShPeriodF_;
			chaosFTarget_ = chaosFRng_.nextFloat() * 2.0f - 1.0f;
		}
		chaosFSmoothed_ = chaosFSmoothCoeff_ * chaosFSmoothed_
		                + (1.0f - chaosFSmoothCoeff_) * chaosFTarget_;
	}

	inline void applyChaosDelay (float& wetL, float& wetR) noexcept
	{
		const int wp = chaosDelayWritePos_;
		chaosDelayBuf_[0][wp] = wetL;
		chaosDelayBuf_[1][wp] = wetR;

		const float centerDelay = smoothedChaosDelayMaxSamples_;
		const float delaySamp   = juce::jlimit (0.0f, (float)(kChaosDelayBufLen - 2),
		                                        centerDelay + chaosDSmoothed_ * smoothedChaosDelayMaxSamples_);

		const float readPos = (float) wp - delaySamp;
		const int iPos = (int) std::floor (readPos);
		const float frac = readPos - (float) iPos;
		const int mask = kChaosDelayBufLen - 1;

		for (int ch = 0; ch < 2; ++ch)
		{
			const float p0 = chaosDelayBuf_[ch][(iPos - 1) & mask];
			const float p1 = chaosDelayBuf_[ch][(iPos    ) & mask];
			const float p2 = chaosDelayBuf_[ch][(iPos + 1) & mask];
			const float p3 = chaosDelayBuf_[ch][(iPos + 2) & mask];
			const float c0 = p1;
			const float c1 = p2 - (1.0f / 3.0f) * p0 - 0.5f * p1 - (1.0f / 6.0f) * p3;
			const float c2 = 0.5f * (p0 + p2) - p1;
			const float c3 = (1.0f / 6.0f) * (p3 - p0) + 0.5f * (p1 - p2);
			float& wet = (ch == 0) ? wetL : wetR;
			wet = ((c3 * frac + c2) * frac + c1) * frac + c0;
		}

		chaosDelayWritePos_ = (wp + 1) & mask;
	}

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FREQTRAudioProcessor)
};
