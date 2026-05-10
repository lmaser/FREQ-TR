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
	static constexpr const char* kParamJitter    = "jitter";
	static constexpr const char* kParamComb      = "comb";
	static constexpr const char* kParamEngine    = "engine";
	static constexpr const char* kParamWindow    = "window";
	static constexpr const char* kParamMaxWindow = "max_window";
	static constexpr const char* kParamStyle     = "style";
	static constexpr const char* kParamHarm      = "harm";
	static constexpr const char* kParamPolarity  = "polarity";
	static constexpr const char* kParamMix       = "mix";
	static constexpr const char* kParamInput     = "input";
	static constexpr const char* kParamOutput    = "output";
	static constexpr const char* kParamSync      = "sync";
	static constexpr const char* kParamRetrig    = "retrig";
	static constexpr const char* kParamMidi      = "midi";
	static constexpr const char* kParamAlign     = "align";
	static constexpr const char* kParamPdc       = "pdc";
	static constexpr const char* kParamSidechain = "sidechain";
	static constexpr const char* kParamSidechainTime = "sidechain_time";
	static constexpr const char* kParamSidechainTone = "sidechain_tone";

	static constexpr const char* kParamFilterHpFreq  = "filter_hp_freq";
	static constexpr const char* kParamFilterLpFreq  = "filter_lp_freq";
	static constexpr const char* kParamFilterHpSlope = "filter_hp_slope";
	static constexpr const char* kParamFilterLpSlope = "filter_lp_slope";
	static constexpr const char* kParamFilterHpOn    = "filter_hp_on";
	static constexpr const char* kParamFilterLpOn    = "filter_lp_on";

	// Tilt
	static constexpr const char* kParamTilt          = "tilt";

	// Pan
	static constexpr const char* kParamPan           = "pan";

	// Mode In / Mode Out / Sum Bus
	static constexpr const char* kParamModeIn        = "mode_in";
	static constexpr const char* kParamModeOut       = "mode_out";
	static constexpr const char* kParamSumBus        = "sum_bus";

	// Limiter
	static constexpr const char* kParamLimThreshold  = "lim_threshold";
	static constexpr const char* kParamLimMode       = "lim_mode";

	// Invert Polarity / Invert Stereo
	static constexpr const char* kParamInvPol        = "inv_pol";
	static constexpr const char* kParamInvStr        = "inv_str";

	// Mix Mode / Dry-Wet Levels / Filter Position
	static constexpr const char* kParamMixMode       = "mix_mode";
	static constexpr const char* kParamDryLevel      = "dry_level";
	static constexpr const char* kParamWetLevel      = "wet_level";
	static constexpr const char* kParamFilterPos     = "filter_pos";

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
	static constexpr int kFreqSyncMax     = 28;       // 29 divisions, ordered by real duration and capped at 8/1.
	static constexpr int kFreqSyncDefault = 10;        // = "1/8" (index 10)

	static constexpr float kModMin     = 0.0f;
	static constexpr float kModMax     = 1.0f;
	static constexpr float kModDefault = 0.5f;

	static constexpr float kFeedbackMin     = 0.0f;
	static constexpr float kFeedbackMax     = 1.0f;
	static constexpr float kFeedbackDefault = 0.0f;

	static constexpr float kJitterMin     = 0.0f;
	static constexpr float kJitterMax     = 1.0f;
	static constexpr float kJitterDefault = 0.0f;

	static constexpr float kCombMin     = 0.0f;
	static constexpr float kCombEffectiveMin = 0.1f;
	static constexpr float kCombMax     = 5000.0f;
	static constexpr float kCombDefault = 5.0f;

	static constexpr int kHilbertWindowMin = 128;
	static constexpr int kHilbertWindowMax = 2048;
	static constexpr int kHilbertWindowDefault = 512;
	static constexpr int kHilbertMaxWindowDefault = 2048;
	static constexpr int kNumHilbertWindows = 5;
	static constexpr int kHilbertWindows[kNumHilbertWindows] = { 128, 256, 512, 1024, 2048 };
	static constexpr int kHilbertMaxOrder = kHilbertWindowMax;
	static constexpr int kHilbertMaxFirLength = kHilbertMaxOrder - 1;
	static constexpr int kHilbertMaxDelay = kHilbertMaxFirLength / 2;

	static int getCanonicalHilbertWindow (int windowValue) noexcept
	{
		if (windowValue <= 192)  return 128;
		if (windowValue <= 384)  return 256;
		if (windowValue <= 768)  return 512;
		if (windowValue <= 1536) return 1024;
		return 2048;
	}

	static int getHilbertDelayForWindow (int windowValue) noexcept
	{
		const int canonical = getCanonicalHilbertWindow (windowValue);
		return (canonical - 1) / 2;
	}

	static int getHilbertWindowLane (int windowValue) noexcept
	{
		const int canonical = getCanonicalHilbertWindow (windowValue);
		for (int i = 0; i < kNumHilbertWindows; ++i)
			if (kHilbertWindows[i] == canonical)
				return i;
		return kNumHilbertWindows - 1;
	}

	static constexpr float kEngineMin     = 0.0f;
	static constexpr float kEngineMax     = 1.0f;
	static constexpr float kEngineDefault = 0.0f;   // 0 = AM, 0.5 = Ring Mod, 1 = Freq Shift

	static constexpr int kStyleMin     = 0;
	static constexpr int kStyleMax     = 3;         // 0 = MONO, 1 = STEREO, 2 = WIDE, 3 = DUAL
	static constexpr float kStyleDefault = 1.0f;

	static constexpr float kHarmMin     = 0.0f;
	static constexpr float kHarmMax     = 1.0f;
	static constexpr float kHarmDefault = 0.0f;    // 0 = sine only, 1 = FREAK-like harmonic stack

	static constexpr float kPolarityMin     = -1.0f;
	static constexpr float kPolarityMax     =  1.0f;
	static constexpr float kPolarityDefault =  1.0f;

	static constexpr float kSidechainTimeMin     = 0.0f;
	static constexpr float kSidechainTimeMax     = 1.0f;
	static constexpr float kSidechainTimeDefault = 0.25f;
	static constexpr float kSidechainToneMin     = 250.0f;
	static constexpr float kSidechainToneMax     = 5000.0f;
	static constexpr float kSidechainToneDefault = 5000.0f;

	static constexpr float kMixMin     = 0.0f;
	static constexpr float kMixMax     = 1.0f;
	static constexpr float kMixDefault = 1.0f;

	static constexpr float kGainFloorDb  = -144.0f;
	static constexpr float kGainMaxDb    =   24.0f;
	static constexpr float kGainDefaultDb =   0.0f;
	static constexpr float kGainSkew     = 4.4965561056f; // 0 dB at the fader midpoint

	static constexpr float kInputMin     = kGainFloorDb;
	static constexpr float kInputMax     = kGainMaxDb;
	static constexpr float kInputDefault = kGainDefaultDb;

	static constexpr float kOutputMin     = kGainFloorDb;
	static constexpr float kOutputMax     = kGainMaxDb;
	static constexpr float kOutputDefault = kGainDefaultDb;

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

	// Pan range
	static constexpr float kPanMin     = 0.0f;
	static constexpr float kPanMax     = 1.0f;
	static constexpr float kPanDefault = 0.5f;

	// Chaos ranges
	static constexpr float kChaosAmtMin     = 0.0f;
	static constexpr float kChaosAmtMax     = 100.0f;
	static constexpr float kChaosAmtDefault = 50.0f;
	static constexpr float kChaosSpdMin     = 0.01f;
	static constexpr float kChaosSpdMax     = 100.0f;
	static constexpr float kChaosSpdDefault = 5.0f;

	// Mode In / Mode Out / Sum Bus defaults
	static constexpr int   kModeInOutDefault = 0;
	static constexpr int   kSumBusDefault    = 0;
	static constexpr float kSqrt2Over2       = 0.707106781f;

	// Limiter ranges
	static constexpr float kLimThresholdMin  = -36.0f;
	static constexpr float kLimThresholdMax  =   0.0f;
	static constexpr float kLimThresholdDefault = 0.0f;
	static constexpr int   kLimModeDefault   = 0;

	// Invert Polarity / Invert Stereo defaults
	static constexpr int   kInvPolDefault    = 0;   // 0=NONE  1=WET  2=GLOBAL
	static constexpr int   kInvStrDefault    = 0;   // 0=NONE  1=WET  2=GLOBAL

	// Mix Mode / Dry-Wet / Filter Pos defaults
	static constexpr int   kMixModeDefault   = 0;   // 0=INSERT  1=SEND
	static constexpr float kDryLevelDefault  = 0.0f;
	static constexpr float kWetLevelDefault  = 1.0f;
	static constexpr int   kFilterPosDefault = 0;   // 0=POST  1=PRE

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
	std::vector<float> hilbertBufL, hilbertBufR;       // circular FIR input buffers
	std::vector<float> cleanDelayBufL, cleanDelayBufR; // feedback-free delay for dry ref
	std::vector<float> sidechainHilbertBufL, sidechainHilbertBufR;
	int hilbertPos = 0;                                // write position in circular buffers

	// Folded Hilbert taps: exploit antisymmetry + zero-skip.
	struct HilbertTap { int offset; float coeff; };
	std::array<std::vector<HilbertTap>, kNumHilbertWindows> hilbertFoldedTapsByWindow_;
	int activeHilbertWindow_ = kHilbertWindowDefault;
	int targetHilbertWindow_ = kHilbertWindowDefault;
	int previousHilbertWindow_ = kHilbertWindowDefault;
	int activeMaxHilbertWindow_ = kHilbertMaxWindowDefault;
	int targetMaxHilbertWindow_ = kHilbertMaxWindowDefault;
	int previousMaxHilbertWindow_ = kHilbertMaxWindowDefault;
	int hilbertWindowCrossfadeRemaining_ = 0;
	int hilbertWindowCrossfadeTotal_ = 0;
	float hilbertWetCompBuf_[kNumHilbertWindows][2][kHilbertMaxOrder] = {};

	struct FreqShiftAllpassSection
	{
		float x1 = 0.0f, x2 = 0.0f;
		float y1 = 0.0f, y2 = 0.0f;

		void reset() noexcept
		{
			x1 = x2 = 0.0f;
			y1 = y2 = 0.0f;
		}

		float process (float x, float coeff) noexcept
		{
			const float y = coeff * (x + y2) - x2;
			x2 = x1;
			x1 = x;
			y2 = y1;
			y1 = y;
			return y;
		}
	};

	struct FreqShiftAllpassHilbertState
	{
		std::array<FreqShiftAllpassSection, 4> h1;
		std::array<FreqShiftAllpassSection, 4> h2;
		float h1Delay = 0.0f;

		void reset() noexcept
		{
			for (auto& s : h1) s.reset();
			for (auto& s : h2) s.reset();
			h1Delay = 0.0f;
		}

		std::pair<float, float> process (float x) noexcept
		{
			static constexpr float c1[4] =
			{
				0.6923878f * 0.6923878f,
				0.9360654322959f * 0.9360654322959f,
				0.9882295226860f * 0.9882295226860f,
				0.9987488452737f * 0.9987488452737f
			};
			static constexpr float c2[4] =
			{
				0.4021921162426f * 0.4021921162426f,
				0.8561710882420f * 0.8561710882420f,
				0.9722909545651f * 0.9722909545651f,
				0.9952884791278f * 0.9952884791278f
			};

			float imag = x;
			float real = x;
			for (int i = 0; i < 4; ++i)
			{
				imag = h1[(size_t) i].process (imag, c1[i]);
				real = h2[(size_t) i].process (real, c2[i]);
			}

			const float delayedImag = h1Delay;
			h1Delay = imag;
			return { real, delayedImag };
		}
	};

	FreqShiftAllpassHilbertState freqShiftHilbertIir_[2];

	// ── Harmonics normalization / weighting ──
	static constexpr int kMaxHarmonics = 16;
	static constexpr float kHarmAmountPower = 0.90f;
	static constexpr float kHarmSlopePower = 0.70f;
	static constexpr float kHarmSlopeMin = 2.15f;
	static constexpr float kHarmSlopeMax = 4.60f;
	static constexpr float kHarmGateLastStart = 0.72f;
	static constexpr float kHarmGateWidth = 0.22f;
	static constexpr float kHarmTailRolloffDb = 38.0f;
	static constexpr float kHarmTailRolloffPower = 2.0f;
	static constexpr int kHarmProfileTableSize = 512;
	std::array<std::array<float, kMaxHarmonics>, kHarmProfileTableSize + 1> harmonicProfileTable_ {};
	void buildHarmTables();

	// ── Oscillator state ──
	double oscPhase = 0.0;         // 0..1 normalised phase
	double oscPhaseR = 0.0;        // R-channel phase (WIDE/DUAL: different rate)
	double amRmOscPhase = 0.0;     // AM/RM carrier phase, jitter-smoothed separately
	double amRmOscPhaseR = 0.0;

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

	struct HarmonicOscPair
	{
		float sine = 0.0f;
		float cosine = 0.0f;
	};

	HarmonicOscPair fastHarmonicOscPair (float phase, float harmNorm, float fundamentalHz) const noexcept;

	float smoothedFreq = 0.0f;     // EMA-smoothed frequency target
	float smoothedEngine = 0.0f;   // EMA-smoothed AM->RM->FreqShift blend
	float smoothedHarm = 0.0f;     // EMA-smoothed harmonic density
	float smoothedMix = 1.0f;      // EMA-smoothed dry/wet
	float smoothedDryLevel = kDryLevelDefault;
	float smoothedWetLevel = kWetLevelDefault;
	bool  filterPre_ = false;
	bool  tiltPre_   = false;
	float smoothedInputGain = 1.0f;  // EMA-smoothed input gain (linear)
	float smoothedOutputGain = 1.0f; // EMA-smoothed output gain (linear)
	float smoothedPan = kPanDefault;
	float smoothedLimThreshold = 1.0f;
	float sidechainDcPrevInL_ = 0.0f;
	float sidechainDcPrevInR_ = 0.0f;
	float sidechainDcPrevOutL_ = 0.0f;
	float sidechainDcPrevOutR_ = 0.0f;
	struct SidechainToneFilterState
	{
		float oneX1 = 0.0f;
		float oneY1 = 0.0f;
		float biquadX1 = 0.0f;
		float biquadX2 = 0.0f;
		float biquadY1 = 0.0f;
		float biquadY2 = 0.0f;

		void reset() noexcept
		{
			oneX1 = oneY1 = 0.0f;
			biquadX1 = biquadX2 = 0.0f;
			biquadY1 = biquadY2 = 0.0f;
		}
	};
	SidechainToneFilterState sidechainToneFilterL_;
	SidechainToneFilterState sidechainToneFilterR_;
	float sidechainCarrierSmoothL_ = 0.0f;
	float sidechainCarrierSmoothR_ = 0.0f;
	float sidechainRmsEnv_ = 0.0f;
	float sidechainGateSmoothed_ = 0.0f;
	float sidechainDepthSmoothed_ = 0.0f;

	// ── Feedback state ──
	juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> feedbackSmoothed;
	static constexpr double kFeedbackSmoothingSeconds = 0.05;
	float feedbackLastL = 0.0f;
	float feedbackLastR = 0.0f;

	// ── Feedback delay line (Comb-controlled resonant frequency) ──
	std::vector<float> fbkDelayBufL, fbkDelayBufR;
	int   fbkDelaySize = 1;
	int   fbkDelayWritePos = 0;
	float smoothedComb_ = 5.0f;

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
	WetFilterBiquadCoeffs hpCoeffs_[2];             // section 0, 1 (L / mono)
	WetFilterBiquadCoeffs lpCoeffs_[2];
	WetFilterBiquadCoeffs hpCoeffsR_[2];            // section 0, 1 (R, stereo chaos)
	WetFilterBiquadCoeffs lpCoeffsR_[2];
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
	std::atomic<float>* jitterParam   = nullptr;
	std::atomic<float>* combParam     = nullptr;
	std::atomic<float>* engineParam  = nullptr;
	std::atomic<float>* windowParam  = nullptr;
	std::atomic<float>* maxWindowParam = nullptr;
	std::atomic<float>* styleParam   = nullptr;
	std::atomic<float>* harmParam    = nullptr;
	std::atomic<float>* polarityParam = nullptr;
	std::atomic<float>* mixParam     = nullptr;
	std::atomic<float>* inputParam   = nullptr;
	std::atomic<float>* outputParam  = nullptr;
	std::atomic<float>* syncParam    = nullptr;
	std::atomic<float>* retrigParam  = nullptr;
	std::atomic<float>* midiParam    = nullptr;
	std::atomic<float>* alignParam   = nullptr;
	std::atomic<float>* pdcParam     = nullptr;
	std::atomic<float>* sidechainParam = nullptr;
	std::atomic<float>* sidechainTimeParam = nullptr;
	std::atomic<float>* sidechainToneParam = nullptr;
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

	std::atomic<float>* panParam       = nullptr;
	std::atomic<float>* modeInParam    = nullptr;
	std::atomic<float>* modeOutParam   = nullptr;
	std::atomic<float>* sumBusParam    = nullptr;
	std::atomic<float>* limThresholdParam = nullptr;
	std::atomic<float>* limModeParam     = nullptr;
	std::atomic<float>* invPolParam      = nullptr;
	std::atomic<float>* invStrParam      = nullptr;
	std::atomic<float>* mixModeParam     = nullptr;
	std::atomic<float>* dryLevelParam    = nullptr;
	std::atomic<float>* wetLevelParam    = nullptr;
	std::atomic<float>* filterPosParam   = nullptr;
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
	bool  chaosStereo_        = false;   // true when style >= 1 (per-channel D/G)

	// CHS D parameters
	float chaosAmtD_                    = 0.0f;
	float chaosAmtNormD_                = 0.0f;   // cached amtD * 0.01
	float chaosShPeriodD_               = 8820.0f;
	float smoothedChaosShPeriodD_       = 8820.0f;
	float chaosDelayMaxSamples_         = 0.0f;
	float smoothedChaosDelayMaxSamples_ = 0.0f;
	float chaosGainMaxDb_               = 0.0f;
	float smoothedChaosGainMaxDb_       = 0.0f;
	float chaosDelaySmoothedSamples_[2] = {};
	bool  chaosDelaySmoothReady_[2]     = {};
	float chaosDriveAmtSmoothed_        = 0.0f;
	float chaosDriveSpdSmoothed_        = 0.0f;
	bool  chaosDriveParamSmoothReady_   = false;

	// CHS D smooth S&H + Drift: delay (per-channel for stereo styles)
	float chaosDPrev_[2]         = {};
	float chaosDCurr_[2]         = {};
	float chaosDNext_[2]         = {};
	float chaosDPhase_[2]        = {};
	float chaosDDriftPhase_[2]   = {};
	float chaosDDriftFreqHz_[2]  = {};
	float chaosDOut_[2]          = {};
	juce::Random chaosDRng_[2];

	// CHS D smooth S&H + Drift: gain (per-channel, decorrelated)
	float chaosGPrev_[2]         = {};
	float chaosGCurr_[2]         = {};
	float chaosGNext_[2]         = {};
	float chaosGPhase_[2]        = {};
	float chaosGDriftPhase_[2]   = {};
	float chaosGDriftFreqHz_[2]  = {};
	float chaosGOut_[2]          = {};
	juce::Random chaosGRng_[2];

	// CHS F parameters
	float chaosAmtF_                  = 0.0f;
	float chaosShPeriodF_             = 8820.0f;
	float smoothedChaosShPeriodF_     = 8820.0f;
	float chaosFilterMaxOct_          = 0.0f;
	float smoothedChaosFilterMaxOct_  = 0.0f;
	float chaosFilterAmtSmoothed_     = 0.0f;
	float chaosFilterSpdSmoothed_     = 0.0f;
	bool  chaosFilterParamSmoothReady_ = false;

	// CHS F smooth S&H + Drift: filter (mono S&H + quadrature drift)
	float chaosFPrev_            = 0.0f;
	float chaosFCurr_            = 0.0f;
	float chaosFNext_            = 0.0f;
	float chaosFPhase_           = 0.0f;
	float chaosFDriftPhase_      = 0.0f;   // single phase; R = +90° offset
	float chaosFDriftFreqHz_     = 0.0f;
	float chaosFOut_[2]          = {};     // [0]=L, [1]=R (quadrature when stereo)
	juce::Random chaosFRng_;

	// Chaos per-sample param smoothing
	float chaosParamSmoothCoeff_ = 0.999f;

	// Precomputed sampleRate-dependent smooth coefficients (set in prepareToPlay)
	float cachedFreqEmaCoeff_            = 0.999f;
	float cachedParamEmaCoeff_           = 0.999f;
	float cachedChaosParamSmoothCoeff_   = 0.999f;
	float chaosDelaySmoothStep_          = 0.001f;

	// JIT: internal frequency/feedback/comb instability, independent from CHS D.
	static constexpr double kJitterSmoothingSeconds = 0.0325;
	static constexpr float kJitterEpsilon = 0.000001f;
	static constexpr float kJitterMinDelaySamples = 2.0f;
	static constexpr float kJitterMinDelayMs = 0.05f;
	static constexpr float kJitterShortRefMs = 8.0f;
	static constexpr float kJitterMidRefMs = 500.0f;
	static constexpr float kJitterLongRefMs = 4000.0f;
	static constexpr float kJitterLongnessRefMs = 250.0f;
	static constexpr float kJitterHighStart = 0.55f;
	static constexpr float kJitterHighRange = 0.45f;
	static constexpr float kJitterExtremeStart = 0.82f;
	static constexpr float kJitterExtremeRange = 0.18f;
	static constexpr float kJitterDriftRateMinHz = 0.03f;
	static constexpr float kJitterDriftRateMaxHz = 2.0f;
	static constexpr float kJitterDriftRateBaseHz = 0.08f;
	static constexpr float kJitterDriftRateTopHz = 1.20f;
	static constexpr float kJitterDriftLongnessDamping = 0.65f;
	static constexpr float kJitterDriftShortnessBoost = 0.10f;
	static constexpr float kJitterFlutterRateMinHz = 2.0f;
	static constexpr float kJitterFlutterRateMaxHz = 7000.0f;
	static constexpr float kJitterFlutterRateBaseHz = 4.0f;
	static constexpr float kJitterFlutterRateTopHz = 130.0f;
	static constexpr float kJitterFlutterRefMs = 250.0f;
	static constexpr float kJitterFlutterDelayPower = 0.90f;
	static constexpr float kJitterToneStart = 0.18f;
	static constexpr float kJitterToneRange = 0.82f;
	static constexpr float kJitterToneRateSmoothTauSeconds = 0.006f;
	static constexpr float kJitterToneCeilHz = 12000.0f;
	static constexpr float kJitterToneCeilSampleRateRatio = 0.22f;
	static constexpr float kJitterToneLiftBase = 4.0f;
	static constexpr float kJitterToneLiftAmount = 190.0f;
	static constexpr float kJitterToneLiftHigh = 60.0f;
	static constexpr float kJitterToneLiftExtreme = 90.0f;
	static constexpr float kJitterToneShortnessPower = 0.70f;
	static constexpr float kJitterToneRightHarmonic = 1.618f;
	static constexpr float kJitterToneWeightShortnessPower = 0.55f;
	static constexpr float kJitterToneWeightBase = 0.35f;
	static constexpr float kJitterToneWeightAmount = 0.55f;
	static constexpr float kJitterToneWeightMax = 0.78f;
	static constexpr float kJitterToneFundamentalWeight = 0.72f;
	static constexpr float kJitterToneSecondWeight = 0.20f;
	static constexpr float kJitterToneThirdWeight = 0.08f;
	static constexpr float kJitterToneSecondPhaseL = 0.73f;
	static constexpr float kJitterToneSecondPhaseR = 1.37f;
	static constexpr float kJitterToneThirdPhaseL = 1.91f;
	static constexpr float kJitterToneThirdPhaseR = 2.47f;
	static constexpr float kJitterDriftWeightMin = 0.18f;
	static constexpr float kJitterDriftWeightMax = 0.72f;
	static constexpr float kJitterDriftWeightBase = 0.42f;
	static constexpr float kJitterDriftWeightLongness = 0.30f;
	static constexpr float kJitterDriftWeightShortness = 0.14f;
	static constexpr float kJitterFlutterWeightMin = 0.35f;
	static constexpr float kJitterFlutterWeightMax = 0.95f;
	static constexpr float kJitterFlutterWeightBase = 0.45f;
	static constexpr float kJitterFlutterWeightShortness = 0.38f;
	static constexpr float kJitterFlutterWeightHigh = 0.12f;
	static constexpr float kJitterOutputLimit = 1.25f;
	static constexpr float kJitterDepthRatio = 0.055f;
	static constexpr float kJitterDepthPower = 1.05f;
	static constexpr float kJitterMaxDepthRatio = 0.12f;
	static constexpr float kJitterMinDepthSeconds = 1.0e-7f;
	static constexpr float kJitterMinEngineRateHz = 0.01f;
	static constexpr float kJitterFeedbackDepthBase = 0.010f;
	static constexpr float kJitterFeedbackDepthRange = 0.055f;
	static constexpr float kJitterFeedbackShortBoost = 0.20f;
	static constexpr float kJitterFeedbackDepthScale = 0.60f;
	static constexpr float kJitterFeedbackSlowRateScale = 0.80f;
	static constexpr float kJitterFeedbackFastRateScale = 0.55f;
	static constexpr float kJitterFeedbackSlowWeight = 0.62f;
	static constexpr float kJitterFeedbackFastWeightBase = 0.24f;
	static constexpr float kJitterFeedbackFastShortnessWeight = 0.28f;
	static constexpr float kJitterFeedbackOutputLimit = 1.0f;
	static constexpr float kJitterFrequencyDepthScale = 2.0f;
	static constexpr float kJitterFrequencyFloorHz = 250.0f;
	static constexpr float kJitterFrequencyRateMinDelayMs = 4.0f;
	static constexpr float kJitterFrequencyRateMaxDelayMs = 500.0f;
	static constexpr float kJitterFrequencyRateCompression = 0.80f;
	static constexpr float kAmRmJitterDeviationSmoothTauSeconds = 0.00075f;

	float jitterTargetNorm_              = 0.0f;
	float jitterAmountSmoothed_          = 0.0f;
	float jitterParamSmoothCoeff_        = 0.999f;
	float amRmJitterDeviationCoeff_      = 0.999f;
	float amRmJitterDeviationSmoothed_[2] = {};
	bool  jitterParamSmoothReady_        = false;
	bool  jitterActive_                  = false;
	bool  jitterStereo_                  = false;

	float jitterFreqOut_[2]              = {};
	float jitterFreqDepthOct_[2]         = {};
	float jitterFeedbackOut_             = 0.0f;
	float jitterFeedbackDepth_           = 0.0f;
	float jitterCombOut_                 = 0.0f;
	float jitterCombDepthOct_            = 0.0f;

	struct JitterModulator
	{
		float slowPrev = 0.0f;
		float slowCurr = 0.0f;
		float slowNext = 0.0f;
		float slowPhase = 0.0f;
		float slowDriftPhase = 0.0f;
		float slowDriftFreqHz = 0.0f;
		juce::Random slowRng;

		float fastPrev = 0.0f;
		float fastCurr = 0.0f;
		float fastNext = 0.0f;
		float fastPhase = 0.0f;
		float fastDriftPhase = 0.0f;
		float fastDriftFreqHz = 0.0f;
		juce::Random fastRng;

		float tonePhase = 0.0f;
		float toneRateHz = 0.0f;

		void reset (juce::int64 seed, float initialTonePhase) noexcept
		{
			slowPrev = slowCurr = 0.0f;
			slowPhase = slowDriftPhase = slowDriftFreqHz = 0.0f;
			slowRng.setSeed (seed ^ 0x5a17d15cll);
			slowNext = slowRng.nextFloat() * 2.0f - 1.0f;

			fastPrev = fastCurr = 0.0f;
			fastPhase = fastDriftPhase = fastDriftFreqHz = 0.0f;
			fastRng.setSeed (seed ^ 0x2f05a11ll);
			fastNext = fastRng.nextFloat() * 2.0f - 1.0f;

			tonePhase = initialTonePhase;
			toneRateHz = 0.0f;
		}
	};

	JitterModulator jitterFreqMod_[2];
	JitterModulator jitterFeedbackMod_;
	JitterModulator jitterCombMod_;

	void resetJitterState() noexcept;

	// Chaos micro-delay buffer
	static constexpr int kChaosDelayBufLen = 1024;
	float chaosDelayBuf_[2][kChaosDelayBufLen] = {};
	int   chaosDelayWritePos_ = 0;

	static constexpr float kChaosDriftAmp = 0.3f;
	static constexpr float kTwoPi = 6.283185307f;

	// Generic smooth S&H + Drift chaos engine (per-sample advance)
	inline void advanceChaosEngine (
		float& prev, float& curr, float& next, float& phase,
		float& driftPhase, float& driftFreqHz, float& output,
		juce::Random& rng, float period, float amtNorm, float sr) noexcept
	{
		const float safePeriod = juce::jmax (1.0f, period);
		phase += 1.0f / safePeriod;
		if (phase >= 1.0f)
		{
			phase -= std::floor (phase);
			prev = curr;
			curr = next;
			next = rng.nextFloat() * 2.0f - 1.0f;
			const float driftBase = sr / safePeriod * 0.37f;
			driftFreqHz = driftBase * (0.88f + rng.nextFloat() * 0.24f);
		}
		const float t = juce::jlimit (0.0f, 1.0f, phase);
		const float t2 = t * t;
		const float t3 = t2 * t;
		const float u = t3 * (t * (t * 6.0f - 15.0f) + 10.0f);
		const float shValue = curr + (next - curr) * u;

		driftPhase += driftFreqHz / sr;
		if (driftPhase > 1e6f) driftPhase -= 1e6f;
		const float driftValue = std::sin (driftPhase * kTwoPi) * kChaosDriftAmp;

		const float shWeight = juce::jlimit (0.0f, 1.0f, amtNorm * 1.5f - 0.15f);
		output = driftValue + shValue * shWeight;
	}

	static float smoothStep01 (float x) noexcept
	{
		const float t = juce::jlimit (0.0f, 1.0f, x);
		return t * t * (3.0f - 2.0f * t);
	}

	static float jitterShortness (float delayMs) noexcept
	{
		return juce::jlimit (0.0f, 1.0f,
			std::log2 (kJitterMidRefMs / delayMs) / std::log2 (kJitterMidRefMs / kJitterShortRefMs));
	}

	static float jitterLongness (float delayMs) noexcept
	{
		return juce::jlimit (0.0f, 1.0f,
			std::log2 (delayMs / kJitterLongnessRefMs) / std::log2 (kJitterLongRefMs / kJitterLongnessRefMs));
	}

	struct JitterMetrics
	{
		float amountMapped = 0.0f;
		float delayMs = 1.0f;
		float shortness = 0.0f;
		float longness = 0.0f;
		float driftRateHz = 0.1f;
		float flutterRateHz = 4.0f;
		float toneRateHz = 0.0f;
		float driftWeight = 0.4f;
		float flutterWeight = 0.5f;
		float toneWeight = 0.0f;
		float delayDepthOct = 0.0f;
		float feedbackDepth = 0.0f;
	};

	static float compressJitterFrequencyRateDelayMs (float rawDelayMs) noexcept
	{
		const float limited = juce::jlimit (kJitterFrequencyRateMinDelayMs,
			kJitterFrequencyRateMaxDelayMs, rawDelayMs);
		const float ratio = limited / kJitterFrequencyRateMinDelayMs;
		return kJitterFrequencyRateMinDelayMs * std::pow (ratio, kJitterFrequencyRateCompression);
	}

	inline JitterMetrics makeJitterMetrics (float rateDelaySamples, float depthDelaySamples,
	                                        float amount, float sr, int laneIndex) const noexcept
	{
		JitterMetrics m;
		m.amountMapped = juce::jlimit (0.0f, 1.0f, amount);

		const float safeSr = juce::jmax (1.0f, sr);
		m.delayMs = juce::jmax (kJitterMinDelayMs,
			juce::jmax (kJitterMinDelaySamples, rateDelaySamples) * 1000.0f / safeSr);
		const float rateDelaySeconds = m.delayMs * 0.001f;
		const float depthDelayMs = juce::jmax (kJitterMinDelayMs,
			juce::jmax (kJitterMinDelaySamples, depthDelaySamples) * 1000.0f / safeSr);
		const float depthDelaySeconds = depthDelayMs * 0.001f;
		const float delayHz = 1.0f / rateDelaySeconds;

		m.shortness = jitterShortness (m.delayMs);
		m.longness = jitterLongness (m.delayMs);

		const float high = smoothStep01 ((m.amountMapped - kJitterHighStart) / kJitterHighRange);
		const float extreme = smoothStep01 ((m.amountMapped - kJitterExtremeStart) / kJitterExtremeRange);

		m.driftRateHz = (kJitterDriftRateBaseHz + (kJitterDriftRateTopHz - kJitterDriftRateBaseHz) * m.amountMapped)
		              * (1.0f - kJitterDriftLongnessDamping * m.longness)
		              * (1.0f + kJitterDriftShortnessBoost * m.shortness);
		m.driftRateHz = juce::jlimit (kJitterDriftRateMinHz, kJitterDriftRateMaxHz, m.driftRateHz);

		m.flutterRateHz = (kJitterFlutterRateBaseHz + (kJitterFlutterRateTopHz - kJitterFlutterRateBaseHz) * m.amountMapped)
		                * std::pow (kJitterFlutterRefMs / m.delayMs, kJitterFlutterDelayPower);
		m.flutterRateHz = juce::jlimit (kJitterFlutterRateMinHz, kJitterFlutterRateMaxHz, m.flutterRateHz);

		const float toneAmount = smoothStep01 ((m.amountMapped - kJitterToneStart) / kJitterToneRange);
		const float toneLift = kJitterToneLiftBase + kJitterToneLiftAmount * m.amountMapped
		                      + high * kJitterToneLiftHigh + extreme * kJitterToneLiftExtreme;
		const float toneShort = std::pow (m.shortness, kJitterToneShortnessPower);
		const float harmonic = (laneIndex & 1) == 0 ? 1.0f : kJitterToneRightHarmonic;
		const float toneCeilHz = juce::jmin (kJitterToneCeilHz, safeSr * kJitterToneCeilSampleRateRatio);
		m.toneRateHz = juce::jlimit (0.0f, toneCeilHz, delayHz * toneLift * toneShort * harmonic);

		m.driftWeight = juce::jlimit (kJitterDriftWeightMin, kJitterDriftWeightMax,
			kJitterDriftWeightBase + kJitterDriftWeightLongness * m.longness
			- kJitterDriftWeightShortness * m.shortness);
		m.flutterWeight = juce::jlimit (kJitterFlutterWeightMin, kJitterFlutterWeightMax,
			kJitterFlutterWeightBase + kJitterFlutterWeightShortness * m.shortness
			+ kJitterFlutterWeightHigh * high);
		m.toneWeight = toneAmount * std::pow (m.shortness, kJitterToneWeightShortnessPower)
		             * (kJitterToneWeightBase + kJitterToneWeightAmount * m.amountMapped);
		m.toneWeight = juce::jlimit (0.0f, kJitterToneWeightMax, m.toneWeight);

		const float targetDepthRatio = kJitterDepthRatio * std::pow (m.amountMapped, kJitterDepthPower);
		const float maxDepthSeconds = depthDelaySeconds * kJitterMaxDepthRatio;
		const float depthSeconds = juce::jlimit (kJitterMinDepthSeconds, maxDepthSeconds,
			depthDelaySeconds * targetDepthRatio);
		m.delayDepthOct = std::log2 ((depthDelaySeconds + depthSeconds) / depthDelaySeconds);

		m.feedbackDepth = (kJitterFeedbackDepthBase + kJitterFeedbackDepthRange * m.amountMapped) * m.amountMapped
		                * (1.0f + kJitterFeedbackShortBoost * m.shortness);
		return m;
	}

	inline JitterMetrics makeJitterMetrics (float baseDelaySamples, float amount, float sr, int laneIndex) const noexcept
	{
		return makeJitterMetrics (baseDelaySamples, baseDelaySamples, amount, sr, laneIndex);
	}

	inline float calcJitterFrequencyDelaySamples (float freqHz) const noexcept
	{
		const float sr = juce::jmax (1.0f, (float) currentSampleRate);
		const float hz = juce::jmax (0.01f, std::abs (freqHz));
		return juce::jmax (kJitterMinDelaySamples, sr / hz);
	}

	inline float advanceJitterModulator (JitterModulator& mod, const JitterMetrics& metrics,
	                                     float sr, int laneIndex) noexcept
	{
		float slowOut = 0.0f;
		float fastOut = 0.0f;
		const float slowPeriod = sr / juce::jmax (kJitterMinEngineRateHz, metrics.driftRateHz);
		const float fastPeriod = sr / juce::jmax (kJitterMinEngineRateHz, metrics.flutterRateHz);

		advanceChaosEngine (mod.slowPrev, mod.slowCurr, mod.slowNext,
		                    mod.slowPhase, mod.slowDriftPhase, mod.slowDriftFreqHz,
		                    slowOut, mod.slowRng, slowPeriod, metrics.amountMapped, sr);
		advanceChaosEngine (mod.fastPrev, mod.fastCurr, mod.fastNext,
		                    mod.fastPhase, mod.fastDriftPhase, mod.fastDriftFreqHz,
		                    fastOut, mod.fastRng, fastPeriod, metrics.amountMapped, sr);

		float toneOut = 0.0f;
		if (metrics.toneWeight > kJitterEpsilon && metrics.toneRateHz > 0.0f)
		{
			const float toneRateSmooth = std::exp (-1.0f / (sr * kJitterToneRateSmoothTauSeconds));
			if (mod.toneRateHz <= 0.0f)
				mod.toneRateHz = metrics.toneRateHz;
			else
				mod.toneRateHz = mod.toneRateHz * toneRateSmooth
				               + metrics.toneRateHz * (1.0f - toneRateSmooth);

			mod.tonePhase += mod.toneRateHz / sr;
			mod.tonePhase -= std::floor (mod.tonePhase);

			const bool oddLane = (laneIndex & 1) != 0;
			const float phase = mod.tonePhase * kTwoPi;
			toneOut = std::sin (phase) * kJitterToneFundamentalWeight
			        + std::sin (phase * 2.0f + (oddLane ? kJitterToneSecondPhaseR : kJitterToneSecondPhaseL))
			          * kJitterToneSecondWeight
			        + std::sin (phase * 3.0f + (oddLane ? kJitterToneThirdPhaseR : kJitterToneThirdPhaseL))
			          * kJitterToneThirdWeight;
		}
		else
		{
			mod.toneRateHz = 0.0f;
		}

		const float combined = slowOut * metrics.driftWeight
		                     + fastOut * metrics.flutterWeight
		                     + toneOut * metrics.toneWeight;
		return juce::jlimit (-kJitterOutputLimit, kJitterOutputLimit, combined);
	}

	inline void advanceJitter (float freqDelaySamplesL, float freqDelaySamplesR, float combDelaySamples) noexcept
	{
		const float sr = juce::jmax (1.0f, (float) currentSampleRate);
		const float smoothStep = 1.0f - jitterParamSmoothCoeff_;
		const float target = juce::jlimit (0.0f, 1.0f, jitterTargetNorm_);

		if (! jitterParamSmoothReady_)
		{
			jitterParamSmoothReady_ = true;
		}
		else
		{
			jitterAmountSmoothed_ += (target - jitterAmountSmoothed_) * smoothStep;
		}

		if (target <= 0.000001f && jitterAmountSmoothed_ <= 0.000001f)
		{
			jitterAmountSmoothed_ = 0.0f;
			jitterParamSmoothReady_ = false;
			jitterActive_ = false;
			jitterFreqOut_[0] = jitterFreqOut_[1] = 0.0f;
			jitterFreqDepthOct_[0] = jitterFreqDepthOct_[1] = 0.0f;
			jitterFeedbackOut_ = 0.0f;
			jitterFeedbackDepth_ = 0.0f;
			jitterCombOut_ = 0.0f;
			jitterCombDepthOct_ = 0.0f;
			return;
		}

		const float amt = juce::jlimit (0.0f, 1.0f, jitterAmountSmoothed_);
		const int nCh = jitterStereo_ ? 2 : 1;
		const float freqDelaySamples[2] =
		{
			juce::jmax (kJitterMinDelaySamples, freqDelaySamplesL),
			juce::jmax (kJitterMinDelaySamples, freqDelaySamplesR)
		};
		const float freqRateDelaySamples[2] =
		{
			compressJitterFrequencyRateDelayMs (freqDelaySamples[0] * 1000.0f / sr) * sr * 0.001f,
			compressJitterFrequencyRateDelayMs (freqDelaySamples[1] * 1000.0f / sr) * sr * 0.001f
		};

		for (int ch = 0; ch < nCh; ++ch)
		{
			const JitterMetrics metrics = makeJitterMetrics (freqRateDelaySamples[ch], freqDelaySamples[ch],
			                                                 amt, sr, ch);
			jitterFreqOut_[ch] = advanceJitterModulator (jitterFreqMod_[ch], metrics, sr, ch);
			jitterFreqDepthOct_[ch] = metrics.delayDepthOct * kJitterFrequencyDepthScale;
		}

		if (! jitterStereo_)
		{
			jitterFreqOut_[1] = jitterFreqOut_[0];
			jitterFreqDepthOct_[1] = jitterFreqDepthOct_[0];
		}

		const float safeCombDelay = juce::jmax (kJitterMinDelaySamples, combDelaySamples);
		const JitterMetrics feedbackMetrics = makeJitterMetrics (safeCombDelay, amt, sr, 2);
		float feedbackSlow = 0.0f;
		float feedbackFast = 0.0f;
		advanceChaosEngine (jitterFeedbackMod_.slowPrev, jitterFeedbackMod_.slowCurr, jitterFeedbackMod_.slowNext,
		                    jitterFeedbackMod_.slowPhase, jitterFeedbackMod_.slowDriftPhase,
		                    jitterFeedbackMod_.slowDriftFreqHz, feedbackSlow, jitterFeedbackMod_.slowRng,
		                    sr / juce::jmax (kJitterMinEngineRateHz,
		                                     feedbackMetrics.driftRateHz * kJitterFeedbackSlowRateScale),
		                    feedbackMetrics.amountMapped, sr);
		advanceChaosEngine (jitterFeedbackMod_.fastPrev, jitterFeedbackMod_.fastCurr, jitterFeedbackMod_.fastNext,
		                    jitterFeedbackMod_.fastPhase, jitterFeedbackMod_.fastDriftPhase,
		                    jitterFeedbackMod_.fastDriftFreqHz, feedbackFast, jitterFeedbackMod_.fastRng,
		                    sr / juce::jmax (kJitterMinEngineRateHz,
		                                     feedbackMetrics.flutterRateHz * kJitterFeedbackFastRateScale),
		                    feedbackMetrics.amountMapped, sr);
		jitterFeedbackOut_ = juce::jlimit (-kJitterFeedbackOutputLimit, kJitterFeedbackOutputLimit,
			feedbackSlow * kJitterFeedbackSlowWeight
			+ feedbackFast * (kJitterFeedbackFastWeightBase
			                  + feedbackMetrics.shortness * kJitterFeedbackFastShortnessWeight));
		jitterFeedbackDepth_ = feedbackMetrics.feedbackDepth * kJitterFeedbackDepthScale;

		const JitterMetrics combMetrics = makeJitterMetrics (safeCombDelay, amt, sr, 3);
		jitterCombOut_ = advanceJitterModulator (jitterCombMod_, combMetrics, sr, 3);
		jitterCombDepthOct_ = combMetrics.delayDepthOct;
	}

	inline float getJitteredFrequencyHz (float baseFreq, int channel) const noexcept
	{
		const float amt = juce::jlimit (0.0f, 1.0f, jitterAmountSmoothed_);
		if (! jitterActive_ || amt <= 0.000001f)
			return baseFreq;

		const int lane = juce::jlimit (0, 1, channel);
		const float absBase = std::abs (baseFreq);
		const float referenceHz = std::sqrt (absBase * absBase + kJitterFrequencyFloorHz * kJitterFrequencyFloorHz);
		const float depthHz = referenceHz * (std::exp2 (jitterFreqDepthOct_[lane]) - 1.0f);
		const float sign = (baseFreq < 0.0f) ? -1.0f : 1.0f;
		const float jittered = baseFreq - sign * jitterFreqOut_[lane] * depthHz;

		return juce::jlimit (-kFreqMax * 4.0f, kFreqMax * 4.0f, jittered);
	}

	inline float getAmRmJitteredFrequencyHz (float baseFreq, int channel) const noexcept
	{
		const float amt = juce::jlimit (0.0f, 1.0f, jitterAmountSmoothed_);
		const float absBase = std::abs (baseFreq);
		if (! jitterActive_ || amt <= 0.000001f || absBase <= 0.000001f)
			return baseFreq;

		const int lane = juce::jlimit (0, 1, channel);
		const float depthHz = absBase * (std::exp2 (jitterFreqDepthOct_[lane]) - 1.0f);
		const float sign = (baseFreq < 0.0f) ? -1.0f : 1.0f;
		const float jittered = baseFreq - sign * jitterFreqOut_[lane] * depthHz;

		return juce::jlimit (-kFreqMax * 4.0f, kFreqMax * 4.0f, jittered);
	}

	inline float applyJitterToFeedbackMagnitude (float feedbackMagnitude) const noexcept
	{
		const float amt = juce::jlimit (0.0f, 1.0f, jitterAmountSmoothed_);
		if (! jitterActive_ || amt <= 0.000001f || feedbackMagnitude <= 0.0f)
			return feedbackMagnitude;

		return juce::jlimit (0.0f, 0.99f,
			feedbackMagnitude * (1.0f + jitterFeedbackOut_ * jitterFeedbackDepth_));
	}

	inline float applyJitterToCombTarget (float combSamples) const noexcept
	{
		const float amt = juce::jlimit (0.0f, 1.0f, jitterAmountSmoothed_);
		if (! jitterActive_ || amt <= 0.000001f || combSamples <= 1.0f)
			return combSamples;

		return juce::jlimit (1.0f, (float) juce::jmax (1, fbkDelaySize),
			combSamples * std::exp2 (jitterCombOut_ * jitterCombDepthOct_));
	}

	inline void advanceChaosD() noexcept
	{
		const float sr = (float) currentSampleRate;
		const float smoothStep = 1.0f - chaosParamSmoothCoeff_;
		const float targetAmt = juce::jlimit (kChaosAmtMin, kChaosAmtMax, chaosAmtD_);
		const float targetSpd = juce::jlimit (kChaosSpdMin, kChaosSpdMax, sr / juce::jmax (1.0f, chaosShPeriodD_));

		if (! chaosDriveParamSmoothReady_)
		{
			chaosDriveParamSmoothReady_ = true;
			if (chaosDriveSpdSmoothed_ <= 0.0f)
				chaosDriveSpdSmoothed_ = targetSpd;
		}

		chaosDriveAmtSmoothed_ += (targetAmt - chaosDriveAmtSmoothed_) * smoothStep;
		const float spdLog = std::log (juce::jmax (kChaosSpdMin, chaosDriveSpdSmoothed_));
		const float targetSpdLog = std::log (targetSpd);
		chaosDriveSpdSmoothed_ = std::exp (spdLog + (targetSpdLog - spdLog) * smoothStep);

		chaosAmtNormD_ = chaosDriveAmtSmoothed_ * 0.01f;
		smoothedChaosDelayMaxSamples_ = chaosAmtNormD_ * 0.005f * sr;
		smoothedChaosGainMaxDb_ = chaosAmtNormD_ * 1.0f;
		smoothedChaosShPeriodD_ = sr / juce::jmax (kChaosSpdMin, chaosDriveSpdSmoothed_);

		const float period = smoothedChaosShPeriodD_;
		const int nCh = chaosStereo_ ? 2 : 1;

		for (int c = 0; c < nCh; ++c)
		{
			advanceChaosEngine (chaosDPrev_[c], chaosDCurr_[c], chaosDNext_[c], chaosDPhase_[c],
				chaosDDriftPhase_[c], chaosDDriftFreqHz_[c], chaosDOut_[c],
				chaosDRng_[c], period, chaosAmtNormD_, sr);

			advanceChaosEngine (chaosGPrev_[c], chaosGCurr_[c], chaosGNext_[c], chaosGPhase_[c],
				chaosGDriftPhase_[c], chaosGDriftFreqHz_[c], chaosGOut_[c],
				chaosGRng_[c], period, chaosAmtNormD_, sr);
		}

		// Delay modulation is mono-linked to avoid mono-sum phaser artifacts.
		// Gain modulation may stay stereo for width when the style supports it.
		chaosDOut_[1] = chaosDOut_[0];
		if (! chaosStereo_)
			chaosGOut_[1] = chaosGOut_[0];
	}

	inline void advanceChaosF() noexcept
	{
		const float sr       = (float) currentSampleRate;
		const float smoothStep = 1.0f - chaosParamSmoothCoeff_;
		const float targetAmt = juce::jlimit (kChaosAmtMin, kChaosAmtMax, chaosAmtF_);
		const float targetSpd = juce::jlimit (kChaosSpdMin, kChaosSpdMax, sr / juce::jmax (1.0f, chaosShPeriodF_));

		if (! chaosFilterParamSmoothReady_)
		{
			chaosFilterParamSmoothReady_ = true;
			if (chaosFilterSpdSmoothed_ <= 0.0f)
				chaosFilterSpdSmoothed_ = targetSpd;
		}

		chaosFilterAmtSmoothed_ += (targetAmt - chaosFilterAmtSmoothed_) * smoothStep;
		const float spdLog = std::log (juce::jmax (kChaosSpdMin, chaosFilterSpdSmoothed_));
		const float targetSpdLog = std::log (targetSpd);
		chaosFilterSpdSmoothed_ = std::exp (spdLog + (targetSpdLog - spdLog) * smoothStep);

		const float amtNormF = chaosFilterAmtSmoothed_ * 0.01f;
		smoothedChaosFilterMaxOct_ = amtNormF * 2.0f;
		smoothedChaosShPeriodF_ = sr / juce::jmax (kChaosSpdMin, chaosFilterSpdSmoothed_);
		const float period = smoothedChaosShPeriodF_;

		const float safePeriod = juce::jmax (1.0f, period);
		chaosFPhase_ += 1.0f / safePeriod;
		if (chaosFPhase_ >= 1.0f)
		{
			chaosFPhase_ -= std::floor (chaosFPhase_);
			chaosFPrev_ = chaosFCurr_;
			chaosFCurr_ = chaosFNext_;
			chaosFNext_ = chaosFRng_.nextFloat() * 2.0f - 1.0f;
			const float driftBase = sr / safePeriod * 0.37f;
			chaosFDriftFreqHz_ = driftBase * (0.88f + chaosFRng_.nextFloat() * 0.24f);
		}

		const float t = juce::jlimit (0.0f, 1.0f, chaosFPhase_);
		const float t2 = t * t;
		const float t3 = t2 * t;
		const float u = t3 * (t * (t * 6.0f - 15.0f) + 10.0f);
		const float shValue = chaosFCurr_ + (chaosFNext_ - chaosFCurr_) * u;

		chaosFDriftPhase_ += chaosFDriftFreqHz_ / sr;
		if (chaosFDriftPhase_ > 1e6f) chaosFDriftPhase_ -= 1e6f;
		const float driftL = std::sin (chaosFDriftPhase_ * kTwoPi) * kChaosDriftAmp;

		const float shWeight = juce::jlimit (0.0f, 1.0f, amtNormF * 1.5f - 0.15f);
		chaosFOut_[0] = driftL + shValue * shWeight;

		if (chaosStereo_)
		{
			const float driftR = std::sin (chaosFDriftPhase_ * kTwoPi + kTwoPi * 0.25f) * kChaosDriftAmp;
			chaosFOut_[1] = driftR + shValue * shWeight;
		}
		else
		{
			chaosFOut_[1] = chaosFOut_[0];
		}
	}

	inline void applyChaosDelay (float& wetL, float& wetR) noexcept
	{
		const int wp = chaosDelayWritePos_;
		chaosDelayBuf_[0][wp] = wetL;
		chaosDelayBuf_[1][wp] = wetR;

		const float centerDelay = smoothedChaosDelayMaxSamples_;
		const int mask = kChaosDelayBufLen - 1;

		for (int ch = 0; ch < 2; ++ch)
		{
			const float targetDelaySamp = juce::jlimit (0.0f, (float)(kChaosDelayBufLen - 2),
			                                      centerDelay + chaosDOut_[ch] * smoothedChaosDelayMaxSamples_);
			float& delaySamp = chaosDelaySmoothedSamples_[ch];
			if (! chaosDelaySmoothReady_[ch])
			{
				delaySamp = targetDelaySamp;
				chaosDelaySmoothReady_[ch] = true;
			}
			else
			{
				delaySamp += (targetDelaySamp - delaySamp) * chaosDelaySmoothStep_;
			}

			const float readPos = (float) wp - delaySamp;
			const int iPos = (int) std::floor (readPos);
			const float frac = readPos - (float) iPos;

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

		// Per-channel gain modulation
		for (int ch = 0; ch < 2; ++ch)
		{
			const float gainDb  = chaosGOut_[ch] * smoothedChaosGainMaxDb_;
			const float ex = gainDb * 0.16609640474f;
			const float exln2 = ex * 0.6931472f;
			const float gainLin = 1.0f + exln2 * (1.0f + exln2 * 0.5f);
			float& wet = (ch == 0) ? wetL : wetR;
			wet *= gainLin;
		}
	}

	// ── Dual-stage transparent peak limiter ──
	static constexpr float kLimFloor = 1.0e-12f;
	float limEnv1_[2] = { kLimFloor, kLimFloor };
	float limEnv2_[2] = { kLimFloor, kLimFloor };
	float limAtt1_  = 0.0f;
	float limRel1_  = 0.0f;
	float limRel2_  = 0.0f;

	inline void applyLimiter (float* leftData, float* rightData, int numSamples,
	                         float thresholdGain) noexcept
	{
		for (int i = 0; i < numSamples; ++i)
		{
			const float peakL = std::abs (leftData[i]);
			const float peakR = std::abs (rightData[i]);

			// Stage 1 — leveler (2 ms attack, 10 ms release)
			for (int ch = 0; ch < 2; ++ch)
			{
				const float p = (ch == 0) ? peakL : peakR;
				if (p > limEnv1_[ch])
					limEnv1_[ch] = limAtt1_ * limEnv1_[ch] + (1.0f - limAtt1_) * p;
				else
					limEnv1_[ch] = limRel1_ * limEnv1_[ch] + (1.0f - limRel1_) * p;
				if (limEnv1_[ch] < kLimFloor) limEnv1_[ch] = kLimFloor;
			}

			// Stage 2 — brickwall (instant attack, 100 ms release)
			for (int ch = 0; ch < 2; ++ch)
			{
				const float p = (ch == 0) ? peakL : peakR;
				if (p > limEnv2_[ch])
					limEnv2_[ch] = p;
				else
					limEnv2_[ch] = limRel2_ * limEnv2_[ch] + (1.0f - limRel2_) * p;
				if (limEnv2_[ch] < kLimFloor) limEnv2_[ch] = kLimFloor;
			}

			// Stereo-linked gain reduction
			float gr = 1.0f;
			const float maxEnv1 = juce::jmax (limEnv1_[0], limEnv1_[1]);
			const float maxEnv2 = juce::jmax (limEnv2_[0], limEnv2_[1]);
			if (maxEnv1 > thresholdGain)
				gr = juce::jmin (gr, thresholdGain / maxEnv1);
			if (maxEnv2 > thresholdGain)
				gr = juce::jmin (gr, thresholdGain / maxEnv2);

			leftData[i]  *= gr;
			rightData[i] *= gr;
		}
	}

	inline void applyLimiterSample (float& sampleL, float& sampleR, float thresholdGain) noexcept
	{
		const float peakL = std::abs (sampleL);
		const float peakR = std::abs (sampleR);

		for (int ch = 0; ch < 2; ++ch)
		{
			const float p = (ch == 0) ? peakL : peakR;
			if (p > limEnv1_[ch])
				limEnv1_[ch] = limAtt1_ * limEnv1_[ch] + (1.0f - limAtt1_) * p;
			else
				limEnv1_[ch] = limRel1_ * limEnv1_[ch] + (1.0f - limRel1_) * p;
			if (limEnv1_[ch] < kLimFloor) limEnv1_[ch] = kLimFloor;
		}

		for (int ch = 0; ch < 2; ++ch)
		{
			const float p = (ch == 0) ? peakL : peakR;
			if (p > limEnv2_[ch])
				limEnv2_[ch] = p;
			else
				limEnv2_[ch] = limRel2_ * limEnv2_[ch] + (1.0f - limRel2_) * p;
			if (limEnv2_[ch] < kLimFloor) limEnv2_[ch] = kLimFloor;
		}

		float gr = 1.0f;
		const float maxEnv1 = juce::jmax (limEnv1_[0], limEnv1_[1]);
		const float maxEnv2 = juce::jmax (limEnv2_[0], limEnv2_[1]);
		if (maxEnv1 > thresholdGain)
			gr = juce::jmin (gr, thresholdGain / maxEnv1);
		if (maxEnv2 > thresholdGain)
			gr = juce::jmin (gr, thresholdGain / maxEnv2);

		sampleL *= gr;
		sampleR *= gr;
	}

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FREQTRAudioProcessor)
};
