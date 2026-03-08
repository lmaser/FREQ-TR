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
	static constexpr const char* kParamEngine    = "engine";
	static constexpr const char* kParamStyle     = "style";
	static constexpr const char* kParamShape     = "shape";
	static constexpr const char* kParamPolarity  = "polarity";
	static constexpr const char* kParamMix       = "mix";
	static constexpr const char* kParamSync      = "sync";
	static constexpr const char* kParamMidi      = "midi";

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

	static constexpr float kEngineMin     = 0.0f;
	static constexpr float kEngineMax     = 1.0f;
	static constexpr float kEngineDefault = 0.0f;   // 0 = AM, 1 = Freq Shift

	static constexpr int kStyleMin     = 0;
	static constexpr int kStyleMax     = 1;         // 0 = MONO, 1 = STEREO
	static constexpr float kStyleDefault = 0.0f;

	static constexpr float kShapeMin     = 0.0f;
	static constexpr float kShapeMax     = 1.0f;
	static constexpr float kShapeDefault = 0.0f;    // 0=Sine, 0.33=Tri, 0.66=Square, 1=Saw

	static constexpr float kPolarityMin     = -1.0f;
	static constexpr float kPolarityMax     =  1.0f;
	static constexpr float kPolarityDefault =  1.0f;

	static constexpr float kMixMin     = 0.0f;
	static constexpr float kMixMax     = 1.0f;
	static constexpr float kMixDefault = 1.0f;

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

	void setUiCustomPaletteColour (int index, juce::Colour colour);
	juce::Colour getUiCustomPaletteColour (int index) const noexcept;

	juce::AudioProcessorValueTreeState apvts;
	static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
	struct UiStateKeys
	{
		static constexpr const char* editorWidth = "uiEditorWidth";
		static constexpr const char* editorHeight = "uiEditorHeight";
		static constexpr const char* useCustomPalette = "uiUseCustomPalette";
		static constexpr const char* crtEnabled = "uiFxTailEnabled";
		static constexpr const char* midiPort = "midiPort";
		static constexpr std::array<const char*, 2> customPalette {
			"uiCustomPalette0", "uiCustomPalette1"
		};
	};

	double currentSampleRate = 44100.0;

	// ── Hilbert transform (FIR, linear phase) ──
	// Two FIR filters: one produces the 90° shifted version,
	// the other is a matched delay for the real (0°) path.
	// At 0 Hz shift the output equals the delayed input exactly.
	static constexpr int kHilbertOrder = 128;          // FIR taps (even)
	static constexpr int kHilbertDelay = kHilbertOrder / 2;  // group delay in samples
	std::vector<float> hilbertCoeffs;                  // 90° FIR coefficients
	std::vector<float> hilbertBufL, hilbertBufR;       // circular FIR input buffers
	std::vector<float> delayBufL, delayBufR;           // matched delay for real path
	int hilbertPos = 0;                                // write position in circular buffers
	std::vector<float> dryDelayBufL, dryDelayBufR;     // latency compensation for dry signal

	// ── Oscillator state ──
	double oscPhase = 0.0;         // 0..1 normalised phase
	float smoothedFreq = 0.0f;     // EMA-smoothed frequency target

	// ── MIDI note tracking ──
	std::atomic<float> currentMidiFrequency { 0.0f };
	std::atomic<int>   lastMidiNote { -1 };
	std::atomic<int>   lastMidiVelocity { 127 };
	std::atomic<int>   midiChannel { 0 };

	// ── Cached parameter pointers ──
	std::atomic<float>* freqParam    = nullptr;
	std::atomic<float>* modParam     = nullptr;
	std::atomic<float>* engineParam  = nullptr;
	std::atomic<float>* styleParam   = nullptr;
	std::atomic<float>* shapeParam   = nullptr;
	std::atomic<float>* polarityParam = nullptr;
	std::atomic<float>* mixParam     = nullptr;
	std::atomic<float>* syncParam    = nullptr;
	std::atomic<float>* midiParam    = nullptr;

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

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FREQTRAudioProcessor)
};
