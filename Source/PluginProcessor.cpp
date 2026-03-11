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
	inline float waveSine (float phase) noexcept
	{
		return std::sin (phase * juce::MathConstants<float>::twoPi);
	}

	inline float waveTriangle (float phase) noexcept
	{
		return 4.0f * std::abs (phase - 0.5f) - 1.0f;
	}

	inline float waveSquare (float phase) noexcept
	{
		return phase < 0.5f ? 1.0f : -1.0f;
	}

	inline float waveSaw (float phase) noexcept
	{
		return 2.0f * phase - 1.0f;
	}

	// Morph between 4 waveforms via shape parameter (0..1)
	// 0 = Sine, 0.333 = Triangle, 0.666 = Square, 1.0 = Saw
	inline float morphedWave (float phase, float shape) noexcept
	{
		const float scaled = shape * 3.0f;
		const int segment = juce::jlimit (0, 2, (int) scaled);
		const float frac = scaled - (float) segment;

		float a, b;
		switch (segment)
		{
			case 0:  a = waveSine (phase);     b = waveTriangle (phase); break;
			case 1:  a = waveTriangle (phase); b = waveSquare (phase);   break;
			case 2:  a = waveSquare (phase);   b = waveSaw (phase);      break;
			default: a = waveSine (phase);     b = waveSine (phase);     break;
		}

		return a + frac * (b - a);
	}

	// ── Hilbert FIR coefficient generation ──
	// Windowed-sinc design: h[n] = (2 / (n*pi)) * sin^2(n*pi/2)  for odd n, 0 for even
	// Windowed with Blackman to suppress sidelobes.
	inline void designHilbertFIR (float* coeffs, int order)
	{
		const int M = order;
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
}

//==============================================================================
FREQTRAudioProcessor::FREQTRAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
	: AudioProcessor (BusesProperties()
					 #if ! JucePlugin_IsMidiEffect
					  #if ! JucePlugin_IsSynth
					   .withInput  ("Input", juce::AudioChannelSet::stereo(), true)
					  #endif
					   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
					 #endif
					   )
#endif
	, apvts (*this, nullptr, "Parameters", createParameterLayout())
{
	freqParam    = apvts.getRawParameterValue (kParamFreq);
	modParam     = apvts.getRawParameterValue (kParamMod);
	engineParam  = apvts.getRawParameterValue (kParamEngine);
	styleParam   = apvts.getRawParameterValue (kParamStyle);
	shapeParam   = apvts.getRawParameterValue (kParamShape);
	polarityParam = apvts.getRawParameterValue (kParamPolarity);
	mixParam     = apvts.getRawParameterValue (kParamMix);
	inputParam   = apvts.getRawParameterValue (kParamInput);
	outputParam  = apvts.getRawParameterValue (kParamOutput);
	syncParam    = apvts.getRawParameterValue (kParamSync);
	retrigParam  = apvts.getRawParameterValue (kParamRetrig);
	midiParam    = apvts.getRawParameterValue (kParamMidi);
	alignParam   = apvts.getRawParameterValue (kParamAlign);
	pdcParam     = apvts.getRawParameterValue (kParamPdc);

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

	buildShapeGainTable();
}

void FREQTRAudioProcessor::buildShapeGainTable()
{
	constexpr int kPhaseSteps = 4096;

	for (int i = 0; i <= kShapeTableSize; ++i)
	{
		const float shape = (float) i / (float) kShapeTableSize;
		float maxAbs = 0.0f;

		for (int j = 0; j < kPhaseSteps; ++j)
		{
			const float phase = (float) j / (float) kPhaseSteps;
			const float val = std::abs (morphedWave (phase, shape));
			if (val > maxAbs)
				maxAbs = val;
		}

		shapeGainTable[(size_t) i] = (maxAbs > 0.001f) ? (1.0f / maxAbs) : 1.0f;
	}
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

	// ── Design Hilbert FIR and build folded taps ──
	{
		std::vector<float> coeffs ((size_t) kHilbertOrder, 0.0f);
		designHilbertFIR (coeffs.data(), kHilbertOrder);

		const int half = kHilbertOrder / 2;
		hilbertFoldedTaps.clear();

		// Exploit antisymmetry: h[k] = -h[N-1-k] for nonzero taps.
		// Pair taps (k, N-1-k) into a single multiply: h[k] * (x[n-k] - x[n-(N-1-k)])
		// Only odd-distance taps are nonzero, and we only need k < half.
		for (int k = 0; k < half; ++k)
		{
			if (std::abs (coeffs[(size_t) k]) < 1e-12f)
				continue;
			hilbertFoldedTaps.push_back ({ k, coeffs[(size_t) k] });
		}
	}

	// Circular buffers for FIR convolution
	hilbertBufL.assign ((size_t) kHilbertOrder, 0.0f);
	hilbertBufR.assign ((size_t) kHilbertOrder, 0.0f);

	hilbertPos = 0;
	oscPhase = 0.0;
	smoothedFreq = 0.0f;
	smoothedEngine = 0.0f;
	smoothedShape = 0.0f;
	smoothedMix = 1.0f;
	smoothedInputGain = 1.0f;
	smoothedOutputGain = 1.0f;

	// Report latency if PDC enabled
	const bool pdcEnabled = loadBoolParamOrDefault (pdcParam, true);
	setLatencySamples (pdcEnabled ? kHilbertDelay : 0);
}

void FREQTRAudioProcessor::releaseResources()
{
	hilbertBufL.clear();
	hilbertBufR.clear();
	hilbertFoldedTaps.clear();
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
   #endif

	return true;
  #endif
}
#endif

//==============================================================================
void FREQTRAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
	juce::ScopedNoDenormals noDenormals;

	const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
	const int numSamples  = buffer.getNumSamples();

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

			if (msg.isNoteOn())
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
			currentMidiFrequency.store (0.0f, std::memory_order_relaxed);
		}
	}

	for (int i = numChannels; i < buffer.getNumChannels(); ++i)
		buffer.clear (i, 0, numSamples);

	if (currentSampleRate <= 0.0 || hilbertFoldedTaps.empty())
		return;

	// ── Read parameters ──
	float targetFreq   = loadAtomicOrDefault (freqParam, kFreqDefault);
	const float modVal = loadAtomicOrDefault (modParam, kModDefault);
	const float engine = loadAtomicOrDefault (engineParam, kEngineDefault);
	const int   style  = loadIntParamOrDefault (styleParam, 0);
	const float shape  = loadAtomicOrDefault (shapeParam, kShapeDefault);
	const float polarity = loadAtomicOrDefault (polarityParam, kPolarityDefault);
	const float mix    = loadAtomicOrDefault (mixParam, kMixDefault);
	const float inputDb  = loadAtomicOrDefault (inputParam, kInputDefault);
	const float outputDb = loadAtomicOrDefault (outputParam, kOutputDefault);
	const float inputGain  = juce::Decibels::decibelsToGain (inputDb, -100.0f);
	const float outputGain = juce::Decibels::decibelsToGain (outputDb, -100.0f);
	const bool  syncEnabled  = loadBoolParamOrDefault (syncParam, false);
	const bool  alignEnabled = loadBoolParamOrDefault (alignParam, true);
	const bool  pdcEnabled   = loadBoolParamOrDefault (pdcParam, true);

	setLatencySamples (pdcEnabled ? kHilbertDelay : 0);

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
	float freqCoeff = std::exp (-1.0f / (float (currentSampleRate) * 0.005f));

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

	// EMA coefficient for mix/engine/shape (~5 ms)
	const float paramCoeff = std::exp (-1.0f / (float (currentSampleRate) * 0.005f));

	const int order = kHilbertOrder;
	const int half  = order / 2;
	const float invSr = 1.0f / (float) currentSampleRate;

	float* writeL = (numChannels > 0) ? buffer.getWritePointer (0) : nullptr;
	float* writeR = (numChannels > 1) ? buffer.getWritePointer (1) : nullptr;

	for (int n = 0; n < numSamples; ++n)
	{
		smoothedFreq = freqCoeff * smoothedFreq + (1.0f - freqCoeff) * targetFreq;
		smoothedEngine = paramCoeff * smoothedEngine + (1.0f - paramCoeff) * engine;
		smoothedShape  = paramCoeff * smoothedShape  + (1.0f - paramCoeff) * shape;
		smoothedMix    = paramCoeff * smoothedMix    + (1.0f - paramCoeff) * mix;
		smoothedInputGain  = paramCoeff * smoothedInputGain  + (1.0f - paramCoeff) * inputGain;
		smoothedOutputGain = paramCoeff * smoothedOutputGain + (1.0f - paramCoeff) * outputGain;

		// ── Phase: retrig from PPQ or free-running ──
		if (useSyncRetrigPhase)
		{
			oscPhase = syncRetrigPhase + (double) smoothedFreq * ((double) n * (double) invSr);
			oscPhase -= std::floor (oscPhase);
		}

		const float inL = (writeL != nullptr) ? writeL[n] : 0.0f;
		const float inR = (writeR != nullptr) ? writeR[n] : inL;

		// Write into circular Hilbert input buffer
		hilbertBufL[(size_t) hilbertPos] = inL;
		hilbertBufR[(size_t) hilbertPos] = inR;

		// ── Hilbert FIR convolution (90° path) — folded antisymmetric taps ──
		float hilbL = 0.0f, hilbR = 0.0f;
		for (const auto& tap : hilbertFoldedTaps)
		{
			const int i1 = (hilbertPos - tap.offset + order) & (order - 1);
			const int i2 = (hilbertPos - (order - 1 - tap.offset) + order) & (order - 1);
			const float diffL = hilbertBufL[(size_t) i1] - hilbertBufL[(size_t) i2];
			const float diffR = hilbertBufR[(size_t) i1] - hilbertBufR[(size_t) i2];
			hilbL += tap.coeff * diffL;
			hilbR += tap.coeff * diffR;
		}

		// ── Real path: delayed input (matches Hilbert group delay) ──
		const int delayIdx = (hilbertPos - half + order) & (order - 1);
		const float realL = hilbertBufL[(size_t) delayIdx];
		const float realR = hilbertBufR[(size_t) delayIdx];

		// ── Oscillator ──
		const float oscCos = std::cos ((float) oscPhase * juce::MathConstants<float>::twoPi);
		const float oscSin = std::sin ((float) oscPhase * juce::MathConstants<float>::twoPi);

		// Peak-normalization factor for current smoothed shape
		const float curShapeIdx = smoothedShape * (float) kShapeTableSize;
		const int csi0 = juce::jlimit (0, kShapeTableSize - 1, (int) curShapeIdx);
		const int csi1 = juce::jmin (csi0 + 1, kShapeTableSize);
		const float csiFrac = curShapeIdx - (float) csi0;
		const float curShapeGain = shapeGainTable[(size_t) csi0]
							  + csiFrac * (shapeGainTable[(size_t) csi1] - shapeGainTable[(size_t) csi0]);

		// Morphed waveform (for AM mode) — peak-normalized
		const float oscWave = morphedWave ((float) oscPhase, smoothedShape) * curShapeGain;

		// ── Engine blend: AM (0) ↔ Freq Shift (1) ──
		//  style: 0 = MONO (L copied to R), 1 = STEREO (same shift both), 2 = WIDE (opposite shift R)
		const bool useStereoInput = (style >= 1);   // STEREO and WIDE both use independent L/R
		float wetL, wetR;

		if (std::abs (smoothedFreq) < 0.001f)
		{
			// Bypass when frequency ≈ 0 to avoid silence from AM mode (sin(0)=0)
			wetL = realL;
			wetR = useStereoInput ? realR : realL;
		}
		else
		{
			// AM: input * wave  (ring modulation at engine=0)
			const float amL = realL * oscWave;
			const float amR = useStereoInput ? (realR * (style == 2 ? -oscWave : oscWave)) : amL;

			// Freq Shift: Re[ analytic * e^(j2πft) ] = real*cos - hilbert*sin
			// WIDE: R uses +sin (opposite sideband) → real*cos + hilbert*sin
			const float fsL = realL * oscCos - hilbL * oscSin;
			const float fsR = useStereoInput
				? (style == 2 ? (realR * oscCos + hilbR * oscSin)
				               : (realR * oscCos - hilbR * oscSin))
				: fsL;

			wetL = amL + smoothedEngine * (fsL - amL);
			wetR = amR + smoothedEngine * (fsR - amR);
		}

		// ── Mix (dry = delayed or direct depending on ALIGN) ──
		// Input/Output gain only affect WET (same as ECHO-TR/DISP-TR)
		const float dryL = alignEnabled ? realL : inL;
		const float dryR = alignEnabled ? realR : inR;
		const float wetGainedL = wetL * smoothedInputGain * smoothedOutputGain;
		const float wetGainedR = wetR * smoothedInputGain * smoothedOutputGain;
		const float outL = dryL + smoothedMix * (wetGainedL - dryL);
		const float outR = dryR + smoothedMix * (wetGainedR - dryR);

		if (writeL != nullptr) writeL[n] = outL;
		if (writeR != nullptr) writeR[n] = outR;

		// Advance oscillator phase (free-running mode only)
		if (! useSyncRetrigPhase)
		{
			oscPhase += (double) smoothedFreq * (double) invSr;
			oscPhase -= std::floor (oscPhase);
		}

		hilbertPos = (hilbertPos + 1) & (order - 1);
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

	// Engine: 0 = AM, 1 = Freq Shift (continuous blend)
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamEngine, "Engine",
		juce::NormalisableRange<float> (kEngineMin, kEngineMax, 0.0f, 1.0f), kEngineDefault));

	// Style: 0 = Mono, 1 = Stereo
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamStyle, "Style",
		juce::NormalisableRange<float> ((float) kStyleMin, (float) kStyleMax, 1.0f, 1.0f), kStyleDefault));

	// Shape: 0 = Sine, ~0.33 = Tri, ~0.66 = Square, 1 = Saw
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamShape, "Shape",
		juce::NormalisableRange<float> (kShapeMin, kShapeMax, 0.0f, 1.0f), kShapeDefault));

	// Polarity: -1 to +1 (flips freq sign / AM wave polarity)
	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamPolarity, "Polarity",
		juce::NormalisableRange<float> (kPolarityMin, kPolarityMax, 0.0f, 1.0f), kPolarityDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamMix, "Mix",
		juce::NormalisableRange<float> (kMixMin, kMixMax, 0.0f, 1.0f), kMixDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamInput, "Input",
		juce::NormalisableRange<float> (kInputMin, kInputMax, 0.0f, 2.5f), kInputDefault));

	params.push_back (std::make_unique<juce::AudioParameterFloat> (
		kParamOutput, "Output",
		juce::NormalisableRange<float> (kOutputMin, kOutputMax, 0.0f, 3.23f), kOutputDefault));

	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamSync, "Sync", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamRetrig, "Retrig", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamMidi, "MIDI", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamAlign, "Align", true));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamPdc, "PDC", true));

	// UI state (hidden from automation)
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiWidth, "UI Width", 360, 1600, 360));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiHeight, "UI Height", 240, 1200, 480));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamUiPalette, "UI Palette", false));
	params.push_back (std::make_unique<juce::AudioParameterBool> (kParamUiCrt, "UI CRT", false));
	params.push_back (std::make_unique<juce::AudioParameterInt> (kParamUiColor0, "UI Color 0", 0, 0xFFFFFF, 0xFFFFFF));
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
