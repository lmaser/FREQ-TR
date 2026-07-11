#pragma once

#include <cstdint>
#include <atomic>
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CrtEffect.h"
#include "../../TR-Shared/SimpleUI/TRSharedUI.h"

class FREQTRAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    public juce::SettableTooltipClient,
                                    private juce::Slider::Listener,
                                    private juce::AudioProcessorValueTreeState::Listener,
                                    private juce::Timer
{
public:
    explicit FREQTRAudioProcessorEditor (FREQTRAudioProcessor&);
    ~FREQTRAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void moved() override;
    void parentHierarchyChanged() override;

private:
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    void openNumericEntryPopupForSlider (juce::Slider& s);
    void openEngineBiasPrompt();
    void openMidiChannelPrompt();
    void openRetrigPrompt();
    void openPdcMaxWindowPrompt();
    void openSidechainPrompt();
    void openFilterPrompt();
    void openChaosConfigPrompt (const char* amtParamId, const char* spdParamId, const juce::String& title);
    void openChaosFilterPrompt();
    void openChaosDelayPrompt();
    void openMixSendPrompt();
    void openInfoPopup();
    void openGraphicsPopup();
    void setPromptOverlayActive (bool shouldBeActive);

    FREQTRAudioProcessor& audioProcessor;

    class BarSlider : public TR::SimpleBarSliderDecl
    {
    public:
        using TR::SimpleBarSliderDecl::SimpleBarSliderDecl;
        void setOwner (FREQTRAudioProcessorEditor* o) { owner = o; }

        double snapValue (double attemptedValue, DragMode dragMode) override
        {
            if (owner != nullptr && this == &owner->windowSlider)
            {
                const int window = FREQTRAudioProcessor::getCanonicalHilbertWindow (
                    (int) std::lround (attemptedValue));
                return (double) juce::jmin (window, owner->getCurrentMaxHilbertWindow());
            }

            return TR::SimpleBarSliderDecl::snapValue (attemptedValue, dragMode);
        }

        double valueToProportionOfLength (double value) override
        {
            if (owner != nullptr && this == &owner->windowSlider)
            {
                const int maxLane = FREQTRAudioProcessor::getHilbertWindowLane (owner->getCurrentMaxHilbertWindow());
                if (maxLane <= 0)
                    return 1.0;

                const int lane = juce::jlimit (0, maxLane,
                    FREQTRAudioProcessor::getHilbertWindowLane ((int) std::lround (value)));
                return (double) lane / (double) maxLane;
            }

            return TR::SimpleBarSliderDecl::valueToProportionOfLength (value);
        }

        double proportionOfLengthToValue (double proportion) override
        {
            if (owner != nullptr && this == &owner->windowSlider)
            {
                const int maxLane = FREQTRAudioProcessor::getHilbertWindowLane (owner->getCurrentMaxHilbertWindow());
                if (maxLane <= 0)
                    return (double) FREQTRAudioProcessor::kHilbertWindowMin;

                const int lane = juce::jlimit (0, maxLane,
                    (int) std::lround (juce::jlimit (0.0, 1.0, proportion) * (double) maxLane));
                return (double) FREQTRAudioProcessor::kHilbertWindows[lane];
            }

            return TR::SimpleBarSliderDecl::proportionOfLengthToValue (proportion);
        }

    private:
        FREQTRAudioProcessorEditor* owner = nullptr;
    };
    using MainGuiPromptToggleButton = TR::MainGuiPromptToggleButton;

    // 16 bars: INPUT, OUTPUT, MIX, FREQ, MOD, COMB, FEEDBACK, ENGINE, WIN, HARM, POLARITY, JIT, STYLE, TILT, PAN, LIMTHRESHOLD
    BarSlider inputSlider;
    BarSlider outputSlider;
    BarSlider mixSlider;
    BarSlider freqSlider;
    BarSlider modSlider;
    BarSlider feedbackSlider;
    BarSlider jitterSlider;
    BarSlider combSlider;
    BarSlider engineSlider;
    BarSlider windowSlider;
    BarSlider harmSlider;
    BarSlider sidechainShadowSlider;
    BarSlider polaritySlider;
    BarSlider styleSlider;
    BarSlider tiltSlider;
    BarSlider panSlider;
    BarSlider limThresholdSlider;

    using FREQScheme = TR::TRScheme;
    using FilterBarComponent = TR::SimpleFilterBarComponent<FREQTRAudioProcessorEditor, FREQTRAudioProcessor, FREQScheme>;
    FilterBarComponent filterBar_;
    using DualMixBarComponent = TR::SimpleDualMixBarComponent<FREQTRAudioProcessorEditor, FREQTRAudioProcessor, FREQScheme>;
    DualMixBarComponent dualMixBar_;

    // 2 checkboxes: SYNC, MIDI
    MainGuiPromptToggleButton syncButton;
    MainGuiPromptToggleButton midiButton;

    // 2 checkboxes: ALIGN, PDC
    MainGuiPromptToggleButton alignButton;
    MainGuiPromptToggleButton pdcButton;
    juce::Label pdcDisplay;

    MainGuiPromptToggleButton sidechainButton;
    juce::Label sidechainDisplay;

    // Chaos buttons
    MainGuiPromptToggleButton chaosFilterButton;
    MainGuiPromptToggleButton chaosDelayButton;
    juce::Label chaosFilterDisplay;
    juce::Label chaosDelayDisplay;

    juce::ComboBox modeInCombo;
    juce::ComboBox modeOutCombo;
    juce::ComboBox sumBusCombo;
    juce::ComboBox limModeCombo;
    juce::ComboBox invPolCombo;
    juce::ComboBox invStrCombo;
    juce::ComboBox mixModeCombo;
    juce::ComboBox filterPosCombo;

    juce::Label midiChannelDisplay;
    juce::Label retrigDisplay;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> freqAttachment;
    std::unique_ptr<SliderAttachment> freqSyncAttachment;
    std::unique_ptr<SliderAttachment> modAttachment;
    std::unique_ptr<SliderAttachment> feedbackAttachment;
    std::unique_ptr<SliderAttachment> jitterAttachment;
    std::unique_ptr<SliderAttachment> combAttachment;
    std::unique_ptr<SliderAttachment> engineAttachment;
    std::unique_ptr<SliderAttachment> windowAttachment;
    std::unique_ptr<SliderAttachment> styleAttachment;
    std::unique_ptr<SliderAttachment> harmAttachment;
    std::unique_ptr<SliderAttachment> sidechainShadowAttachment;
    std::unique_ptr<SliderAttachment> polarityAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> inputAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<SliderAttachment> tiltAttachment;
    std::unique_ptr<SliderAttachment> panAttachment;
    std::unique_ptr<SliderAttachment> limThresholdAttachment;

    std::unique_ptr<ButtonAttachment> syncAttachment;
    std::unique_ptr<ButtonAttachment> midiAttachment;
    std::unique_ptr<ButtonAttachment> alignAttachment;
    std::unique_ptr<ButtonAttachment> pdcAttachment;
    std::unique_ptr<ButtonAttachment> sidechainAttachment;
    std::unique_ptr<ButtonAttachment> chaosFilterAttachment;
    std::unique_ptr<ButtonAttachment> chaosDelayAttachment;

    std::unique_ptr<ComboBoxAttachment> modeInAttachment;
    std::unique_ptr<ComboBoxAttachment> modeOutAttachment;
    std::unique_ptr<ComboBoxAttachment> sumBusAttachment;
    std::unique_ptr<ComboBoxAttachment> limModeAttachment;
    std::unique_ptr<ComboBoxAttachment> invPolAttachment;
    std::unique_ptr<ComboBoxAttachment> invStrAttachment;
    std::unique_ptr<ComboBoxAttachment> mixModeAttachment;
    std::unique_ptr<ComboBoxAttachment> filterPosAttachment;

    juce::ComponentBoundsConstrainer resizeConstrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizerCorner;

    FREQScheme activeScheme;

    using HorizontalLayoutMetrics = TR::SimpleHorizontalLayoutMetrics;

    using VerticalLayoutMetrics = TR::SimpleVerticalLayoutMetrics;

    static HorizontalLayoutMetrics buildHorizontalLayout (int editorW, int valueColW);
    static VerticalLayoutMetrics buildVerticalLayout (int editorH, int biasY, bool ioExpanded);
    void updateCachedLayout();

    using MinimalLNF = TR::SimpleLookAndFeel;

    using PromptOverlay = TR::PromptOverlay;

    MinimalLNF lnf;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    PromptOverlay promptOverlay;

    void setupBar (juce::Slider& s);
    void updateFreqSliderForSyncMode();

    juce::String getFreqText() const;
    juce::String getFreqTextShort() const;

    juce::String getModText() const;
    juce::String getModTextShort() const;

    juce::String getFeedbackText() const;
    juce::String getFeedbackTextShort() const;

    juce::String getJitterText() const;
    juce::String getJitterTextShort() const;

    juce::String getCombText() const;
    juce::String getCombTextShort() const;

    void updateCombEnabled();
    void updateWindowEnabled();
    int getCurrentMaxHilbertWindow() const;
    void syncWindowToMax (bool notifyHost);
    void updatePdcTooltip();
    void showFreqShiftHilbertModeTooltip();
    void updateSidechainDependentControls();

    juce::String getEngineText() const;
    juce::String getEngineTextShort() const;

    juce::String getWindowText() const;
    juce::String getWindowTextShort() const;

    juce::String getStyleText() const;
    juce::String getStyleTextShort() const;

    juce::String getHarmText() const;
    juce::String getHarmTextShort() const;
    juce::String getSidechainShadowText() const;
    juce::String getSidechainShadowTextShort() const;

    juce::String getPolarityText() const;
    juce::String getPolarityTextShort() const;

    juce::String getMixText() const;
    juce::String getMixTextShort() const;

    juce::String getInputText() const;
    juce::String getInputTextShort() const;

    juce::String getOutputText() const;
    juce::String getOutputTextShort() const;

    juce::String getTiltText() const;
    juce::String getTiltTextShort() const;

    juce::String getPanText() const;
    juce::String getPanTextShort() const;

    int getTargetValueColumnWidth() const;

    void sliderValueChanged (juce::Slider* slider) override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    void applyPersistedUiStateFromProcessor (bool applySize, bool applyPaletteAndFx);

public:
    void triggerUiRestore() { applyPersistedUiStateFromProcessor (true, true); }

private:
    void applyLabelTextColour (juce::Label& label, juce::Colour colour);

    friend class TR::SimpleFilterBarComponent<FREQTRAudioProcessorEditor, FREQTRAudioProcessor, FREQScheme>;
    friend class TR::SimpleDualMixBarComponent<FREQTRAudioProcessorEditor, FREQTRAudioProcessor, FREQScheme>;
    friend struct TR::PromptHostBridge;

    juce::Rectangle<int> getValueAreaFor (const juce::Rectangle<int>& barBounds) const;
    juce::Slider* getSliderForValueAreaPoint (juce::Point<int> p);
    juce::Rectangle<int> getSyncLabelArea() const;
    juce::Rectangle<int> getMidiLabelArea() const;
    juce::Rectangle<int> getAlignLabelArea() const;
    juce::Rectangle<int> getPdcLabelArea() const;
    juce::Rectangle<int> getSidechainLabelArea() const;
    juce::Rectangle<int> getChaosFilterLabelArea() const;
    juce::Rectangle<int> getChaosDelayLabelArea() const;
    juce::Rectangle<int> getInfoIconArea() const;
    juce::Rectangle<int> getTitleHitArea() const;
    juce::String getFreqShiftHilbertModeTooltip() const;
    void updateInfoIconCache();
    bool refreshLegendTextCache();
    TR::SimpleMainPanelSpec buildMainPanelSpec();
    juce::Rectangle<int> getRowRepaintBounds (const juce::Slider& s) const;
    void applyActivePalette();
    void applyCrtState (bool enabled);
    void applyIoFxState (bool enabled);
    void updateIoFxMeterSliders();

    juce::Path cachedInfoGearPath;
    juce::Rectangle<float> cachedInfoGearHole;

    juce::String cachedFreqTextFull;
    juce::String cachedFreqTextShort;
    juce::String cachedModTextFull;
    juce::String cachedModTextShort;
    juce::String cachedFeedbackTextFull;
    juce::String cachedFeedbackTextShort;
    juce::String cachedJitterTextFull;
    juce::String cachedJitterTextShort;
    juce::String cachedCombTextFull;
    juce::String cachedCombTextShort;
    juce::String cachedEngineTextFull;
    juce::String cachedEngineTextShort;
    juce::String cachedWindowTextFull;
    juce::String cachedWindowTextShort;
    juce::String cachedStyleTextFull;
    juce::String cachedStyleTextShort;
    juce::String cachedHarmTextFull;
    juce::String cachedHarmTextShort;
    juce::String cachedPolarityTextFull;
    juce::String cachedPolarityTextShort;
    juce::String cachedMixTextFull;
    juce::String cachedMixTextShort;

    juce::String cachedLimThresholdTextFull;
    juce::String cachedLimThresholdTextShort;
    juce::String cachedLimThresholdIntOnly;

    juce::String cachedInputTextFull;
    juce::String cachedInputTextShort;
    juce::String cachedInputIntOnly;
    juce::String cachedOutputTextFull;
    juce::String cachedOutputTextShort;
    juce::String cachedOutputIntOnly;

    juce::String cachedTiltTextFull;
    juce::String cachedTiltTextShort;
    juce::String cachedTiltIntOnly;

    juce::String cachedMixIntOnly;
    juce::String cachedFreqIntOnly;
    juce::String cachedModIntOnly;
    juce::String cachedFeedbackIntOnly;
    juce::String cachedJitterIntOnly;
    juce::String cachedCombIntOnly;
    juce::String cachedEngineIntOnly;
    juce::String cachedWindowIntOnly;
    juce::String cachedHarmIntOnly;
    juce::String cachedPolarityIntOnly;
    juce::String cachedStyleIntOnly;

    juce::String cachedFilterTextFull;
    juce::String cachedFilterTextShort;

    juce::String cachedPanTextFull;
    juce::String cachedPanTextShort;

    juce::String cachedMidiDisplay;

    mutable std::uint64_t cachedValueColumnWidthKey = 0;
    mutable int cachedValueColumnWidth = 90;

    HorizontalLayoutMetrics cachedHLayout_;
    VerticalLayoutMetrics cachedVLayout_;
    std::array<juce::Rectangle<int>, 14> cachedValueAreas_;
    juce::Rectangle<int> cachedFilterValueArea_;
    juce::Rectangle<int> cachedPanValueArea_;
    juce::Rectangle<int> cachedLimThresholdValueArea_;

    static constexpr double kDefaultLimThreshold = 0.0;

    // IO collapsible section state
    juce::Rectangle<int> cachedToggleBarArea_;
    bool ioSectionExpanded_ = false;

    static constexpr double kDefaultPolarity = (double) FREQTRAudioProcessor::kPolarityDefault;

    static constexpr int kMinW = 360;
    static constexpr int kMinH = 752;
    static constexpr int kMaxW = kMinW + (kMinW / 2);
    static constexpr int kMaxH = kMinH;

    static constexpr int kLayoutVerticalBiasPx = 10;

    bool promptOverlayActive = false;
    bool suppressSizePersistence = false;
    int lastPersistedEditorW = -1;
    int lastPersistedEditorH = -1;
    std::atomic<uint32_t> lastUserInteractionMs { 0 };
    static constexpr uint32_t kUserInteractionPersistWindowMs = 5000;
    bool crtEnabled = false;
    bool ioFxEnabled = true;
    bool useCustomPalette = false;
    double lastInputSignalMs = -10000.0;
    double lastOutputSignalMs = -10000.0;

    CrtEffect crtEffect;
    float     crtTime = 0.0f;

    static constexpr int kPaletteColourCount = 4;
    std::array<juce::Colour, kPaletteColourCount> defaultPalette = TR::defaultSimplePalette();
    std::array<juce::Colour, kPaletteColourCount> customPalette = TR::defaultSimpleCustomPalette();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FREQTRAudioProcessorEditor)
};
