#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
	inline float loadAtomicOrDefault (std::atomic<float>* p, float def) noexcept
	{
		return p != nullptr ? p->load (std::memory_order_relaxed) : def;
	}

	inline int loadIntParamOrDefault (std::atomic<float>* p, int def) noexcept
	{
		return (int) std::lround (loadAtomicOrDefault (p, (float) def));
	}

	inline bool loadBoolParamOrDefault (std::atomic<float>* p, bool def) noexcept
	{
		return loadAtomicOrDefault (p, def ? 1.0f : 0.0f) > 0.5f;
	}

	inline float fastDecibelsToGain (float dB) noexcept
	{
		return (dB <= -100.0f) ? 0.0f : std::exp2 (dB * 0.16609640474f);
	}

	inline float gainFaderDecibelsToGain (float dB) noexcept
	{
		return (dB <= FREQTRAudioProcessor::kGainFloorDb) ? 0.0f : std::exp2 (dB * 0.16609640474f);
	}

	inline juce::NormalisableRange<float> makeGainFaderRange() noexcept
	{
		return juce::NormalisableRange<float> (FREQTRAudioProcessor::kGainFloorDb,
		                                       FREQTRAudioProcessor::kGainMaxDb,
		                                       0.0f,
		                                       FREQTRAudioProcessor::kGainSkew);
	}

	inline juce::NormalisableRange<float> makeCombFrequencyRange() noexcept
	{
		return juce::NormalisableRange<float> (
			FREQTRAudioProcessor::kCombMin,
			FREQTRAudioProcessor::kCombMax,
			[] (float rangeStart, float rangeEnd, float normalised) noexcept
			{
				const float minHz = juce::jmax (0.000001f, rangeStart);
				const float maxHz = juce::jmax (minHz, rangeEnd);
				return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, normalised));
			},
			[] (float rangeStart, float rangeEnd, float value) noexcept
			{
				const float minHz = juce::jmax (0.000001f, rangeStart);
				const float maxHz = juce::jmax (minHz, rangeEnd);
				const float clamped = juce::jlimit (minHz, maxHz, value);
				return std::log (clamped / minHz) / std::log (maxHz / minHz);
			});
	}

	inline void setParameterPlainValue (juce::AudioProcessorValueTreeState& apvts,
										const char* paramId,
										float plainValue)
	{
		if (auto* param = apvts.getParameter (paramId))
		{
			const float norm = param->convertTo0to1 (plainValue);
			param->setValueNotifyingHost (norm);
		}
	}

	// ── Waveform generation (normalised phase 0..1 → output -1..1) ──


	// ── Hilbert FIR coefficient generation ──
	// Odd-length windowed-sinc design: h[n] = 2 / (n*pi) for odd n, 0 for even.
	// The odd length is required so folded taps are truly antisymmetric.
	inline void designHilbertFIR (float* coeffs, int length)
	{
		const int M = length;
		const int half = M / 2;

		for (int i = 0; i < M; ++i)
			coeffs[i] = 0.0f;

		for (int i = 0; i < M; ++i)
		{
			const int n = i - half;
			if (n == 0 || (n % 2) == 0)
			{
				coeffs[i] = 0.0f;
			}
			else
			{
				coeffs[i] = 2.0f / (juce::MathConstants<float>::pi * (float) n);
			}

			// Blackman window
			const float w = 0.42f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (M - 1))
						   + 0.08f * std::cos (2.0f * juce::MathConstants<float>::twoPi * (float) i / (float) (M - 1));
			coeffs[i] *= w;
		}
	}

	// ── Biquad coefficient calculators for wet HP/LP filters ──
	using BQC = FREQTRAudioProcessor::WetFilterBiquadCoeffs;

	inline BQC calcOnePoleLP (float freq, float sr)
	{
		const float w = juce::MathConstants<float>::twoPi * freq / sr;
		const float alpha = w / (1.0f + w);
		return { alpha, 0.0f, 0.0f, -(1.0f - alpha), 0.0f };
	}

	inline BQC calcOnePoleHP (float freq, float sr)
	{
		const float w = juce::MathConstants<float>::twoPi * freq / sr;
		const float a = 1.0f / (1.0f + w);
		return { a, -a, 0.0f, -(1.0f - a), 0.0f };
	}

	inline BQC calcBiquadLP (float freq, float sr, float Q)
	{
		const float w0 = juce::MathConstants<float>::twoPi * freq / sr;
		const float cs = std::cos (w0);
		const float sn = std::sin (w0);
		const float alpha = sn / (2.0f * Q);
		const float a0 = 1.0f + alpha;
		return { ((1.0f - cs) * 0.5f) / a0,
				 (1.0f - cs) / a0,
				 ((1.0f - cs) * 0.5f) / a0,
				 (-2.0f * cs) / a0,
				 (1.0f - alpha) / a0 };
	}

	inline BQC calcBiquadHP (float freq, float sr, float Q)
	{
		const float w0 = juce::MathConstants<float>::twoPi * freq / sr;
		const float cs = std::cos (w0);
		const float sn = std::sin (w0);
		const float alpha = sn / (2.0f * Q);
		const float a0 = 1.0f + alpha;
		return { ((1.0f + cs) * 0.5f) / a0,
				 -(1.0f + cs) / a0,
				 ((1.0f + cs) * 0.5f) / a0,
				 (-2.0f * cs) / a0,
				 (1.0f - alpha) / a0 };
	}

	// 4th-order Butterworth Q values
	constexpr float kBW4_Q1 = 0.54119610f;   // 1 / (2 cos(3π/8))
	constexpr float kBW4_Q2 = 1.30656296f;   // 1 / (2 cos(π/8))

	inline float processWetBiquad (const BQC& c,
								   FREQTRAudioProcessor::WetFilterBiquadState& s,
								   float x) noexcept
	{
		const float y = c.b0 * x + s.z1;
		s.z1 = c.b1 * x - c.a1 * y + s.z2;
		s.z2 = c.b2 * x - c.a2 * y;
		return y;
	}

	constexpr float kGainSmoothCoeff = 0.9955f;
}

//==============================================================================
FREQTRAudioProcessor::FREQTRAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
	: AudioProcessor (BusesProperties()
					 #if ! JucePlugin_IsMidiEffect
					  #if ! JucePlugin_IsSynth
					   .withInput  ("Input", juce::AudioChannelSet::stereo(), true)
					   .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
					  #endif
					   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
					 #endif
					   )
#endif
	, apvts (*this, nullptr, "Parameters", createParameterLayout())
{
	freqParam    = apvts.getRawParameterValue (kParamFreq);
	modParam     = apvts.getRawParameterValue (kParamMod);
	feedbackParam = apvts.getRawParameterValue (kParamFeedback);
	jitterParam   = apvts.getRawParameterValue (kParamJitter);
	combParam     = apvts.getRawParameterValue (kParamComb);
	engineParam  = apvts.getRawParameterValue (kParamEngine);
	windowParam  = apvts.getRawParameterValue (kParamWindow);
	maxWindowParam = apvts.getRawParameterValue (kParamMaxWindow);
	styleParam   = apvts.getRawParameterValue (kParamStyle);
	harmParam    = apvts.getRawParameterValue (kParamHarm);
	polarityParam = apvts.getRawParameterValue (kParamPolarity);
	mixParam     = apvts.getRawParameterValue (kParamMix);
	inputParam   = apvts.getRawParameterValue (kParamInput);
	outputParam  = apvts.getRawParameterValue (kParamOutput);
	syncParam    = apvts.getRawParameterValue (kParamSync);
	retrigParam  = apvts.getRawParameterValue (kParamRetrig);
	midiParam    = apvts.getRawParameterValue (kParamMidi);
	alignParam   = apvts.getRawParameterValue (kParamAlign);
	pdcParam     = apvts.getRawParameterValue (kParamPdc);
	sidechainParam = apvts.getRawParameterValue (kParamSidechain);
	sidechainTimeParam = apvts.getRawParameterValue (kParamSidechainTime);
	sidechainToneParam = apvts.getRawParameterValue (kParamSidechainTone);
	filterHpFreqParam  = apvts.getRawParameterValue (kParamFilterHpFreq);
	filterLpFreqParam  = apvts.getRawParameterValue (kParamFilterLpFreq);
	filterHpSlopeParam = apvts.getRawParameterValue (kParamFilterHpSlope);
	filterLpSlopeParam = apvts.getRawParameterValue (kParamFilterLpSlope);
	filterHpOnParam    = apvts.getRawParameterValue (kParamFilterHpOn);
	filterLpOnParam    = apvts.getRawParameterValue (kParamFilterLpOn);

	tiltParam          = apvts.getRawParameterValue (kParamTilt);
	panParam           = apvts.getRawParameterValue (kParamPan);
	modeInParam        = apvts.getRawParameterValue (kParamModeIn);
	modeOutParam       = apvts.getRawParameterValue (kParamModeOut);
	sumBusParam        = apvts.getRawParameterValue (kParamSumBus);
	limThresholdParam  = apvts.getRawParameterValue (kParamLimThreshold);
	limModeParam       = apvts.getRawParameterValue (kParamLimMode);
	invPolParam         = apvts.getRawParameterValue (kParamInvPol);
	invStrParam         = apvts.getRawParameterValue (kParamInvStr);
	mixModeParam        = apvts.getRawParameterValue (kParamMixMode);
	dryLevelParam       = apvts.getRawParameterValue (kParamDryLevel);
	wetLevelParam       = apvts.getRawParameterValue (kParamWetLevel);
	filterPosParam      = apvts.getRawParameterValue (kParamFilterPos);
	chaosParam         = apvts.getRawParameterValue (kParamChaos);
	chaosDelayParam    = apvts.getRawParameterValue (kParamChaosD);
	chaosAmtParam      = apvts.getRawParameterValue (kParamChaosAmt);
	chaosSpdParam      = apvts.getRawParameterValue (kParamChaosSpd);
	chaosAmtFilterParam = apvts.getRawParameterValue (kParamChaosAmtFilter);
	chaosSpdFilterParam = apvts.getRawParameterValue (kParamChaosSpdFilter);

	uiWidthParam   = apvts.getRawParameterValue (kParamUiWidth);
	uiHeightParam  = apvts.getRawParameterValue (kParamUiHeight);
	uiPaletteParam = apvts.getRawParameterValue (kParamUiPalette);
	uiCrtParam     = apvts.getRawParameterValue (kParamUiCrt);
	uiColorParams[0] = apvts.getRawParameterValue (kParamUiColor0);
	uiColorParams[1] = apvts.getRawParameterValue (kParamUiColor1);

	const int w = loadIntParamOrDefault (uiWidthParam, 360);
	const int h = loadIntParamOrDefault (uiHeightParam, 480);
	uiEditorWidth.store (w, std::memory_order_relaxed);
	uiEditorHeight.store (h, std::memory_order_relaxed);

	buildHarmTables();
	buildSineLut();
}

void FREQTRAudioProcessor::buildSineLut() noexcept
{
	for (int i = 0; i <= kSineLutSize; ++i)
		sineLut_[i] = std::sin ((float) i / (float) kSineLutSize * juce::MathConstants<float>::twoPi);
}

void FREQTRAudioProcessor::buildHarmTables()
{
	for (int i = 0; i <= kHarmProfileTableSize; ++i)
	{
		const float harm = (float) i / (float) kHarmProfileTableSize;
		const float amount = harm <= 0.0f ? 0.0f : std::pow (harm, kHarmAmountPower);
		const float slopeT = harm <= 0.0f ? 0.0f : std::pow (harm, kHarmSlopePower);
		const float slope = kHarmSlopeMax + (kHarmSlopeMin - kHarmSlopeMax) * slopeT;

		harmonicProfileTable_[(size_t) i][0] = 1.0f;
		for (int n = 2; n <= kMaxHarmonics; ++n)
		{
			const float harmonicIndex = (float) (n - 2) / (float) juce::jmax (1, kMaxHarmonics - 2);
			const float gateStart = kHarmGateLastStart * harmonicIndex;
			const float gate = smoothStep01 ((harm - gateStart) / kHarmGateWidth);
			const float tailRolloffDb = kHarmTailRolloffDb * harm * std::pow (harmonicIndex, kHarmTailRolloffPower);
			const float tailRolloff = juce::Decibels::decibelsToGain (-tailRolloffDb);
			harmonicProfileTable_[(size_t) i][(size_t) (n - 1)] = gate * amount * tailRolloff / std::pow ((float) n, slope);
		}
	}
}

FREQTRAudioProcessor::HarmonicOscPair FREQTRAudioProcessor::fastHarmonicOscPair (float phase, float harmNorm, float fundamentalHz) const noexcept
{
	HarmonicOscPair pair;

	const float harm = juce::jlimit (0.0f, 1.0f, harmNorm);
	pair.sine = fastSin (phase);
	pair.cosine = fastCos (phase);

	if (harm <= 0.0f)
		return pair;

	const float absFundamentalHz = std::abs (fundamentalHz);
	const int nyquistLimitedCount = (absFundamentalHz > 0.001f && currentSampleRate > 0.0)
		? juce::jlimit (1, kMaxHarmonics,
			(int) std::floor ((0.45 * currentSampleRate) / juce::jmax (absFundamentalHz, 1.0f)))
		: kMaxHarmonics;
	const float tablePos = harm * (float) kHarmProfileTableSize;
	const int i0 = juce::jlimit (0, kHarmProfileTableSize - 1, (int) tablePos);
	const int i1 = i0 + 1;
	const float tableFrac = tablePos - (float) i0;

	float sumSq = 1.0f;
	for (int n = 2; n <= nyquistLimitedCount; ++n)
	{
		const float harmonicPhase = phase * (float) n;
		const float w0 = harmonicProfileTable_[(size_t) i0][(size_t) (n - 1)];
		const float w1 = harmonicProfileTable_[(size_t) i1][(size_t) (n - 1)];
		const float weight = w0 + (w1 - w0) * tableFrac;
		pair.sine += weight * fastSin (harmonicPhase);
		pair.cosine += weight * fastCos (harmonicPhase);
		sumSq += weight * weight;
	}

	const float gain = 1.0f / std::sqrt (sumSq);
	pair.sine *= gain;
	pair.cosine *= gain;

	return pair;
}

FREQTRAudioProcessor::~FREQTRAudioProcessor()
{
}

//==============================================================================
const juce::String FREQTRAudioProcessor::getName() const { return JucePlugin_Name; }

bool FREQTRAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
	return true;
   #else
	return false;
   #endif
}

bool FREQTRAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
	return true;
   #else
	return false;
   #endif
}

bool FREQTRAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
	return true;
   #else
	return false;
   #endif
}

double FREQTRAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int FREQTRAudioProcessor::getNumPrograms() { return 1; }
int FREQTRAudioProcessor::getCurrentProgram() { return 0; }
void FREQTRAudioProcessor::setCurrentProgram (int) {}
const juce::String FREQTRAudioProcessor::getProgramName (int) { return {}; }
void FREQTRAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void FREQTRAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
	juce::ignoreUnused (samplesPerBlock);
	currentSampleRate = sampleRate;

	// ── Design Hilbert FIRs and build folded taps ──
	{
		for (int lane = 0; lane < kNumHilbertWindows; ++lane)
		{
			const int firLength = kHilbertWindows[lane] - 1;
			std::vector<float> coeffs ((size_t) firLength, 0.0f);
			designHilbertFIR (coeffs.data(), firLength);

			const int half = firLength / 2;
			auto& taps = hilbertFoldedTapsByWindow_[(size_t) lane];
			taps.clear();

			// Exploit antisymmetry: h[k] = -h[N-1-k] for nonzero taps.
			for (int k = 0; k < half; ++k)
			{
				if (std::abs (coeffs[(size_t) k]) < 1e-12f)
					continue;
				taps.push_back ({ k, coeffs[(size_t) k] });
			}
		}
	}

	// Circular buffers for FIR convolution
	hilbertBufL.assign ((size_t) kHilbertMaxOrder, 0.0f);
	hilbertBufR.assign ((size_t) kHilbertMaxOrder, 0.0f);
	cleanDelayBufL.assign ((size_t) kHilbertMaxOrder, 0.0f);
	cleanDelayBufR.assign ((size_t) kHilbertMaxOrder, 0.0f);
	sidechainHilbertBufL.assign ((size_t) kHilbertMaxOrder, 0.0f);
	sidechainHilbertBufR.assign ((size_t) kHilbertMaxOrder, 0.0f);
	std::memset (hilbertWetCompBuf_, 0, sizeof (hilbertWetCompBuf_));

	hilbertPos = 0;
	{
		const int maxWindow = getCanonicalHilbertWindow (
			(int) std::lround (loadAtomicOrDefault (maxWindowParam, (float) kHilbertMaxWindowDefault)));
		activeMaxHilbertWindow_ = maxWindow;
		activeHilbertWindow_ = juce::jmin (getCanonicalHilbertWindow (
			(int) std::lround (loadAtomicOrDefault (windowParam, (float) kHilbertWindowDefault))), maxWindow);
	}
	targetHilbertWindow_ = activeHilbertWindow_;
	previousHilbertWindow_ = activeHilbertWindow_;
	targetMaxHilbertWindow_ = activeMaxHilbertWindow_;
	previousMaxHilbertWindow_ = activeMaxHilbertWindow_;
	hilbertWindowCrossfadeRemaining_ = 0;
	hilbertWindowCrossfadeTotal_ = 0;
	for (auto& state : freqShiftHilbertIir_)
		state.reset();
	oscPhase = 0.0;
	oscPhaseR = 0.0;
	smoothedFreq = 0.0f;
	smoothedEngine = 0.0f;
	smoothedHarm = 0.0f;
	smoothedMix = 1.0f;
	smoothedDryLevel = loadAtomicOrDefault (dryLevelParam, kDryLevelDefault);
	smoothedWetLevel = loadAtomicOrDefault (wetLevelParam, kWetLevelDefault);
	smoothedInputGain = 1.0f;
	smoothedOutputGain = 1.0f;
	smoothedPan = loadAtomicOrDefault (panParam, kPanDefault);
	smoothedLimThreshold = fastDecibelsToGain (loadAtomicOrDefault (limThresholdParam, kLimThresholdDefault));
	sidechainDcPrevInL_ = 0.0f;
	sidechainDcPrevInR_ = 0.0f;
	sidechainDcPrevOutL_ = 0.0f;
	sidechainDcPrevOutR_ = 0.0f;
	sidechainToneFilterL_.reset();
	sidechainToneFilterR_.reset();
	sidechainCarrierSmoothL_ = 0.0f;
	sidechainCarrierSmoothR_ = 0.0f;
	sidechainRmsEnv_ = 0.0f;
	sidechainGateSmoothed_ = 0.0f;
	sidechainDepthSmoothed_ = 0.0f;
	lastMidiNote.store (-1, std::memory_order_relaxed);
	lastMidiVelocity.store (0, std::memory_order_relaxed);
	currentMidiFrequency.store (0.0f, std::memory_order_relaxed);

	feedbackSmoothed.reset (currentSampleRate, kFeedbackSmoothingSeconds);
	feedbackSmoothed.setCurrentAndTargetValue (juce::jlimit (kFeedbackMin, kFeedbackMax, loadAtomicOrDefault (feedbackParam, kFeedbackDefault)));
	feedbackLastL = 0.0f;
	feedbackLastR = 0.0f;
	fbkDelaySize = juce::jmax (2, (int) std::ceil (currentSampleRate / (double) kCombMin) + 2);
	fbkDelayBufL.assign ((size_t) fbkDelaySize, 0.0f);
	fbkDelayBufR.assign ((size_t) fbkDelaySize, 0.0f);
	fbkDelayWritePos = 0;
	{
		const float initCombHz = juce::jlimit (kCombMin, kCombMax, loadAtomicOrDefault (combParam, kCombDefault));
		smoothedComb_ = (float) juce::jmax (1, (int) std::round (currentSampleRate / (double) initCombHz));
	}

	// DC blocker coefficient: R = e^(-2π * fHP / fs)
	fbkDcCoeff = std::exp (-juce::MathConstants<float>::twoPi * kFbkDcBlockHz / (float) currentSampleRate);
	fbkDcStateInL = fbkDcStateInR = fbkDcStateOutL = fbkDcStateOutR = 0.0f;

	// Feedback LPF: 2nd-order Butterworth at 0.35 × fs
	// Transparent below ~14 kHz, steep -12 dB/oct rolloff above, null at Nyquist.
	{
		const float fc = (float) currentSampleRate * 0.35f;
		const float K  = std::tan (juce::MathConstants<float>::pi * fc / (float) currentSampleRate);
		const float K2 = K * K;
		const float sqrt2K = std::sqrt (2.0f) * K;
		const float norm   = 1.0f / (1.0f + sqrt2K + K2);
		fbkLpCoeffs.b0 = K2 * norm;
		fbkLpCoeffs.b1 = 2.0f * fbkLpCoeffs.b0;
		fbkLpCoeffs.b2 = fbkLpCoeffs.b0;
		fbkLpCoeffs.a1 = 2.0f * (K2 - 1.0f) * norm;
		fbkLpCoeffs.a2 = (1.0f - sqrt2K + K2) * norm;
	}
	fbkLpStateL = fbkLpStateR = {};

	// Report latency if PDC enabled
	const bool pdcEnabled = loadBoolParamOrDefault (pdcParam, true);
	setLatencySamples (pdcEnabled ? kHilbertMaxDelay : 0);

	// Reset wet filter state
	wetFilterState_[0].reset();
	wetFilterState_[1].reset();
	smoothedFilterHpFreq_ = loadAtomicOrDefault (filterHpFreqParam, kFilterHpFreqDefault);
	smoothedFilterLpFreq_ = loadAtomicOrDefault (filterLpFreqParam, kFilterLpFreqDefault);
	lastCalcHpFreq_ = -1.0f;
	lastCalcLpFreq_ = -1.0f;
	lastCalcHpSlope_ = -1;
	lastCalcLpSlope_ = -1;
	filterCoeffCountdown_ = 0;
	updateFilterCoeffs (true, true);

	// Reset tilt state
	tiltDb_ = 0.0f;
	smoothedTiltDb_ = 0.0f;
	tiltB0_ = 1.0f; tiltB1_ = 0.0f; tiltA1_ = 0.0f;
	tiltStateL_ = 0.0f; tiltStateR_ = 0.0f;

	// Reset chaos state
	chaosFilterEnabled_ = false;
	chaosDelayEnabled_  = false;
	chaosStereo_ = false;
	chaosAmtD_ = 0.0f; chaosAmtF_ = 0.0f;
	chaosAmtNormD_ = 0.0f;
	for (int c = 0; c < 2; ++c)
	{
		chaosDPrev_[c] = 0.0f; chaosDCurr_[c] = 0.0f; chaosDNext_[c] = 0.0f;
		chaosDPhase_[c] = 0.0f; chaosDDriftPhase_[c] = 0.0f; chaosDDriftFreqHz_[c] = 0.0f;
		chaosDOut_[c] = 0.0f;
		chaosGPrev_[c] = 0.0f; chaosGCurr_[c] = 0.0f; chaosGNext_[c] = 0.0f;
		chaosGPhase_[c] = 0.0f; chaosGDriftPhase_[c] = 0.0f; chaosGDriftFreqHz_[c] = 0.0f;
		chaosGOut_[c] = 0.0f;
	}
	chaosFPrev_ = 0.0f; chaosFCurr_ = 0.0f; chaosFNext_ = 0.0f;
	chaosFPhase_ = 0.0f; chaosFDriftPhase_ = 0.0f; chaosFDriftFreqHz_ = 0.0f;
	chaosFOut_[0] = chaosFOut_[1] = 0.0f;
	smoothedChaosDelayMaxSamples_ = 0.0f;
	smoothedChaosGainMaxDb_ = 0.0f;
	smoothedChaosFilterMaxOct_ = 0.0f;
	chaosParamSmoothCoeff_ = 0.999f;
	chaosDriveAmtSmoothed_ = 0.0f;
	chaosDriveSpdSmoothed_ = kChaosSpdDefault;
	chaosDriveParamSmoothReady_ = false;
	chaosFilterAmtSmoothed_ = 0.0f;
	chaosFilterSpdSmoothed_ = kChaosSpdDefault;
	chaosFilterParamSmoothReady_ = false;
	std::memset (chaosDelayBuf_, 0, sizeof (chaosDelayBuf_));
	chaosDelayWritePos_ = 0;
	for (int c = 0; c < 2; ++c)
	{
		chaosDelaySmoothedSamples_[c] = 0.0f;
		chaosDelaySmoothReady_[c] = false;
	}
	resetJitterState();

	// Precompute sampleRate-dependent smooth coefficients
	cachedFreqEmaCoeff_          = std::exp (-1.0f / ((float) currentSampleRate * 0.005f));
	cachedChaosParamSmoothCoeff_ = std::exp (-1.0f / ((float) currentSampleRate * 0.010f));
	chaosDelaySmoothStep_        = 1.0f - std::exp (-1.0f / ((float) currentSampleRate * 0.002f));
	jitterParamSmoothCoeff_      = std::exp (-1.0f / ((float) currentSampleRate * (float) kJitterSmoothingSeconds));

	// Limiter state reset
	limEnv1_[0] = limEnv1_[1] = kLimFloor;
	limEnv2_[0] = limEnv2_[1] = kLimFloor;
	limAtt1_ = std::exp (-1.0f / ((float) currentSampleRate * 0.002f));
	limRel1_ = std::exp (-1.0f / ((float) currentSampleRate * 0.010f));
	limRel2_ = std::exp (-1.0f / ((float) currentSampleRate * 0.100f));
}

void FREQTRAudioProcessor::resetJitterState() noexcept
{
	jitterTargetNorm_ = 0.0f;
	jitterAmountSmoothed_ = 0.0f;
	jitterParamSmoothReady_ = false;
	jitterActive_ = false;
	jitterStereo_ = false;
	jitterFreqOut_[0] = jitterFreqOut_[1] = 0.0f;
	jitterFreqDepthOct_[0] = jitterFreqDepthOct_[1] = 0.0f;
	jitterFeedbackOut_ = 0.0f;
	jitterFeedbackDepth_ = 0.0f;
	jitterCombOut_ = 0.0f;
	jitterCombDepthOct_ = 0.0f;

	for (int ch = 0; ch < 2; ++ch)
	{
		const juce::int64 seedBase = (ch == 0) ? 0x465245514a495431ll : 0x465245514a495432ll;
		jitterFreqMod_[ch].reset (seedBase, ch == 0 ? 0.113f : 0.617f);
	}

	jitterFeedbackMod_.reset (0x465245514a495446ll, 0.381f);
	jitterCombMod_.reset (0x465245514a495443ll, 0.827f);

	jitterFreqOut_[0] = jitterFreqOut_[1] = 0.0f;
	jitterFreqDepthOct_[0] = jitterFreqDepthOct_[1] = 0.0f;
	jitterFeedbackOut_ = 0.0f;
	jitterFeedbackDepth_ = 0.0f;
	jitterCombOut_ = 0.0f;
	jitterCombDepthOct_ = 0.0f;
}

void FREQTRAudioProcessor::updateFilterCoeffs (bool forceHp, bool forceLp)
{
	const float sr = (float) currentSampleRate;
	const int hpSlope = juce::jlimit (kFilterSlopeMin, kFilterSlopeMax,
									  loadIntParamOrDefault (filterHpSlopeParam, kFilterSlopeDefault));
	const int lpSlope = juce::jlimit (kFilterSlopeMin, kFilterSlopeMax,
									  loadIntParamOrDefault (filterLpSlopeParam, kFilterSlopeDefault));

	const float hpFreq = juce::jlimit (kFilterFreqMin, juce::jmin (kFilterFreqMax, 0.49f * sr), smoothedFilterHpFreq_);
	const float lpFreq = juce::jlimit (kFilterFreqMin, juce::jmin (kFilterFreqMax, 0.49f * sr), smoothedFilterLpFreq_);

	if (forceHp || hpSlope != lastCalcHpSlope_ || std::abs (hpFreq - lastCalcHpFreq_) > 0.01f)
	{
		lastCalcHpFreq_  = hpFreq;
		lastCalcHpSlope_ = hpSlope;

		if (hpSlope == 0)      // 6 dB/oct — single 1-pole
		{
			hpCoeffs_[0] = calcOnePoleHP (hpFreq, sr);
			hpCoeffs_[1] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };  // pass-through
		}
		else if (hpSlope == 1) // 12 dB/oct — single Butterworth biquad
		{
			constexpr float kBW2_Q = 0.70710678f;  // 1/sqrt(2)
			hpCoeffs_[0] = calcBiquadHP (hpFreq, sr, kBW2_Q);
			hpCoeffs_[1] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
		}
		else                   // 24 dB/oct — two cascaded Butterworth biquads
		{
			hpCoeffs_[0] = calcBiquadHP (hpFreq, sr, kBW4_Q1);
			hpCoeffs_[1] = calcBiquadHP (hpFreq, sr, kBW4_Q2);
		}
	}

	if (forceLp || lpSlope != lastCalcLpSlope_ || std::abs (lpFreq - lastCalcLpFreq_) > 0.01f)
	{
		lastCalcLpFreq_  = lpFreq;
		lastCalcLpSlope_ = lpSlope;

		if (lpSlope == 0)
		{
			lpCoeffs_[0] = calcOnePoleLP (lpFreq, sr);
			lpCoeffs_[1] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
		}
		else if (lpSlope == 1)
		{
			constexpr float kBW2_Q = 0.70710678f;
			lpCoeffs_[0] = calcBiquadLP (lpFreq, sr, kBW2_Q);
			lpCoeffs_[1] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
		}
		else
		{
			lpCoeffs_[0] = calcBiquadLP (lpFreq, sr, kBW4_Q1);
			lpCoeffs_[1] = calcBiquadLP (lpFreq, sr, kBW4_Q2);
		}
	}
}

void FREQTRAudioProcessor::releaseResources()
{
	hilbertBufL.clear();
	hilbertBufR.clear();
	sidechainHilbertBufL.clear();
	sidechainHilbertBufR.clear();
	for (auto& taps : hilbertFoldedTapsByWindow_)
		taps.clear();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FREQTRAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
	juce::ignoreUnused (layouts);
	return true;
  #else
	if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
	 && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
		return false;

   #if ! JucePlugin_IsSynth
	if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
		return false;

	if (getBusCount (true) > 1)
	{
		const auto sidechainLayout = layouts.getChannelSet (true, 1);
		if (! sidechainLayout.isDisabled()
			&& sidechainLayout != juce::AudioChannelSet::mono()
			&& sidechainLayout != juce::AudioChannelSet::stereo())
			return false;
	}
   #endif

	return true;
  #endif
}
#endif

//==============================================================================
namespace
{
    inline float dcBlockTick (float in, float& inState, float& outState, float r) noexcept
    {
        outState = r * (outState + in - inState);
        inState = in;
        return outState;
    }

    // Direct Form II Transposed biquad (numerically stable at high frequencies)
    inline float biquadTick (float x, FREQTRAudioProcessor::BiquadState& st,
                             const FREQTRAudioProcessor::BiquadCoeffs& c) noexcept
    {
        const float y = c.b0 * x + st.s1;
        st.s1 = c.b1 * x - c.a1 * y + st.s2;
        st.s2 = c.b2 * x - c.a2 * y;
        return y;
    }
}

void FREQTRAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
	juce::ScopedNoDenormals noDenormals;

	const int numChannels = juce::jmin (getTotalNumOutputChannels(), 2);
	const int numSamples  = buffer.getNumSamples();

	const bool sidechainEnabled = loadBoolParamOrDefault (sidechainParam, false);
	const float* sidechainReadL = nullptr;
	const float* sidechainReadR = nullptr;
	int sidechainChannels = 0;
	float sidechainPeak = 0.0f;

	if (sidechainEnabled && getBusCount (true) > 1)
	{
		auto sidechainBuffer = getBusBuffer (buffer, true, 1);
		sidechainChannels = sidechainBuffer.getNumChannels();
		if (sidechainChannels > 0)
		{
			sidechainReadL = sidechainBuffer.getReadPointer (0);
			sidechainReadR = (sidechainChannels > 1) ? sidechainBuffer.getReadPointer (1) : sidechainReadL;

			for (int ch = 0; ch < juce::jmin (sidechainChannels, 2); ++ch)
				sidechainPeak = juce::jmax (sidechainPeak,
					sidechainBuffer.getMagnitude (ch, 0, numSamples));
		}
	}
	const bool sidechainHasInput = sidechainEnabled && sidechainReadL != nullptr && sidechainPeak > 1.0e-6f;

	// ── MIDI note tracking ──
	const bool midiEnabled = loadBoolParamOrDefault (midiParam, false);

	if (midiEnabled && ! midiMessages.isEmpty())
	{
		const int selectedMidiChannel = midiChannel.load (std::memory_order_relaxed);
		for (const auto metadata : midiMessages)
		{
			const auto msg = metadata.getMessage();

			if (selectedMidiChannel > 0 && msg.getChannel() != selectedMidiChannel)
				continue;

			if (msg.isAllNotesOff() || msg.isAllSoundOff())
			{
				lastMidiNote.store (-1, std::memory_order_relaxed);
				lastMidiVelocity.store (0, std::memory_order_relaxed);
				currentMidiFrequency.store (0.0f, std::memory_order_relaxed);
			}
			else if (msg.isNoteOn())
			{
				const int noteNumber = msg.getNoteNumber();
				lastMidiNote.store (noteNumber, std::memory_order_relaxed);
				lastMidiVelocity.store (msg.getVelocity(), std::memory_order_relaxed);
				const float frequency = 440.0f * std::exp2 ((noteNumber - 69) * (1.0f / 12.0f));
				currentMidiFrequency.store (frequency, std::memory_order_relaxed);
			}
			else if (msg.isNoteOff())
			{
				if (msg.getNoteNumber() == lastMidiNote.load (std::memory_order_relaxed))
				{
					lastMidiNote.store (-1, std::memory_order_relaxed);
					lastMidiVelocity.store (0, std::memory_order_relaxed);
					currentMidiFrequency.store (0.0f, std::memory_order_relaxed);
				}
			}
		}
	}
	else if (! midiEnabled)
	{
		if (lastMidiNote.load (std::memory_order_relaxed) >= 0)
		{
			lastMidiNote.store (-1, std::memory_order_relaxed);
			lastMidiVelocity.store (0, std::memory_order_relaxed);
			currentMidiFrequency.store (0.0f, std::memory_order_relaxed);
		}
	}

	for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
		buffer.clear (i, 0, numSamples);

	if (currentSampleRate <= 0.0
		|| hilbertFoldedTapsByWindow_[0].empty()
		|| hilbertFoldedTapsByWindow_[1].empty()
		|| hilbertFoldedTapsByWindow_[2].empty()
		|| hilbertFoldedTapsByWindow_[3].empty()
		|| hilbertFoldedTapsByWindow_[4].empty())
		return;

	// ── Read parameters ──
	float targetFreq   = loadAtomicOrDefault (freqParam, kFreqDefault);
	const float modVal = loadAtomicOrDefault (modParam, kModDefault);
	const float rawFeedback = juce::jlimit (kFeedbackMin, kFeedbackMax, loadAtomicOrDefault (feedbackParam, kFeedbackDefault));
	const float targetFeedback = rawFeedback * rawFeedback * (3.0f - 2.0f * rawFeedback) * 0.99f;
	const float combHz = juce::jlimit (kCombMin, kCombMax,
		loadAtomicOrDefault (combParam, kCombDefault));
	const float targetComb = (float) juce::jmax (1, (int) std::round (currentSampleRate / (double) combHz));
	const float engine = loadAtomicOrDefault (engineParam, kEngineDefault);
	const int requestedMaxHilbertWindow = getCanonicalHilbertWindow (
		(int) std::lround (loadAtomicOrDefault (maxWindowParam, (float) kHilbertMaxWindowDefault)));
	const int requestedMaxDelay = getHilbertDelayForWindow (requestedMaxHilbertWindow);
	const int requestedHilbertWindow = getCanonicalHilbertWindow (
		(int) std::lround (loadAtomicOrDefault (windowParam, (float) kHilbertWindowDefault)));
	const int targetHilbertWindow = juce::jmin (requestedHilbertWindow, requestedMaxHilbertWindow);
	const int   style  = loadIntParamOrDefault (styleParam, 0);
	const float harm   = loadAtomicOrDefault (harmParam, kHarmDefault);
	const float polarity = loadAtomicOrDefault (polarityParam, kPolarityDefault);
	const float jitterTarget = juce::jlimit (kJitterMin, kJitterMax, loadAtomicOrDefault (jitterParam, kJitterDefault));
	jitterTargetNorm_ = jitterTarget;
	jitterStereo_ = (style >= 1);
	jitterActive_ = (jitterTarget > 0.000001f)
	             || (jitterAmountSmoothed_ > 0.000001f)
	             || jitterParamSmoothReady_;
	const float mix    = loadAtomicOrDefault (mixParam, kMixDefault);
	const float inputDb  = loadAtomicOrDefault (inputParam, kInputDefault);
	const float outputDb = loadAtomicOrDefault (outputParam, kOutputDefault);
	const float inputGain  = gainFaderDecibelsToGain (inputDb);
	const float outputGain = gainFaderDecibelsToGain (outputDb);

	// ── Limiter ──
	const int limMode = loadIntParamOrDefault (limModeParam, kLimModeDefault);
	const float targetLimThreshLin = (limMode != 0)
		? fastDecibelsToGain (loadAtomicOrDefault (limThresholdParam, kLimThresholdDefault))
		: 1.0f;

	const int invPol = loadIntParamOrDefault (invPolParam, kInvPolDefault);
	const int invStr = loadIntParamOrDefault (invStrParam, kInvStrDefault);

	const int mixMode = loadIntParamOrDefault (mixModeParam, kMixModeDefault);
	const float dryLevel = loadAtomicOrDefault (dryLevelParam, kDryLevelDefault);
	const float wetLevel = loadAtomicOrDefault (wetLevelParam, kWetLevelDefault);
	const float targetPan = loadAtomicOrDefault (panParam, kPanDefault);
	// Filter / Tilt position
	{
		const int fltPos = loadIntParamOrDefault (filterPosParam, kFilterPosDefault);
		// 0=F▼T▼  1=F▲T▲  2=F▲T▼  3=F▼T▲
		filterPre_ = (fltPos == 1 || fltPos == 2);
		tiltPre_   = (fltPos == 1 || fltPos == 3);
	}

	const bool  hpOn = loadBoolParamOrDefault (filterHpOnParam, false);
	const bool  lpOn = loadBoolParamOrDefault (filterLpOnParam, false);
	const bool  syncEnabled  = loadBoolParamOrDefault (syncParam, false);
	const bool  alignEnabled = loadBoolParamOrDefault (alignParam, true);
	const bool  pdcEnabled   = loadBoolParamOrDefault (pdcParam, true);
	const float sidechainTimeTarget = juce::jlimit (kSidechainTimeMin, kSidechainTimeMax,
		loadAtomicOrDefault (sidechainTimeParam, kSidechainTimeDefault));
	const float sidechainToneTarget = juce::jlimit (kSidechainToneMin, kSidechainToneMax,
		loadAtomicOrDefault (sidechainToneParam, kSidechainToneDefault));
	const float sidechainGateTau = 0.001f + sidechainTimeTarget * sidechainTimeTarget * 0.040f;
	const float sidechainGateCoeff = std::exp (-1.0f / ((float) currentSampleRate * sidechainGateTau));
	const float sidechainAgcTau = 0.020f + sidechainTimeTarget * 0.130f;
	const float sidechainAgcCoeff = std::exp (-1.0f / ((float) currentSampleRate * sidechainAgcTau));
	const float sidechainDcCoeff = std::exp (-juce::MathConstants<float>::twoPi * 20.0f / (float) currentSampleRate);
	const float sidechainToneEndFactor = std::pow (
		std::pow (10.0f, 18.0f / 10.0f) - 1.0f, 1.0f / 6.0f);
	const float sidechainToneEndHz = juce::jmin (sidechainToneTarget, (float) currentSampleRate * 0.45f);
	const float sidechainToneCutoffHz = juce::jlimit (20.0f, (float) currentSampleRate * 0.45f,
		((float) currentSampleRate / juce::MathConstants<float>::pi)
			* std::atan (std::tan (juce::MathConstants<float>::pi * sidechainToneEndHz
				/ (float) currentSampleRate) / sidechainToneEndFactor));
	const float sidechainToneK = std::tan (juce::MathConstants<float>::pi * sidechainToneCutoffHz
		/ (float) currentSampleRate);
	const float sidechainToneOneNorm = 1.0f / (1.0f + sidechainToneK);
	const float sidechainToneOneB0 = sidechainToneK * sidechainToneOneNorm;
	const float sidechainToneOneB1 = sidechainToneOneB0;
	const float sidechainToneOneA1 = (sidechainToneK - 1.0f) * sidechainToneOneNorm;
	const float sidechainToneBiquadQ = 1.0f;
	const float sidechainToneK2 = sidechainToneK * sidechainToneK;
	const float sidechainToneBiquadNorm = 1.0f / (1.0f + sidechainToneK / sidechainToneBiquadQ
		+ sidechainToneK2);
	const float sidechainToneBqB0 = sidechainToneK2 * sidechainToneBiquadNorm;
	const float sidechainToneBqB1 = 2.0f * sidechainToneBqB0;
	const float sidechainToneBqB2 = sidechainToneBqB0;
	const float sidechainToneBqA1 = 2.0f * (sidechainToneK2 - 1.0f) * sidechainToneBiquadNorm;
	const float sidechainToneBqA2 = (1.0f - sidechainToneK / sidechainToneBiquadQ + sidechainToneK2)
		* sidechainToneBiquadNorm;
	const float sidechainTimeSquared = sidechainTimeTarget * sidechainTimeTarget;
	const float sidechainCarrierSmoothHz = juce::jlimit (20.0f, (float) currentSampleRate * 0.45f,
		sidechainToneTarget * (4.0f - 3.75f * sidechainTimeSquared));
	const float sidechainCarrierSmoothCoeff = std::exp (-juce::MathConstants<float>::twoPi
		* sidechainCarrierSmoothHz / (float) currentSampleRate);

	setLatencySamples (pdcEnabled ? requestedMaxDelay : 0);

	if (targetHilbertWindow != targetHilbertWindow_
		|| requestedMaxHilbertWindow != targetMaxHilbertWindow_)
	{
		previousHilbertWindow_ = activeHilbertWindow_;
		previousMaxHilbertWindow_ = activeMaxHilbertWindow_;
		targetHilbertWindow_ = targetHilbertWindow;
		targetMaxHilbertWindow_ = requestedMaxHilbertWindow;
		hilbertWindowCrossfadeTotal_ = juce::jmax (1, (int) std::round (currentSampleRate * 0.030));
		hilbertWindowCrossfadeRemaining_ = hilbertWindowCrossfadeTotal_;
	}

	useSyncRetrigPhase = false; // reset per block; set true if SYNC+RETRIG+PPQ

	// Priority: MIDI note > Sync > Manual frequency (same as ECHO-TR)
	const int  midiNote       = lastMidiNote.load (std::memory_order_relaxed);
	const bool midiNoteActive = midiEnabled && (midiNote >= 0);

	if (midiNoteActive)
	{
		const float midiFreq = currentMidiFrequency.load (std::memory_order_relaxed);
		if (midiFreq > 0.1f)
			targetFreq = midiFreq;
	}
	else if (syncEnabled)
	{
		const int freqSyncValue = loadIntParamOrDefault (
			apvts.getRawParameterValue (kParamFreqSync), kFreqSyncDefault);
		double bpm = 120.0;
		double ppqPos = 0.0;
		bool ppqAvailable = false;
		auto posInfo = getPlayHead();
		if (posInfo != nullptr)
		{
			auto pos = posInfo->getPosition();
			if (pos.hasValue())
			{
				if (pos->getBpm().hasValue())
					bpm = *pos->getBpm();
				if (pos->getPpqPosition().hasValue())
				{
					ppqPos = *pos->getPpqPosition();
					ppqAvailable = true;
				}
			}
		}
		targetFreq = tempoSyncToHz (freqSyncValue, bpm);

		// ── RETRIG: compute oscPhase from PPQ position ──
		const bool retrigEnabled = loadBoolParamOrDefault (retrigParam, false);
		if (retrigEnabled && ppqAvailable)
		{
			// Period in quarter-note beats
			const int syncIdx = juce::jlimit (0, 29, freqSyncValue);
			const float divisions[] = { 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };
			const int baseIdx = syncIdx / 3;
			const int modifier = syncIdx % 3;
			float periodBeats = 4.0f / divisions[baseIdx]; // in quarter-note beats
			if (modifier == 0)       periodBeats *= (2.0f / 3.0f); // triplet
			else if (modifier == 2)  periodBeats *= 1.5f;          // dotted

			if (periodBeats > 0.0001f)
			{
				const double phaseFromPpq = std::fmod (ppqPos / (double) periodBeats, 1.0);
				syncRetrigPhase = (phaseFromPpq < 0.0) ? phaseFromPpq + 1.0 : phaseFromPpq;
				useSyncRetrigPhase = true;
			}
		}
	}

	// MOD multiplier (hyperbolic below centre, linear above — same as ECHO-TR/DISP-TR)
	float freqMultiplier;
	if (modVal < 0.5f)
		freqMultiplier = 1.0f / (4.0f - 6.0f * modVal);
	else
		freqMultiplier = 1.0f + ((modVal - 0.5f) * 6.0f);

	// Apply polarity as continuous multiplier (-1..+1; 0 = no effect)
	targetFreq *= freqMultiplier * polarity;
	targetFreq = juce::jlimit (-kFreqMax * 4.0f, kFreqMax * 4.0f, targetFreq);

	// Frequency smoothing (EMA, ~5ms) with MIDI glide support
	float freqCoeff = cachedFreqEmaCoeff_;

	// MIDI velocity-controlled glide
	if (midiNoteActive)
	{
		const float vel  = (float) lastMidiVelocity.load (std::memory_order_relaxed);
		const float tLin = juce::jlimit (0.0f, 1.0f, (vel - 1.0f) / 126.0f);

		constexpr float kTauMax = 0.200f;   // 200 ms — full portamento at pianissimo
		constexpr float kTauMin = 0.0002f;  // 0.2 ms — imperceptible at max velocity

		const float t   = std::pow (tLin, 0.05f);
		const float tau = kTauMax - t * (kTauMax - kTauMin);
		freqCoeff = std::exp (-1.0f / ((float) currentSampleRate * tau));
	}

	// EMA coefficient for mix/engine/harm (~5 ms)
	const float paramCoeff = cachedFreqEmaCoeff_;

	feedbackSmoothed.setTargetValue (targetFeedback);

	const int order = kHilbertMaxOrder;
	const int orderMask = kHilbertMaxOrder - 1;
	const float invSr = 1.0f / (float) currentSampleRate;

	float* writeL = (numChannels > 0) ? buffer.getWritePointer (0) : nullptr;
	float* writeR = (numChannels > 1) ? buffer.getWritePointer (1) : nullptr;

	// ── Wet filter targets ──
	float targetHpFreq = 0.0f, targetLpFreq = 0.0f;
	int numSections_hp = 0, numSections_lp = 0;
	if (hpOn || lpOn)
	{
		targetHpFreq = juce::jlimit (kFilterFreqMin, kFilterFreqMax,
			loadAtomicOrDefault (filterHpFreqParam, kFilterHpFreqDefault));
		targetLpFreq = juce::jlimit (kFilterFreqMin, kFilterFreqMax,
			loadAtomicOrDefault (filterLpFreqParam, kFilterLpFreqDefault));
		const int hpSlope = juce::jlimit (kFilterSlopeMin, kFilterSlopeMax,
			loadIntParamOrDefault (filterHpSlopeParam, kFilterSlopeDefault));
		const int lpSlope = juce::jlimit (kFilterSlopeMin, kFilterSlopeMax,
			loadIntParamOrDefault (filterLpSlopeParam, kFilterSlopeDefault));
		numSections_hp = (hpSlope == 2) ? 2 : 1;
		numSections_lp = (lpSlope == 2) ? 2 : 1;
	}

	// ── Tilt target ──
	const float tiltTarget = loadAtomicOrDefault (tiltParam, kTiltDefault);

	// ── Chaos ──
	chaosFilterEnabled_ = loadBoolParamOrDefault (chaosParam, false);
	chaosDelayEnabled_  = loadBoolParamOrDefault (chaosDelayParam, false);
	const bool anyChaos = chaosFilterEnabled_ || chaosDelayEnabled_;
	if (anyChaos)
	{
		if (chaosDelayEnabled_)
		{
			const float rawAmtD = juce::jlimit (kChaosAmtMin, kChaosAmtMax,
				loadAtomicOrDefault (chaosAmtParam, kChaosAmtDefault));
			const float rawSpdD = juce::jlimit (kChaosSpdMin, kChaosSpdMax,
				loadAtomicOrDefault (chaosSpdParam, kChaosSpdDefault));
			chaosAmtD_       = rawAmtD;
			chaosAmtNormD_   = rawAmtD * 0.01f;
			chaosShPeriodD_  = (float) currentSampleRate / rawSpdD;
			const float amtNormD = rawAmtD * 0.01f;
			chaosDelayMaxSamples_ = amtNormD * 0.005f * (float) currentSampleRate;  // ±5ms at 100%
			chaosGainMaxDb_       = amtNormD * 1.0f;                                // ±1dB at 100%
		}
		else
		{
			chaosDelayMaxSamples_ = 0.0f;
			chaosGainMaxDb_ = 0.0f;
			chaosDriveAmtSmoothed_ = 0.0f;
			chaosDriveSpdSmoothed_ = kChaosSpdDefault;
			chaosDriveParamSmoothReady_ = false;
			chaosDelaySmoothedSamples_[0] = chaosDelaySmoothedSamples_[1] = 0.0f;
			chaosDelaySmoothReady_[0] = chaosDelaySmoothReady_[1] = false;
		}

		if (chaosFilterEnabled_)
		{
			const float rawAmtF = juce::jlimit (kChaosAmtMin, kChaosAmtMax,
				loadAtomicOrDefault (chaosAmtFilterParam, kChaosAmtDefault));
			const float rawSpdF = juce::jlimit (kChaosSpdMin, kChaosSpdMax,
				loadAtomicOrDefault (chaosSpdFilterParam, kChaosSpdDefault));
			chaosAmtF_       = rawAmtF;
			chaosShPeriodF_  = (float) currentSampleRate / rawSpdF;
			const float amtNormF = rawAmtF * 0.01f;
			chaosFilterMaxOct_ = amtNormF * 2.0f;  // ±2 oct at 100%
		}
		else
		{
			chaosFilterMaxOct_ = 0.0f;
			chaosFilterAmtSmoothed_ = 0.0f;
			chaosFilterSpdSmoothed_ = kChaosSpdDefault;
			chaosFilterParamSmoothReady_ = false;
		}

		chaosParamSmoothCoeff_ = cachedChaosParamSmoothCoeff_;
		chaosStereo_ = (style >= 1);
	}
	else
	{
		chaosAmtD_ = 0.0f; chaosAmtF_ = 0.0f;
		chaosAmtNormD_ = 0.0f;
		chaosDelayMaxSamples_ = 0.0f;
		chaosGainMaxDb_ = 0.0f;
		chaosFilterMaxOct_ = 0.0f;
		chaosDriveAmtSmoothed_ = 0.0f;
		chaosDriveSpdSmoothed_ = kChaosSpdDefault;
		chaosDriveParamSmoothReady_ = false;
		chaosFilterAmtSmoothed_ = 0.0f;
		chaosFilterSpdSmoothed_ = kChaosSpdDefault;
		chaosFilterParamSmoothReady_ = false;
		chaosDelaySmoothedSamples_[0] = chaosDelaySmoothedSamples_[1] = 0.0f;
		chaosDelaySmoothReady_[0] = chaosDelaySmoothReady_[1] = false;
	}

	// ── Mode In / Mode Out / Sum Bus ──
	const int modeInVal  = loadIntParamOrDefault (modeInParam,  kModeInOutDefault);
	const int modeOutVal = loadIntParamOrDefault (modeOutParam, kModeInOutDefault);
	const int sumBusVal  = loadIntParamOrDefault (sumBusParam,  kSumBusDefault);
	double syncPhaseAccumL = syncRetrigPhase;
	double syncPhaseAccumR = syncRetrigPhase;

	auto processSidechainTone = [&] (float x, SidechainToneFilterState& state) noexcept
	{
		const float oneY = sidechainToneOneB0 * x + sidechainToneOneB1 * state.oneX1
			- sidechainToneOneA1 * state.oneY1;
		state.oneX1 = x;
		state.oneY1 = oneY;

		const float y = sidechainToneBqB0 * oneY
			+ sidechainToneBqB1 * state.biquadX1
			+ sidechainToneBqB2 * state.biquadX2
			- sidechainToneBqA1 * state.biquadY1
			- sidechainToneBqA2 * state.biquadY2;
		state.biquadX2 = state.biquadX1;
		state.biquadX1 = oneY;
		state.biquadY2 = state.biquadY1;
		state.biquadY1 = y;
		return y;
	};

	for (int n = 0; n < numSamples; ++n)
	{
		smoothedFreq = freqCoeff * smoothedFreq + (1.0f - freqCoeff) * targetFreq;
		smoothedEngine = paramCoeff * smoothedEngine + (1.0f - paramCoeff) * engine;
		smoothedHarm   = paramCoeff * smoothedHarm   + (1.0f - paramCoeff) * harm;
		smoothedMix    = paramCoeff * smoothedMix    + (1.0f - paramCoeff) * mix;
		smoothedDryLevel = paramCoeff * smoothedDryLevel + (1.0f - paramCoeff) * dryLevel;
		smoothedWetLevel = paramCoeff * smoothedWetLevel + (1.0f - paramCoeff) * wetLevel;
		smoothedInputGain  = paramCoeff * smoothedInputGain  + (1.0f - paramCoeff) * inputGain;
		smoothedOutputGain = paramCoeff * smoothedOutputGain + (1.0f - paramCoeff) * outputGain;
		smoothedPan = paramCoeff * smoothedPan + (1.0f - paramCoeff) * targetPan;
		smoothedLimThreshold = paramCoeff * smoothedLimThreshold + (1.0f - paramCoeff) * targetLimThreshLin;

		const float rFreqRatio = (style == 3) ? 0.5f : 1.0f;
		const float baseFreqL = smoothedFreq;
		const float baseFreqR = smoothedFreq * rFreqRatio;

		if (jitterActive_)
			advanceJitter (calcJitterFrequencyDelaySamples (baseFreqL),
			               calcJitterFrequencyDelaySamples (baseFreqR),
			               smoothedComb_);

		const float jitteredFreqL = getJitteredFrequencyHz (baseFreqL, 0);
		const float jitteredFreqR = getJitteredFrequencyHz (baseFreqR, 1);

		// ── Phase: retrig from PPQ or free-running ──
		if (useSyncRetrigPhase)
		{
			oscPhase = syncPhaseAccumL;
			oscPhase -= std::floor (oscPhase);
		}

		// R-channel phase: only DUAL uses a different rate (×0.5)
		// WIDE uses the same oscillator as L (opposite sideband, not different rate)
		if (useSyncRetrigPhase)
		{
			oscPhaseR = syncPhaseAccumR;
			oscPhaseR -= std::floor (oscPhaseR);
		}

		const float inL = (writeL != nullptr) ? writeL[n] : 0.0f;
		const float inR = (writeR != nullptr) ? writeR[n] : inL;

		// Mode In: M/S encode input
		float mInL = inL, mInR = inR;
		if (numChannels >= 2 && modeInVal != 0)
		{
			if (modeInVal == 1)      { const float mid  = (inL + inR) * kSqrt2Over2; mInL = mInR = mid; }
			else /* modeInVal==2 */   { const float side = (inL - inR) * kSqrt2Over2; mInL = mInR = side; }
		}

		// Write clean input into Hilbert buffer (no feedback — keeps Hilbert clean)
		const float sidechainGateTarget = sidechainHasInput ? 1.0f : 0.0f;
		sidechainGateSmoothed_ = sidechainGateCoeff * sidechainGateSmoothed_
			+ (1.0f - sidechainGateCoeff) * sidechainGateTarget;

		const float sidechainRawL = sidechainHasInput ? sidechainReadL[n] : 0.0f;
		const float sidechainRawR = sidechainHasInput ? sidechainReadR[n] : sidechainRawL;
		const float sidechainDcL = sidechainRawL - sidechainDcPrevInL_ + sidechainDcCoeff * sidechainDcPrevOutL_;
		const float sidechainDcR = sidechainRawR - sidechainDcPrevInR_ + sidechainDcCoeff * sidechainDcPrevOutR_;
		sidechainDcPrevInL_ = sidechainRawL;
		sidechainDcPrevInR_ = sidechainRawR;
		sidechainDcPrevOutL_ = sidechainDcL;
		sidechainDcPrevOutR_ = sidechainDcR;

		const float sidechainToneL = processSidechainTone (sidechainDcL, sidechainToneFilterL_);
		const float sidechainToneR = processSidechainTone (sidechainDcR, sidechainToneFilterR_);
		sidechainCarrierSmoothL_ = sidechainCarrierSmoothCoeff * sidechainCarrierSmoothL_
			+ (1.0f - sidechainCarrierSmoothCoeff) * sidechainToneL;
		sidechainCarrierSmoothR_ = sidechainCarrierSmoothCoeff * sidechainCarrierSmoothR_
			+ (1.0f - sidechainCarrierSmoothCoeff) * sidechainToneR;

		const float sidechainEnergy = 0.5f * (sidechainCarrierSmoothL_ * sidechainCarrierSmoothL_
			+ sidechainCarrierSmoothR_ * sidechainCarrierSmoothR_);
		sidechainRmsEnv_ = sidechainAgcCoeff * sidechainRmsEnv_
			+ (1.0f - sidechainAgcCoeff) * sidechainEnergy;
		const float sidechainRms = std::sqrt (juce::jmax (sidechainRmsEnv_, 1.0e-10f));
		const float sidechainDepthTarget = sidechainHasInput
			? std::sqrt (juce::jlimit (0.0f, 1.0f, sidechainRms * 8.0f))
			: 0.0f;
		sidechainDepthSmoothed_ = sidechainGateCoeff * sidechainDepthSmoothed_
			+ (1.0f - sidechainGateCoeff) * sidechainDepthTarget;
		const float sidechainAutoGain = juce::jlimit (0.35f, 3.0f, 0.5f / sidechainRms);
		const float sidechainPartialGain = 1.0f + 0.65f * (sidechainAutoGain - 1.0f);
		const float sidechainCarrierL = sidechainHasInput ? sidechainCarrierSmoothL_ * sidechainPartialGain : 0.0f;
		const float sidechainCarrierR = sidechainHasInput ? sidechainCarrierSmoothR_ * sidechainPartialGain : 0.0f;

		sidechainHilbertBufL[(size_t) hilbertPos] = std::tanh (sidechainCarrierL);
		sidechainHilbertBufR[(size_t) hilbertPos] = std::tanh (sidechainCarrierR);

		cleanDelayBufL[(size_t) hilbertPos] = mInL;
		cleanDelayBufR[(size_t) hilbertPos] = mInR;

		// Feedback: read from Comb-controlled delay line
		const float fb = applyJitterToFeedbackMagnitude (feedbackSmoothed.getNextValue());
		const float sidechainPolarity = sidechainEnabled ? juce::jlimit (kPolarityMin, kPolarityMax, polarity) : 1.0f;
		const float sidechainPolarityAbs = std::abs (sidechainPolarity);
		const float sidechainPolaritySign = sidechainPolarity < 0.0f ? -1.0f : 1.0f;
		const float carrierPresence = sidechainEnabled
			? sidechainGateSmoothed_ * sidechainPolarityAbs
			: 1.0f;
		const float effectiveFb = (carrierPresence > 0.0001f) ? fb * carrierPresence : 0.0f;

		// Smooth the comb size to avoid clicks
		const float jitteredTargetComb = applyJitterToCombTarget (targetComb);
		smoothedComb_ = smoothedComb_ * 0.999f + jitteredTargetComb * 0.001f;
		const int combSamples = juce::jlimit (1, juce::jmax (1, fbkDelaySize),
			(int) std::round (smoothedComb_));

		// Read feedback from delay line at Comb offset
		const int fbReadPos = (fbkDelayWritePos - combSamples + fbkDelaySize)
			% fbkDelaySize;
		const float fbSrcL = (style == 2) ? fbkDelayBufR[(size_t) fbReadPos] : fbkDelayBufL[(size_t) fbReadPos];
		const float fbSrcR = (style == 2) ? fbkDelayBufL[(size_t) fbReadPos] : fbkDelayBufR[(size_t) fbReadPos];

		// ── PRE wet filter (HP + LP) ── applied to input before effect
		if (filterPre_ && (hpOn || lpOn))
		{
			smoothedFilterHpFreq_ = smoothedFilterHpFreq_ * kGainSmoothCoeff
				+ targetHpFreq * (1.0f - kGainSmoothCoeff);
			smoothedFilterLpFreq_ = smoothedFilterLpFreq_ * kGainSmoothCoeff
				+ targetLpFreq * (1.0f - kGainSmoothCoeff);

			--filterCoeffCountdown_;
			if (filterCoeffCountdown_ <= 0)
			{
				filterCoeffCountdown_ = kFilterCoeffUpdateInterval;
				if (chaosFilterEnabled_
					&& (chaosAmtF_ > 0.01f || (chaosFilterParamSmoothReady_ && chaosFilterAmtSmoothed_ > 0.01f)))
				{
					const float sHp = smoothedFilterHpFreq_;
					const float sLp = smoothedFilterLpFreq_;

					// L channel coefficients
					const float octL = chaosFOut_[0] * smoothedChaosFilterMaxOct_;
					const float multL = std::exp2 (octL);
					smoothedFilterHpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax,
						(hpOn ? sHp : kFilterFreqMin) * multL);
					smoothedFilterLpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax,
						(lpOn ? sLp : kFilterFreqMax) * multL);
					updateFilterCoeffs (true, true);

					if (chaosStereo_)
					{
						// Save L coefficients, compute R with quadrature offset
						auto hpL0 = hpCoeffs_[0]; auto hpL1 = hpCoeffs_[1];
						auto lpL0 = lpCoeffs_[0]; auto lpL1 = lpCoeffs_[1];

						const float octR = chaosFOut_[1] * smoothedChaosFilterMaxOct_;
						const float multR = std::exp2 (octR);
						smoothedFilterHpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax,
							(hpOn ? sHp : kFilterFreqMin) * multR);
						smoothedFilterLpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax,
							(lpOn ? sLp : kFilterFreqMax) * multR);
						updateFilterCoeffs (true, true);

						hpCoeffsR_[0] = hpCoeffs_[0]; hpCoeffsR_[1] = hpCoeffs_[1];
						lpCoeffsR_[0] = lpCoeffs_[0]; lpCoeffsR_[1] = lpCoeffs_[1];
						hpCoeffs_[0] = hpL0; hpCoeffs_[1] = hpL1;
						lpCoeffs_[0] = lpL0; lpCoeffs_[1] = lpL1;
					}
					else
					{
						hpCoeffsR_[0] = hpCoeffs_[0]; hpCoeffsR_[1] = hpCoeffs_[1];
						lpCoeffsR_[0] = lpCoeffs_[0]; lpCoeffsR_[1] = lpCoeffs_[1];
					}

					smoothedFilterHpFreq_ = sHp;
					smoothedFilterLpFreq_ = sLp;
				}
				else
				{
					updateFilterCoeffs (false, false);
					hpCoeffsR_[0] = hpCoeffs_[0]; hpCoeffsR_[1] = hpCoeffs_[1];
					lpCoeffsR_[0] = lpCoeffs_[0]; lpCoeffsR_[1] = lpCoeffs_[1];
				}
			}

			if (hpOn)
			{
				for (int s = 0; s < numSections_hp; ++s)
				{
					mInL = processWetBiquad (hpCoeffs_[s], wetFilterState_[0].hp[s], mInL);
					mInR = processWetBiquad (hpCoeffsR_[s], wetFilterState_[1].hp[s], mInR);
				}
			}
			if (lpOn)
			{
				for (int s = 0; s < numSections_lp; ++s)
				{
					mInL = processWetBiquad (lpCoeffs_[s], wetFilterState_[0].lp[s], mInL);
					mInR = processWetBiquad (lpCoeffsR_[s], wetFilterState_[1].lp[s], mInR);
				}
			}
		}

		// ── Tilt EQ PRE (before effect) ──
		if (tiltPre_)
		{
			smoothedTiltDb_ = paramCoeff * smoothedTiltDb_ + (1.0f - paramCoeff) * tiltTarget;
			--tiltCoeffCountdown_;
			if (tiltCoeffCountdown_ <= 0)
			{
				tiltCoeffCountdown_ = kTiltCoeffUpdateInterval;
				if (std::abs (smoothedTiltDb_ - tiltDb_) > 0.01f)
				{
					tiltDb_ = smoothedTiltDb_;
					computeTiltCoeffs (tiltDb_, 1000.0f, (float) currentSampleRate);
				}
			}
			{
				const float tL = tiltB0_ * mInL + tiltStateL_;
				tiltStateL_ = tiltB1_ * mInL - tiltA1_ * tL;
				mInL = tL;

				const float tR = tiltB0_ * mInR + tiltStateR_;
				tiltStateR_ = tiltB1_ * mInR - tiltA1_ * tR;
				mInR = tR;
			}
		}

		// Inject feedback pre-Hilbert (creative comb effect)
		const float fbInL = mInL + effectiveFb * fbSrcL;
		const float fbInR = mInR + effectiveFb * fbSrcR;

		// Write into circular Hilbert input buffer
		hilbertBufL[(size_t) hilbertPos] = fbInL;
		hilbertBufR[(size_t) hilbertPos] = fbInR;
		const auto freqShiftIirL = freqShiftHilbertIir_[0].process (fbInL);
		const auto freqShiftIirR = freqShiftHilbertIir_[1].process (style >= 1 ? fbInR : fbInL);

		// Harmonic oscillator pair (fullness/slope profile, RMS-normalized)
		const auto harmPairL = fastHarmonicOscPair ((float) oscPhase, smoothedHarm, jitteredFreqL);
		const auto harmPairR = fastHarmonicOscPair ((float) oscPhaseR, smoothedHarm, jitteredFreqR);

		const float oscWave     = harmPairL.sine;
		const float oscWaveCos  = harmPairL.cosine;
		const float oscWaveR    = harmPairR.sine;
		const float oscWaveCosR = harmPairR.cosine;

		// ── Engine blend: AM (0) -> RM (0.5) -> Freq Shift (1) ──
		const bool useStereoInput = (style >= 1);

		struct WetPair { float l = 0.0f, r = 0.0f; };

		auto makeWindowWet = [&] (int window, int renderMaxWindow) -> WetPair
		{
			const int clampedWindow = juce::jmin (getCanonicalHilbertWindow (window),
				getCanonicalHilbertWindow (renderMaxWindow));
			const int renderMaxDelay = getHilbertDelayForWindow (renderMaxWindow);
			const int lane = getHilbertWindowLane (clampedWindow);
			const int firLength = clampedWindow - 1;
			const int half = firLength / 2;
			const auto& taps = hilbertFoldedTapsByWindow_[(size_t) lane];

			float hilbL = 0.0f, hilbR = 0.0f;
			float sidechainHilbL = 0.0f, sidechainHilbR = 0.0f;
			for (const auto& tap : taps)
			{
				const int i1 = (hilbertPos - tap.offset + order) & orderMask;
				const int i2 = (hilbertPos - (firLength - 1 - tap.offset) + order) & orderMask;
				const float diffL = hilbertBufL[(size_t) i1] - hilbertBufL[(size_t) i2];
				const float diffR = hilbertBufR[(size_t) i1] - hilbertBufR[(size_t) i2];
				hilbL += tap.coeff * diffL;
				hilbR += tap.coeff * diffR;
				const float scDiffL = sidechainHilbertBufL[(size_t) i1] - sidechainHilbertBufL[(size_t) i2];
				const float scDiffR = sidechainHilbertBufR[(size_t) i1] - sidechainHilbertBufR[(size_t) i2];
				sidechainHilbL += tap.coeff * scDiffL;
				sidechainHilbR += tap.coeff * scDiffR;
			}

			const int delayIdx = (hilbertPos - half + order) & orderMask;
			const float realL = hilbertBufL[(size_t) delayIdx];
			const float realR = hilbertBufR[(size_t) delayIdx];
			const float sidechainRealL = sidechainHilbertBufL[(size_t) delayIdx];
			const float sidechainRealR = sidechainHilbertBufR[(size_t) delayIdx];

			float coreL, coreR;
			if (sidechainEnabled && (sidechainGateSmoothed_ <= 0.0001f || sidechainPolarityAbs <= 0.0001f))
			{
				coreL = realL;
				coreR = useStereoInput ? realR : realL;
			}
			else
			{
				const auto amEnvelope = [] (float carrier, float depth) noexcept
				{
					const float unipolar = 0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, carrier);
					return 1.0f + juce::jlimit (0.0f, 1.0f, depth) * (unipolar - 1.0f);
				};
				const auto normaliseCarrier = [] (float real, float imag) noexcept
				{
					const float magSq = real * real + imag * imag;
					if (magSq <= 1.0e-8f)
						return std::pair<float, float> { 0.0f, 0.0f };

					const float invMag = 1.0f / std::sqrt (magSq);
					return std::pair<float, float> {
						juce::jlimit (-1.0f, 1.0f, real * invMag),
						juce::jlimit (-1.0f, 1.0f, imag * invMag)
					};
				};

				const auto sidechainNormL = normaliseCarrier (sidechainRealL, sidechainHilbL);
				const auto sidechainNormR = normaliseCarrier (sidechainRealR, sidechainHilbR);
				const float sidechainNormSignedCarrierL = sidechainNormL.first * sidechainPolaritySign;
				const float sidechainNormSignedCarrierR = sidechainNormR.first * sidechainPolaritySign;
				const float sidechainDepth = sidechainEnabled
					? juce::jlimit (0.0f, 1.0f, sidechainDepthSmoothed_)
					: 1.0f;
				const float carrierWaveL = sidechainEnabled ? sidechainNormSignedCarrierL : oscWave;
				const float carrierWaveR = sidechainEnabled
					? (useStereoInput
						? (style == 2 ? -sidechainNormSignedCarrierL
						  : (style == 3 ? sidechainNormSignedCarrierR
						                : sidechainNormSignedCarrierR))
						: sidechainNormSignedCarrierL)
					: (useStereoInput
						? (style == 2 ? -oscWave
						  : (style == 3 ? oscWaveR
						                : oscWave))
						: oscWave);

				const float rmCarrierWaveL = sidechainEnabled ? sidechainNormSignedCarrierL : oscWave;
				const float rmCarrierWaveR = sidechainEnabled
					? (useStereoInput
						? (style == 2 ? -sidechainNormSignedCarrierL
						  : (style == 3 ? sidechainNormSignedCarrierR
						                : sidechainNormSignedCarrierR))
						: sidechainNormSignedCarrierL)
					: (useStereoInput
						? (style == 2 ? -oscWave
						  : (style == 3 ? oscWaveR
						                : oscWave))
						: oscWave);

				const float fsCosL = sidechainEnabled ? sidechainNormL.first : oscWaveCos;
				const float fsSinL = sidechainEnabled ? sidechainNormL.second * sidechainPolaritySign : oscWave;
				const float fsCosR = sidechainEnabled
					? (style == 3 ? sidechainNormR.first : sidechainNormL.first)
					: (style == 3 ? oscWaveCosR : oscWaveCos);
				const float fsSinR = sidechainEnabled
					? ((style == 3 ? sidechainNormR.second : sidechainNormL.second) * sidechainPolaritySign)
					: (style == 3 ? oscWaveR : oscWave);

				const float amL = realL * amEnvelope (carrierWaveL, sidechainDepth);
				const float amR = useStereoInput ? (realR * amEnvelope (carrierWaveR, sidechainDepth)) : amL;
				const float rmL = realL * rmCarrierWaveL;
				const float rmR = useStereoInput ? (realR * rmCarrierWaveR) : rmL;

				const float fsRealL = sidechainEnabled ? realL : freqShiftIirL.first;
				const float fsImagL = sidechainEnabled ? hilbL : freqShiftIirL.second;
				const float fsRealR = sidechainEnabled ? realR : freqShiftIirR.first;
				const float fsImagR = sidechainEnabled ? hilbR : freqShiftIirR.second;
				const float fsL = fsRealL * fsCosL - fsImagL * fsSinL;
				const float fsR = useStereoInput
					? (style == 2 ? (fsRealR * fsCosR + fsImagR * fsSinR)
					  : (fsRealR * fsCosR - fsImagR * fsSinR))
					: fsL;

				const float enginePos = juce::jlimit (0.0f, 1.0f, smoothedEngine);
				if (enginePos < 0.5f)
				{
					const float t = enginePos * 2.0f;
					coreL = amL + t * (rmL - amL);
					coreR = amR + t * (rmR - amR);
				}
				else
				{
					const float t = (enginePos - 0.5f) * 2.0f;
					coreL = rmL + t * (fsL - rmL);
					coreR = rmR + t * (fsR - rmR);
				}

				if (sidechainEnabled)
				{
					const float rmFsWeight = enginePos < 0.5f ? enginePos * 2.0f : 1.0f;
					const float scMixDepth = 1.0f + rmFsWeight * (sidechainDepth - 1.0f);
					const float scMix = juce::jlimit (0.0f, 1.0f,
						sidechainGateSmoothed_ * sidechainPolarityAbs * scMixDepth);
					const float dryCoreR = useStereoInput ? realR : realL;
					coreL = realL + scMix * (coreL - realL);
					coreR = dryCoreR + scMix * (coreR - dryCoreR);
				}
			}

			const int pad = juce::jmax (0, renderMaxDelay - half);
			hilbertWetCompBuf_[lane][0][hilbertPos] = coreL;
			hilbertWetCompBuf_[lane][1][hilbertPos] = coreR;
			const int compIdx = (hilbertPos - pad + order) & orderMask;
			return { hilbertWetCompBuf_[lane][0][compIdx],
			         hilbertWetCompBuf_[lane][1][compIdx] };
		};

		WetPair wetPair;
		const bool hilbertWindowCrossfadeActive = hilbertWindowCrossfadeRemaining_ > 0
			&& hilbertWindowCrossfadeTotal_ > 0;
		const float hilbertWindowCrossfadeProgress = hilbertWindowCrossfadeActive
			? (1.0f - ((float) hilbertWindowCrossfadeRemaining_
			           / (float) hilbertWindowCrossfadeTotal_))
			: 1.0f;
		const float hilbertWindowOldMix = hilbertWindowCrossfadeActive
			? std::cos (hilbertWindowCrossfadeProgress * juce::MathConstants<float>::halfPi)
			: 0.0f;
		const float hilbertWindowNewMix = hilbertWindowCrossfadeActive
			? std::sin (hilbertWindowCrossfadeProgress * juce::MathConstants<float>::halfPi)
			: 1.0f;
		const int previousHilbertWindowForCrossfade = previousHilbertWindow_;
		const int targetHilbertWindowForCrossfade = targetHilbertWindow_;
		const int previousMaxHilbertWindowForCrossfade = previousMaxHilbertWindow_;
		const int targetMaxHilbertWindowForCrossfade = targetMaxHilbertWindow_;

		const bool needsDualWindowRender = smoothedEngine > 0.5f
			|| engine > 0.5f
			|| hilbertWindowCrossfadeActive;

		if (needsDualWindowRender)
		{
			auto renderWetByWindow = [&] (int renderMaxWindow)
			{
				std::array<WetPair, kNumHilbertWindows> wetByWindow {};
				const int maxWindowLane = getHilbertWindowLane (renderMaxWindow);
				for (int lane = 0; lane <= maxWindowLane; ++lane)
					wetByWindow[(size_t) lane] = makeWindowWet (kHilbertWindows[lane], renderMaxWindow);
				for (int lane = maxWindowLane + 1; lane < kNumHilbertWindows; ++lane)
					wetByWindow[(size_t) lane] = wetByWindow[(size_t) maxWindowLane];
				return wetByWindow;
			};

			auto selectWet = [] (const std::array<WetPair, kNumHilbertWindows>& wetByWindow,
			                     int window) noexcept -> WetPair
			{
				return wetByWindow[(size_t) FREQTRAudioProcessor::getHilbertWindowLane (window)];
			};

			if (hilbertWindowCrossfadeActive)
			{
				const auto oldWetByWindow = renderWetByWindow (previousMaxHilbertWindowForCrossfade);
				const auto newWetByWindow = renderWetByWindow (targetMaxHilbertWindowForCrossfade);
				const WetPair oldWet = selectWet (oldWetByWindow, previousHilbertWindowForCrossfade);
				const WetPair newWet = selectWet (newWetByWindow, targetHilbertWindowForCrossfade);
				wetPair.l = oldWet.l * hilbertWindowOldMix + newWet.l * hilbertWindowNewMix;
				wetPair.r = oldWet.r * hilbertWindowOldMix + newWet.r * hilbertWindowNewMix;

				--hilbertWindowCrossfadeRemaining_;
				if (hilbertWindowCrossfadeRemaining_ <= 0)
				{
					hilbertWindowCrossfadeRemaining_ = 0;
					hilbertWindowCrossfadeTotal_ = 0;
					activeHilbertWindow_ = targetHilbertWindow_;
					activeMaxHilbertWindow_ = targetMaxHilbertWindow_;
					previousHilbertWindow_ = activeHilbertWindow_;
					previousMaxHilbertWindow_ = activeMaxHilbertWindow_;
				}
			}
			else
			{
				if (activeHilbertWindow_ != targetHilbertWindow_)
					activeHilbertWindow_ = targetHilbertWindow_;
				if (activeMaxHilbertWindow_ != targetMaxHilbertWindow_)
					activeMaxHilbertWindow_ = targetMaxHilbertWindow_;
				const auto wetByWindow = renderWetByWindow (activeMaxHilbertWindow_);
				wetPair = selectWet (wetByWindow, activeHilbertWindow_);
			}
		}
		else
		{
			if (activeHilbertWindow_ != targetHilbertWindow_)
				activeHilbertWindow_ = targetHilbertWindow_;
			if (activeMaxHilbertWindow_ != targetMaxHilbertWindow_)
				activeMaxHilbertWindow_ = targetMaxHilbertWindow_;
			wetPair = makeWindowWet (activeHilbertWindow_, activeMaxHilbertWindow_);
		}

		float wetL = wetPair.l;
		float wetR = wetPair.r;

		// Write conditioned wet into feedback delay line
		{
			const float condL = juce::jlimit (-4.0f, 4.0f,
				dcBlockTick (biquadTick (wetL, fbkLpStateL, fbkLpCoeffs), fbkDcStateInL, fbkDcStateOutL, fbkDcCoeff));
			const float condR = juce::jlimit (-4.0f, 4.0f,
				dcBlockTick (biquadTick (wetR, fbkLpStateR, fbkLpCoeffs), fbkDcStateInR, fbkDcStateOutR, fbkDcCoeff));
			fbkDelayBufL[(size_t) fbkDelayWritePos] = condL;
			fbkDelayBufR[(size_t) fbkDelayWritePos] = condR;
			fbkDelayWritePos = (fbkDelayWritePos + 1) % fbkDelaySize;
		}

		// ── Advance chaos S&H ──
		if (chaosDelayEnabled_) advanceChaosD();
		if (chaosFilterEnabled_) advanceChaosF();

		// ── Wet filter (HP + LP) ── POST position
		if (! filterPre_ && (hpOn || lpOn))
		{
			smoothedFilterHpFreq_ = smoothedFilterHpFreq_ * kGainSmoothCoeff
				+ targetHpFreq * (1.0f - kGainSmoothCoeff);
			smoothedFilterLpFreq_ = smoothedFilterLpFreq_ * kGainSmoothCoeff
				+ targetLpFreq * (1.0f - kGainSmoothCoeff);

			--filterCoeffCountdown_;
			if (filterCoeffCountdown_ <= 0)
			{
				filterCoeffCountdown_ = kFilterCoeffUpdateInterval;
				if (chaosFilterEnabled_
					&& (chaosAmtF_ > 0.01f || (chaosFilterParamSmoothReady_ && chaosFilterAmtSmoothed_ > 0.01f)))
				{
					const float sHp = smoothedFilterHpFreq_;
					const float sLp = smoothedFilterLpFreq_;

					// L channel coefficients
					const float octL = chaosFOut_[0] * smoothedChaosFilterMaxOct_;
					const float multL = std::exp2 (octL);
					smoothedFilterHpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax,
						(hpOn ? sHp : kFilterFreqMin) * multL);
					smoothedFilterLpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax,
						(lpOn ? sLp : kFilterFreqMax) * multL);
					updateFilterCoeffs (true, true);

					if (chaosStereo_)
					{
						auto hpL0 = hpCoeffs_[0]; auto hpL1 = hpCoeffs_[1];
						auto lpL0 = lpCoeffs_[0]; auto lpL1 = lpCoeffs_[1];

						const float octR = chaosFOut_[1] * smoothedChaosFilterMaxOct_;
						const float multR = std::exp2 (octR);
						smoothedFilterHpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax,
							(hpOn ? sHp : kFilterFreqMin) * multR);
						smoothedFilterLpFreq_ = juce::jlimit (kFilterFreqMin, kFilterFreqMax,
							(lpOn ? sLp : kFilterFreqMax) * multR);
						updateFilterCoeffs (true, true);

						hpCoeffsR_[0] = hpCoeffs_[0]; hpCoeffsR_[1] = hpCoeffs_[1];
						lpCoeffsR_[0] = lpCoeffs_[0]; lpCoeffsR_[1] = lpCoeffs_[1];
						hpCoeffs_[0] = hpL0; hpCoeffs_[1] = hpL1;
						lpCoeffs_[0] = lpL0; lpCoeffs_[1] = lpL1;
					}
					else
					{
						hpCoeffsR_[0] = hpCoeffs_[0]; hpCoeffsR_[1] = hpCoeffs_[1];
						lpCoeffsR_[0] = lpCoeffs_[0]; lpCoeffsR_[1] = lpCoeffs_[1];
					}

					smoothedFilterHpFreq_ = sHp;
					smoothedFilterLpFreq_ = sLp;
				}
				else
				{
					updateFilterCoeffs (false, false);
					hpCoeffsR_[0] = hpCoeffs_[0]; hpCoeffsR_[1] = hpCoeffs_[1];
					lpCoeffsR_[0] = lpCoeffs_[0]; lpCoeffsR_[1] = lpCoeffs_[1];
				}
			}

			if (hpOn)
			{
				for (int s = 0; s < numSections_hp; ++s)
				{
					wetL = processWetBiquad (hpCoeffs_[s], wetFilterState_[0].hp[s], wetL);
					wetR = processWetBiquad (hpCoeffsR_[s], wetFilterState_[1].hp[s], wetR);
				}
			}
			if (lpOn)
			{
				for (int s = 0; s < numSections_lp; ++s)
				{
					wetL = processWetBiquad (lpCoeffs_[s], wetFilterState_[0].lp[s], wetL);
					wetR = processWetBiquad (lpCoeffsR_[s], wetFilterState_[1].lp[s], wetR);
				}
			}
		}

		// ── Tilt EQ POST (after effect) ──
		if (!tiltPre_)
		{
			smoothedTiltDb_ = paramCoeff * smoothedTiltDb_ + (1.0f - paramCoeff) * tiltTarget;
			--tiltCoeffCountdown_;
			if (tiltCoeffCountdown_ <= 0)
			{
				tiltCoeffCountdown_ = kTiltCoeffUpdateInterval;
				if (std::abs (smoothedTiltDb_ - tiltDb_) > 0.01f)
				{
					tiltDb_ = smoothedTiltDb_;
					computeTiltCoeffs (tiltDb_, 1000.0f, (float) currentSampleRate);
				}
			}
			{
				const float tL = tiltB0_ * wetL + tiltStateL_;
				tiltStateL_ = tiltB1_ * wetL - tiltA1_ * tL;
				wetL = tL;

				const float tR = tiltB0_ * wetR + tiltStateR_;
				tiltStateR_ = tiltB1_ * wetR - tiltA1_ * tR;
				wetR = tR;
			}
		}

		// ── Chaos D (micro-delay + gain modulation) ──
		if (chaosDelayEnabled_)
		{
			applyChaosDelay (wetL, wetR);
		}

		// Mode Out: MID stays dual-mono, SIDE becomes true stereo (+S / -S)
		if (numChannels >= 2 && modeOutVal != 0)
		{
			const float mono = (wetL + wetR) * 0.5f;
			if (modeOutVal == 1)
			{
				wetL = mono;
				wetR = mono;
			}
			else /* modeOutVal == 2 */
			{
				wetL = mono;
				wetR = -mono;
			}
		}

		// ── Mix dry/wet with Sum Bus routing ──
		// Use clean delay buffer for dry reference (feedback-free)
		auto readCleanDry = [&] (int renderMaxWindow) noexcept -> WetPair
		{
			const int delay = getHilbertDelayForWindow (renderMaxWindow);
			const int dryDelayIdx = (hilbertPos - delay + order) & orderMask;
			return { cleanDelayBufL[(size_t) dryDelayIdx],
			         cleanDelayBufR[(size_t) dryDelayIdx] };
		};
		WetPair cleanDry;
		if (hilbertWindowCrossfadeActive)
		{
			const WetPair oldDry = readCleanDry (previousMaxHilbertWindowForCrossfade);
			const WetPair newDry = readCleanDry (targetMaxHilbertWindowForCrossfade);
			cleanDry.l = oldDry.l * hilbertWindowOldMix + newDry.l * hilbertWindowNewMix;
			cleanDry.r = oldDry.r * hilbertWindowOldMix + newDry.r * hilbertWindowNewMix;
		}
		else
		{
			cleanDry = readCleanDry (activeMaxHilbertWindow_);
		}
		const float dryRefL = alignEnabled ? cleanDry.l : inL;
		const float dryRefR = alignEnabled ? cleanDry.r : inR;
		float wL = wetL * smoothedInputGain * smoothedOutputGain;
		float wR = wetR * smoothedInputGain * smoothedOutputGain;
		if (limMode == 1)
			applyLimiterSample (wL, wR, smoothedLimThreshold);

		// Invert Polarity / Stereo (WET mode: after Limiter WET)
		if (invPol == 1) { wL = -wL; wR = -wR; }
		if (invStr == 1 && numChannels >= 2) std::swap (wL, wR);

		float dL, dR;
		if (mixMode == 1) // SEND: independent dry/wet levels
		{
			wL *= smoothedWetLevel;
			wR *= smoothedWetLevel;
			dL = dryRefL * smoothedDryLevel;
			dR = dryRefR * smoothedDryLevel;
		}
		else // INSERT: crossfade
		{
			wL *= smoothedMix;
			wR *= smoothedMix;
			dL = dryRefL * (1.0f - smoothedMix);
			dR = dryRefR * (1.0f - smoothedMix);
		}

		float outL, outR;
		if (sumBusVal == 0) // ST: normal stereo
		{
			outL = dL + wL;
			outR = dR + wR;
		}
		else if (sumBusVal == 1) // →M: wet collapsed to mono mid
		{
			const float midBus = (wL + wR) * 0.5f;
			outL = dL + midBus;
			outR = dR + midBus;
		}
		else // →S: wet collapsed to side
		{
			const float sideBus = (wL - wR) * 0.5f;
			outL = dL + sideBus;
			outR = dR - sideBus;
		}

		if (numChannels >= 2 && std::abs (smoothedPan - 0.5f) > 0.001f)
		{
			const float panPhase = smoothedPan * 0.25f;
			outL *= fastCos (panPhase);
			outR *= fastSin (panPhase);
		}

		if (limMode == 2)
		{
			if (numChannels >= 2)
				applyLimiterSample (outL, outR, smoothedLimThreshold);
			else
			{
				float dummy = 0.0f;
				applyLimiterSample (outL, dummy, smoothedLimThreshold);
			}
		}

		if (writeL != nullptr) writeL[n] = outL;
		if (writeR != nullptr) writeR[n] = outR;

		// Advance oscillator phase
		if (useSyncRetrigPhase)
		{
			syncPhaseAccumL += (double) jitteredFreqL * (double) invSr;
			syncPhaseAccumL -= std::floor (syncPhaseAccumL);
			oscPhase = syncPhaseAccumL;

			syncPhaseAccumR += (double) jitteredFreqR * (double) invSr;
			syncPhaseAccumR -= std::floor (syncPhaseAccumR);
			oscPhaseR = syncPhaseAccumR;
		}
		else
		{
			oscPhase += (double) jitteredFreqL * (double) invSr;
			oscPhase -= std::floor (oscPhase);

			oscPhaseR += (double) jitteredFreqR * (double) invSr;
			oscPhaseR -= std::floor (oscPhaseR);
		}

		hilbertPos = (hilbertPos + 1) & orderMask;
	}

	// ── Invert Polarity / Stereo (GLOBAL mode: after Limiter GLOBAL, before safety clip) ──
	{
		const int nc = numChannels;
		if (invPol == 2)
			for (int ch = 0; ch < nc; ++ch)
				juce::FloatVectorOperations::multiply (buffer.getWritePointer (ch), -1.0f, numSamples);
		if (invStr == 2 && nc >= 2)
		{
			float* sL = buffer.getWritePointer (0);
			float* sR = buffer.getWritePointer (1);
			for (int n = 0; n < numSamples; ++n)
				std::swap (sL[n], sR[n]);
		}
	}

	// ── Safety limiter: +48 dBFS — only catches runaways ──
	constexpr float kSafetyLimit = 251.19f;
	for (int ch = 0; ch < numChannels; ++ch)
	{
		auto* data = buffer.getWritePointer (ch);
		juce::FloatVectorOperations::clip (data, data, -kSafetyLimit, kSafetyLimit, numSamples);
	}
}

//==============================================================================
bool FREQTRAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* FREQTRAudioProcessor::createEditor()
{
	return new FREQTRAudioProcessorEditor (*this);
}

//==============================================================================
void FREQTRAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
	auto state = apvts.copyState();
	std::unique_ptr<juce::XmlElement> xml (state.createXml());
	copyXmlToBinary (*xml, destData);
}

void FREQTRAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
	std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

	if (xmlState.get() != nullptr)
	{
		if (xmlState->hasTagName (apvts.state.getType()))
		{
			apvts.replaceState (juce::ValueTree::fromXml (*xmlState));

			const auto restoredChannel = apvts.state.getProperty (UiStateKeys::midiPort);
			if (! restoredChannel.isVoid())
				midiChannel.store ((int) restoredChannel, std::memory_order_relaxed);
		}
	}
}

void FREQTRAudioProcessor::getCurrentProgramStateInformation (juce::MemoryBlock& destData)
{
	getStateInformation (destData);
}

void FREQTRAudioProcessor::setCurrentProgramStateInformation (const void* data, int sizeInBytes)
{
	setStateInformation (data, sizeInBytes);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FREQTRAudioProcessor::createParameterLayout()
{
	std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

	// Frequency: 0 to 5000 Hz, continuous
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFreq, "Frequency",
		juce::NormalisableRange<float> (kFreqMin, kFreqMax, 0.0f, 0.35f), kFreqDefault));

	// Frequency sync: choice parameter, 30 divisions, default index 10 ("1/8")
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamFreqSync, "Freq Sync", getFreqSyncChoices(), kFreqSyncDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMod, "Mod",
		juce::NormalisableRange<float> (kModMin, kModMax, 0.0f, 1.0f), kModDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFeedback, "Feedback",
		juce::NormalisableRange<float> (kFeedbackMin, kFeedbackMax, 0.0f, 1.0f), kFeedbackDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamJitter, "Jitter",
		juce::NormalisableRange<float> (kJitterMin, kJitterMax, 0.001f, 1.0f), kJitterDefault));
	// Comb: resonant frequency in Hz, controls the feedback delay pitch/period.
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamComb, "Comb", makeCombFrequencyRange(), kCombDefault));
	// Engine: 0 = AM, 0.5 = Ring Mod, 1 = Freq Shift (continuous blend)
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamEngine, "Engine",
		juce::NormalisableRange<float> (kEngineMin, kEngineMax, 0.0f, 1.0f), kEngineDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamWindow, "Window",
		juce::NormalisableRange<float> ((float) kHilbertWindowMin, (float) kHilbertWindowMax, 1.0f, 1.0f),
		(float) kHilbertWindowDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMaxWindow, "Max Window",
		juce::NormalisableRange<float> ((float) kHilbertWindowMin, (float) kHilbertWindowMax, 1.0f, 1.0f),
		(float) kHilbertMaxWindowDefault));

	// Style: 0 = Mono, 1 = Stereo, 2 = Wide, 3 = Dual
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamStyle, "Style",
		juce::NormalisableRange<float> ((float) kStyleMin, (float) kStyleMax, 1.0f, 1.0f), kStyleDefault));

	// HARM: 0 = sine only, 100% = FREAK-like harmonic stack allowed by Nyquist
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamHarm, "Harm",
		juce::NormalisableRange<float> (kHarmMin, kHarmMax, 0.0f, 1.0f), kHarmDefault));

	// Polarity: -1 to +1 (flips freq sign / AM wave polarity)
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamPolarity, "Polarity",
		juce::NormalisableRange<float> (kPolarityMin, kPolarityMax, 0.0f, 1.0f), kPolarityDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMix, "Mix",
		juce::NormalisableRange<float> (kMixMin, kMixMax, 0.0f, 1.0f), kMixDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamInput, "Input",
		makeGainFaderRange(), kInputDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamOutput, "Output",
		makeGainFaderRange(), kOutputDefault));

	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamSync, "Sync", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamRetrig, "Retrig", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamMidi, "MIDI", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamAlign, "Align", true));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamPdc, "PDC", true));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamSidechain, "Sidechain", false));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamSidechainTime, "Sidechain Time",
		juce::NormalisableRange<float> (kSidechainTimeMin, kSidechainTimeMax, 0.01f, 1.0f),
		kSidechainTimeDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamSidechainTone, "Sidechain Tone",
		juce::NormalisableRange<float> (kSidechainToneMin, kSidechainToneMax, 0.0f, 0.35f),
		kSidechainToneDefault));

	// Wet filter
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterHpFreq, "Filter HP Freq",
		juce::NormalisableRange<float> (kFilterFreqMin, kFilterFreqMax, 0.0f, 0.35f), kFilterHpFreqDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterLpFreq, "Filter LP Freq",
		juce::NormalisableRange<float> (kFilterFreqMin, kFilterFreqMax, 0.0f, 0.35f), kFilterLpFreqDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterHpSlope, "Filter HP Slope",
		juce::NormalisableRange<float> ((float) kFilterSlopeMin, (float) kFilterSlopeMax, 1.0f), (float) kFilterSlopeDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamFilterLpSlope, "Filter LP Slope",
		juce::NormalisableRange<float> ((float) kFilterSlopeMin, (float) kFilterSlopeMax, 1.0f), (float) kFilterSlopeDefault));

	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamFilterHpOn, "Filter HP On", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamFilterLpOn, "Filter LP On", false));

	// Tilt
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamTilt, "Tilt",
		juce::NormalisableRange<float> (kTiltMin, kTiltMax, 0.0f, 1.0f), kTiltDefault));

	// Pan
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamPan, "Pan",
		juce::NormalisableRange<float> (kPanMin, kPanMax, 0.0f), kPanDefault));

	// Chaos
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamChaos, "Chaos Filter", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamChaosD, "Chaos Delay", false));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosAmt, "Chaos Amount",
		juce::NormalisableRange<float> (kChaosAmtMin, kChaosAmtMax, 0.1f), kChaosAmtDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosSpd, "Chaos Speed",
		juce::NormalisableRange<float> (kChaosSpdMin, kChaosSpdMax, 0.01f, 0.3f), kChaosSpdDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosAmtFilter, "Chaos Amt Filter",
		juce::NormalisableRange<float> (kChaosAmtMin, kChaosAmtMax, 0.1f), kChaosAmtDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamChaosSpdFilter, "Chaos Spd Filter",
		juce::NormalisableRange<float> (kChaosSpdMin, kChaosSpdMax, 0.01f, 0.3f), kChaosSpdDefault));

	// Mode In / Mode Out / Sum Bus
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamModeIn, "Mode In", juce::StringArray { "L+R", "MID", "SIDE" }, kModeInOutDefault));
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamModeOut, "Mode Out", juce::StringArray { "L+R", "MID", "SIDE" }, kModeInOutDefault));
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamSumBus, "Sum Bus", juce::StringArray { "ST", u8"\u2192M", u8"\u2192S" }, kSumBusDefault));

	// Limiter
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamLimThreshold, "Lim Threshold",
		juce::NormalisableRange<float> (kLimThresholdMin, kLimThresholdMax, 0.1f), kLimThresholdDefault));
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamLimMode, "Lim Mode", juce::StringArray { "NONE", "WET", "GLOBAL" }, kLimModeDefault));

	// Invert Polarity / Invert Stereo
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamInvPol, "Invert Polarity",
		juce::StringArray { "NONE", "WET", "GLOBAL" }, kInvPolDefault));
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamInvStr, "Invert Stereo",
		juce::StringArray { "NONE", "WET", "GLOBAL" }, kInvStrDefault));

	// Mix Mode / Dry-Wet Levels / Filter Position
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamMixMode, "Mix Mode",
		juce::StringArray { "INSERT", "SEND" }, kMixModeDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamDryLevel, "Dry Level", 0.0f, 1.0f, kDryLevelDefault));
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamWetLevel, "Wet Level", 0.0f, 1.0f, kWetLevelDefault));
	// Filter / Tilt position (PRE / POST effect)
	params.push_back (std::make_unique<juce::AudioParameterChoice> (
		kParamFilterPos, "Filter Position",
		juce::StringArray { juce::String::fromUTF8 (u8"F\u25bc T\u25bc"),
		                    juce::String::fromUTF8 (u8"F\u25b2 T\u25b2"),
		                    juce::String::fromUTF8 (u8"F\u25b2 T\u25bc"),
		                    juce::String::fromUTF8 (u8"F\u25bc T\u25b2") },
		kFilterPosDefault));

	// UI state (hidden from automation)
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiWidth, "UI Width", 360, 1600, 360));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiHeight, "UI Height", 240, 1200, 480));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamUiPalette, "UI Palette", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamUiCrt, "UI CRT", false));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiColor0, "UI Color 0", 0, 0xFFFFFF, 0x00FF00));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiColor1, "UI Color 1", 0, 0xFFFFFF, 0x000000));

	return { params.begin(), params.end() };
}

//==============================================================================
// UI state management

void FREQTRAudioProcessor::setUiEditorSize (int width, int height)
{
	const int w = juce::jlimit (360, 1600, width);
	const int h = juce::jlimit (240, 1200, height);
	uiEditorWidth.store (w, std::memory_order_relaxed);
	uiEditorHeight.store (h, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::editorWidth, w, nullptr);
	apvts.state.setProperty (UiStateKeys::editorHeight, h, nullptr);
	setParameterPlainValue (apvts, kParamUiWidth, (float) w);
	setParameterPlainValue (apvts, kParamUiHeight, (float) h);
	updateHostDisplay();
}

int FREQTRAudioProcessor::getUiEditorWidth() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::editorWidth);
	if (! fromState.isVoid()) return (int) fromState;
	if (uiWidthParam != nullptr)
		return (int) std::lround (uiWidthParam->load (std::memory_order_relaxed));
	return uiEditorWidth.load (std::memory_order_relaxed);
}

int FREQTRAudioProcessor::getUiEditorHeight() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::editorHeight);
	if (! fromState.isVoid()) return (int) fromState;
	if (uiHeightParam != nullptr)
		return (int) std::lround (uiHeightParam->load (std::memory_order_relaxed));
	return uiEditorHeight.load (std::memory_order_relaxed);
}

void FREQTRAudioProcessor::setUiUseCustomPalette (bool shouldUseCustomPalette)
{
	uiUseCustomPalette.store (shouldUseCustomPalette ? 1 : 0, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::useCustomPalette, shouldUseCustomPalette, nullptr);
	setParameterPlainValue (apvts, kParamUiPalette, shouldUseCustomPalette ? 1.0f : 0.0f);
	updateHostDisplay();
}

bool FREQTRAudioProcessor::getUiUseCustomPalette() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::useCustomPalette);
	if (! fromState.isVoid()) return (bool) fromState;
	return uiUseCustomPalette.load (std::memory_order_relaxed) != 0;
}

void FREQTRAudioProcessor::setUiCrtEnabled (bool enabled)
{
	uiCrtEnabled.store (enabled ? 1 : 0, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::crtEnabled, enabled, nullptr);
	setParameterPlainValue (apvts, kParamUiCrt, enabled ? 1.0f : 0.0f);
	updateHostDisplay();
}

bool FREQTRAudioProcessor::getUiCrtEnabled() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::crtEnabled);
	if (! fromState.isVoid()) return (bool) fromState;
	return uiCrtEnabled.load (std::memory_order_relaxed) != 0;
}

void FREQTRAudioProcessor::setUiIoExpanded (bool expanded)
{
	apvts.state.setProperty (UiStateKeys::ioExpanded, expanded, nullptr);
}

bool FREQTRAudioProcessor::getUiIoExpanded() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::ioExpanded);
	if (! fromState.isVoid()) return (bool) fromState;
	return false;
}

void FREQTRAudioProcessor::setMidiChannel (int channel)
{
	const int ch = juce::jlimit (0, 16, channel);
	midiChannel.store (ch, std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::midiPort, ch, nullptr);
}

int FREQTRAudioProcessor::getMidiChannel() const noexcept
{
	const auto fromState = apvts.state.getProperty (UiStateKeys::midiPort);
	if (! fromState.isVoid()) return (int) fromState;
	return midiChannel.load (std::memory_order_relaxed);
}

void FREQTRAudioProcessor::setUiCustomPaletteColour (int index, juce::Colour colour)
{
	if (index < 0 || index >= 2) return;
	uiCustomPalette[(size_t) index].store (colour.getARGB(), std::memory_order_relaxed);
	apvts.state.setProperty (UiStateKeys::customPalette[(size_t) index], (int) colour.getARGB(), nullptr);

	const int rgb = (int) (colour.getARGB() & 0x00FFFFFFu);
	if (uiColorParams[(size_t) index] != nullptr)
	{
		setParameterPlainValue (apvts, (index == 0 ? kParamUiColor0 : kParamUiColor1), (float) rgb);
		updateHostDisplay();
	}
}

juce::Colour FREQTRAudioProcessor::getUiCustomPaletteColour (int index) const noexcept
{
	if (index < 0 || index >= 2) return juce::Colours::white;

	const juce::String key = UiStateKeys::customPalette[(size_t) index];
	const auto fromState = apvts.state.getProperty (key);
	if (! fromState.isVoid())
		return juce::Colour ((juce::uint32) (int) fromState);

	if (uiColorParams[(size_t) index] != nullptr)
	{
		const int rgb = juce::jlimit (0, 0xFFFFFF,
									  (int) std::lround (uiColorParams[(size_t) index]->load (std::memory_order_relaxed)));
		const juce::uint8 r = (juce::uint8) ((rgb >> 16) & 0xFF);
		const juce::uint8 gv = (juce::uint8) ((rgb >> 8) & 0xFF);
		const juce::uint8 b = (juce::uint8) (rgb & 0xFF);
		return juce::Colour::fromRGB (r, gv, b);
	}

	return juce::Colour (uiCustomPalette[(size_t) index].load (std::memory_order_relaxed));
}

//==============================================================================
// MIDI helpers

juce::String FREQTRAudioProcessor::getMidiNoteName (int midiNote)
{
	if (midiNote < 0 || midiNote > 127) return "";

	const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	const int octave = (midiNote / 12) - 1;
	const int noteIndex = midiNote % 12;

	return juce::String (noteNames[noteIndex]) + juce::String (octave);
}

juce::String FREQTRAudioProcessor::getCurrentFreqDisplay() const
{
	const bool midiEnabled = loadBoolParamOrDefault (midiParam, false);
	const int midiNote = lastMidiNote.load (std::memory_order_relaxed);

	if (midiEnabled && midiNote >= 0)
		return getMidiNoteName (midiNote);

	return "";
}

//==============================================================================
// Sync helpers

juce::StringArray FREQTRAudioProcessor::getFreqSyncChoices()
{
	return {
		"1/64T", "1/64", "1/64.",
		"1/32T", "1/32", "1/32.",
		"1/16T", "1/16", "1/16.",
		"1/8T",  "1/8",  "1/8.",
		"1/4T",  "1/4",  "1/4.",
		"1/2T",  "1/2",  "1/2.",
		"1/1T",  "1/1",  "1/1.",
		"2/1T",  "2/1",  "2/1.",
		"4/1T",  "4/1",  "4/1.",
		"8/1T",  "8/1",  "8/1."
	};
}

juce::String FREQTRAudioProcessor::getFreqSyncName (int index)
{
	auto choices = getFreqSyncChoices();
	if (index >= 0 && index < choices.size())
		return choices[index];
	return "1/8";
}

float FREQTRAudioProcessor::tempoSyncToHz (int syncIndex, double bpm) const
{
	if (bpm <= 0.0)
		bpm = 120.0;

	syncIndex = juce::jlimit (0, 29, syncIndex);

	// divisions[i] = how many of this note fit in a whole note
	const float divisions[] = { 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };
	const int baseIndex = syncIndex / 3;
	const int modifier  = syncIndex % 3;

	const float quarterNoteMs = (float) (60000.0 / bpm);
	float durationMs = quarterNoteMs * (4.0f / divisions[baseIndex]);

	if (modifier == 0)       // triplet
		durationMs *= (2.0f / 3.0f);
	else if (modifier == 2)  // dotted
		durationMs *= 1.5f;

	// Convert period → frequency: Hz = 1000 / ms
	return (durationMs > 0.001f) ? (1000.0f / durationMs) : 0.0f;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new FREQTRAudioProcessor();
}
