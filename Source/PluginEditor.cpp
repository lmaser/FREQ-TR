// PluginEditor.cpp â€” FREQ-TR
#include "PluginEditor.h"
#include "InfoContent.h"
#include <functional>

using namespace TR;

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace UiStateKeys = TR::SimpleUiStateKeys;

// â”€â”€ Timer & display constants â”€â”€
static constexpr int   kCrtTimerHz   = 10;
static constexpr int   kIdleTimerHz  = 4;
static constexpr float kMultEpsilon  = 0.01f;

static juce::String formatGainFaderDb (float dB)
{
    return TR::formatGainFaderDbShared (dB, FREQTRAudioProcessor::kGainFloorDb);
}

static juce::String formatGainFaderDbCompact (float dB)
{
    return TR::formatGainFaderDbCompactShared (dB, FREQTRAudioProcessor::kGainFloorDb);
}

static juce::String formatInlineFrequency (float hz)
{
    return TR::formatInlineFrequencyShared (hz);
}

// â”€â”€ Mod slider â†” multiplier conversion â”€â”€
static constexpr double kModCenter  = 0.5;
static constexpr double kModScale   = 3.0;
static constexpr double kModMaxMult = 4.0;
static constexpr double kModMinMult = 0.25;

static double modSliderToMultiplier (double v)
{
    if (v < kModCenter)
        return 1.0 / (kModMaxMult - kModScale * (v / kModCenter));
    return 1.0 + kModScale * ((v - kModCenter) / kModCenter);
}

static juce::String formatModHarmText (double v, bool withSuffix)
{
    return TR::formatModHarmTextShared (v, withSuffix);
}

template <typename Processor>
static bool isModHarmEnabled (Processor& processor) noexcept
{
    if (auto* value = processor.apvts.getRawParameterValue (Processor::kParamModHarm))
        return value->load (std::memory_order_relaxed) > 0.5f;
    return false;
}

template <typename Processor>
static void setModHarmEnabled (Processor& processor, bool shouldBeEnabled)
{
    if (auto* param = processor.apvts.getParameter (Processor::kParamModHarm))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (shouldBeEnabled ? 1.0f : 0.0f));
        param->endChangeGesture();
    }
}

static juce::String formatModHarmTooltip (bool enabled)
{
    return TR::formatModHarmTooltipShared (enabled);
}
static double multiplierToModSlider (double m)
{
    m = juce::jlimit (kModMinMult, kModMaxMult, m);
    if (m <= 1.0)
        return kModCenter * (1.0 - (1.0 / m)) / (kModMaxMult - 1.0) * (kModMaxMult - 1.0) / kModScale;
    return kModCenter + kModCenter * (m - 1.0) / kModScale;
}

// â”€â”€ MIDI channel tooltip â”€â”€
static juce::String formatMidiChannelTooltip (int ch, int delayMs = 0)
{
    return TR::formatMidiChannelTooltipShared (ch, delayMs, false);
}

// â”€â”€ Retrig tooltip â”€â”€
static juce::String formatRetrigTooltip (bool on)
{
    return juce::String ("RETRIG ") + (on ? "ON" : "OFF");
}

static juce::String formatChaosTooltip (float amountPercent, float speedHz)
{
    return TR::formatChaosTooltipShared (amountPercent, speedHz, FREQTRAudioProcessor::kChaosSpdMin, FREQTRAudioProcessor::kChaosSpdMax);
}

static juce::String formatPdcTooltip (bool on, int maxWindow)
{
    return juce::String ("PDC ") + (on ? "ON" : "OFF")
         + " | MAX WIN " + juce::String (FREQTRAudioProcessor::getCanonicalHilbertWindow (maxWindow));
}

static juce::String formatSidechainTooltip (float gainDb, float smooth,
                                             bool hpOn, float hp, int hpSlope,
                                             bool lpOn, float lp, int lpSlope)
{
    return TR::formatSidechainTooltipShared (gainDb, FREQTRAudioProcessor::kGainFloorDb,
                                             smooth, FREQTRAudioProcessor::kSidechainSmoothMin, FREQTRAudioProcessor::kSidechainSmoothMax,
                                             hpOn, hp, hpSlope, lpOn, lp, lpSlope,
                                             FREQTRAudioProcessor::kSidechainFilterFreqMin, FREQTRAudioProcessor::kSidechainFilterFreqMax,
                                             true);
}
// â”€â”€ Parameter listener IDs â”€â”€
static constexpr std::array<const char*, 7> kUiMirrorParamIds {
    FREQTRAudioProcessor::kParamUiPalette,
    FREQTRAudioProcessor::kParamUiCrt,
    FREQTRAudioProcessor::kParamUiIoFx,
    FREQTRAudioProcessor::kParamUiColor0,
    FREQTRAudioProcessor::kParamUiColor1,
    FREQTRAudioProcessor::kParamUiColor2,
    FREQTRAudioProcessor::kParamUiColor3
};

//========================== Editor ==========================


FREQTRAudioProcessorEditor::FREQTRAudioProcessorEditor (FREQTRAudioProcessor& p)
: AudioProcessorEditor (&p), audioProcessor (p)
{
    const std::array<BarSlider*, 16> barSliders { &inputSlider, &outputSlider, &mixSlider, &freqSlider, &modSlider, &combSlider, &feedbackSlider, &engineSlider, &windowSlider, &harmSlider, &polaritySlider, &jitterSlider, &styleSlider, &tiltSlider, &panSlider, &limThresholdSlider };

    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    crtEnabled = audioProcessor.getUiCrtEnabled();
    ioFxEnabled = audioProcessor.getUiIoFxEnabled();
    ioSectionExpanded_ = audioProcessor.getUiIoExpanded();

    for (int i = 0; i < 4; ++i)
        customPalette[(size_t) i] = audioProcessor.getUiCustomPaletteColour (i);

    TR::SimpleEditorLifecycle::initCommon (*this, audioProcessor, lnf, tooltipWindow,
        promptOverlay, resizeConstrainer, resizerCorner, kMinW, kMinH, kMaxW, kMaxH);
    applyActivePalette();
    suppressSizePersistence = true;
    lastPersistedEditorW = getWidth();
    lastPersistedEditorH = getHeight();
    suppressSizePersistence = false;

    // Wire slider formats
    {
        auto wireNumeric = [this](juce::Slider& s) { openNumericEntryPopupForSlider(s); };
        auto configure = [&](BarSlider& s, SliderValueFormat fmt, int dec = 0, bool numeric = true) {
            s.setFormat(fmt, dec);
            s.setOwner(this);
            if (numeric) s.onPopup = wireNumeric;
        };
        configure(inputSlider,        SliderValueFormat::gainDb,       1);
        configure(outputSlider,       SliderValueFormat::gainDb,       1);
        configure(mixSlider,          SliderValueFormat::percent,      1);
        configure(freqSlider,         SliderValueFormat::plain,        1);
        configure(modSlider,          SliderValueFormat::plain,        2);
        configure(feedbackSlider,     SliderValueFormat::percent,      1);
        configure(jitterSlider,       SliderValueFormat::percent,      1);
        configure(combSlider,         SliderValueFormat::plain,        2);
        configure(engineSlider,       SliderValueFormat::percent,      1, false);
        configure(windowSlider,       SliderValueFormat::plain,        0, false);
        configure(harmSlider,         SliderValueFormat::percent,      1);
        configure(polaritySlider,     SliderValueFormat::plain,        2);
        configure(styleSlider,        SliderValueFormat::plain,        0, false);
        configure(tiltSlider,         SliderValueFormat::plain,        1);
        configure(panSlider,          SliderValueFormat::pan,          1);
        configure(limThresholdSlider, SliderValueFormat::plain,        1);
    }
    for (auto* slider : barSliders) {
        setupBar(*slider);
        addAndMakeVisible(*slider);
        slider->addListener(this);
    }
    sidechainShadowSlider.setOwner(this);
    sidechainShadowSlider.setFormat(SliderValueFormat::percent, 1);
    setupBar(sidechainShadowSlider);
    addAndMakeVisible(sidechainShadowSlider);
    sidechainShadowSlider.addListener(this);
    TR::setSimpleComponentVisible(sidechainShadowSlider, false);

    windowSlider.setRange((double)FREQTRAudioProcessor::kHilbertWindowMin,
                           (double)FREQTRAudioProcessor::kHilbertWindowMax, 1.0);
    inputSlider.setSkewFactor(FREQTRAudioProcessor::kGainSkew);
    outputSlider.setSkewFactor(FREQTRAudioProcessor::kGainSkew);

    // IO sliders start hidden (collapsible section, collapsed by default)
    TR::setSimpleComponentVisible(inputSlider, false);
    TR::setSimpleComponentVisible(outputSlider, false);
    TR::setSimpleComponentVisible(mixSlider, false);
    TR::setSimpleComponentVisible(tiltSlider, false);
    TR::setSimpleComponentVisible(panSlider, false);

    // Filter bar â€” hidden along with IO sliders in collapsed state
    filterBar_.setOwner (this);
    filterBar_.setScheme (activeScheme);
    addAndMakeVisible (filterBar_);
    TR::setSimpleComponentVisible (filterBar_, false);
    filterBar_.updateFromProcessor();

    // Chaos buttons (visible only when IO expanded)
    chaosFilterButton.setButtonText ("");
    addAndMakeVisible (chaosFilterButton);
    TR::setSimpleComponentVisible (chaosFilterButton, false);
    {
        const float savedAmt = audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosAmtFilter)->load();
        const float savedSpd = audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosSpdFilter)->load();
        chaosFilterDisplay.setText ("", juce::dontSendNotification);
        chaosFilterDisplay.setInterceptsMouseClicks (true, false);
        chaosFilterDisplay.addMouseListener (this, false);
        chaosFilterDisplay.setTooltip (formatChaosTooltip (savedAmt, savedSpd));
        TR::configureSimpleTransparentLabel (chaosFilterDisplay, activeScheme);
        addAndMakeVisible (chaosFilterDisplay);
        TR::setSimpleComponentVisible (chaosFilterDisplay, false);
    }
    chaosDelayButton.setButtonText ("");
    addAndMakeVisible (chaosDelayButton);
    TR::setSimpleComponentVisible (chaosDelayButton, false);
    {
        const float savedAmt = audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosAmt)->load();
        const float savedSpd = audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosSpd)->load();
        chaosDelayDisplay.setText ("", juce::dontSendNotification);
        chaosDelayDisplay.setInterceptsMouseClicks (true, false);
        chaosDelayDisplay.addMouseListener (this, false);
        chaosDelayDisplay.setTooltip (formatChaosTooltip (savedAmt, savedSpd));
        TR::configureSimpleTransparentLabel (chaosDelayDisplay, activeScheme);
        addAndMakeVisible (chaosDelayDisplay);
        TR::setSimpleComponentVisible (chaosDelayDisplay, false);
    }

    // Mode In / Mode Out / Sum Bus combos
    {
        auto setupModeCombo = [this] (juce::ComboBox& combo)
        {
            addAndMakeVisible (combo);
            combo.addItem ("L+R",  1);
            combo.addItem ("M/S",  2);
            combo.addItem ("MID",  3);
            combo.addItem ("SIDE", 4);
            TR::centreSimpleCombo (combo);
            combo.setLookAndFeel (&lnf);
            TR::setSimpleComponentVisible (combo, false);
        };
        setupModeCombo (modeInCombo);
        setupModeCombo (modeOutCombo);

        addAndMakeVisible (sumBusCombo);
        sumBusCombo.addItem ("ST",              1);
        sumBusCombo.addItem (juce::String::fromUTF8 (u8"\u2192M"), 2);
        sumBusCombo.addItem (juce::String::fromUTF8 (u8"\u2192S"), 3);
        TR::centreSimpleCombo (sumBusCombo);
        sumBusCombo.setLookAndFeel (&lnf);
        TR::setSimpleComponentVisible (sumBusCombo, false);

        addAndMakeVisible (limModeCombo);
        limModeCombo.addItem ("NONE",   1);
        limModeCombo.addItem ("WET",    2);
        limModeCombo.addItem ("GLOBAL", 3);
        TR::centreSimpleCombo (limModeCombo);
        limModeCombo.setLookAndFeel (&lnf);
        TR::setSimpleComponentVisible (limModeCombo, false);

        addAndMakeVisible (invPolCombo);
        invPolCombo.addItem ("NONE",   1);
        invPolCombo.addItem ("WET",    2);
        invPolCombo.addItem ("GLOBAL", 3);
        TR::centreSimpleCombo (invPolCombo);
        invPolCombo.setLookAndFeel (&lnf);
        TR::setSimpleComponentVisible (invPolCombo, false);

        addAndMakeVisible (invStrCombo);
        invStrCombo.addItem ("NONE",   1);
        invStrCombo.addItem ("WET",    2);
        invStrCombo.addItem ("GLOBAL", 3);
        TR::centreSimpleCombo (invStrCombo);
        invStrCombo.setLookAndFeel (&lnf);
        TR::setSimpleComponentVisible (invStrCombo, false);

        addAndMakeVisible (mixModeCombo);
        mixModeCombo.addItem ("INSERT", 1);
        mixModeCombo.addItem ("SEND",   2);
        TR::centreSimpleCombo (mixModeCombo);
        mixModeCombo.setLookAndFeel (&lnf);
        TR::setSimpleComponentVisible (mixModeCombo, false);

        addAndMakeVisible (filterPosCombo);
        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25bc T\u25bc"), 1);
        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25b2 T\u25b2"), 2);
        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25b2 T\u25bc"), 3);
        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25bc T\u25b2"), 4);
        TR::centreSimpleCombo (filterPosCombo);
        filterPosCombo.setLookAndFeel (&lnf);
        TR::setSimpleComponentVisible (filterPosCombo, false);

        addAndMakeVisible (dualMixBar_);
        dualMixBar_.setOwner (this);
        TR::setSimpleComponentVisible (dualMixBar_, false);
    }

    syncButton.setButtonText ("");
    midiButton.setButtonText ("");
    alignButton.setButtonText ("");
    pdcButton.setButtonText ("");
    sidechainButton.setButtonText ("");

    addAndMakeVisible (syncButton);
    addAndMakeVisible (midiButton);
    addAndMakeVisible (alignButton);
    addAndMakeVisible (pdcButton);
    addAndMakeVisible (sidechainButton);
    sidechainButton.setInterceptsMouseClicks (false, false);
    sidechainButton.addMouseListener (this, false);

    // MIDI channel tooltip overlay
    const int savedChannel = audioProcessor.getMidiChannel();
    midiChannelDisplay.setText ("", juce::dontSendNotification);
    midiChannelDisplay.setInterceptsMouseClicks (true, false);
    midiChannelDisplay.addMouseListener (this, false);
    midiChannelDisplay.setTooltip (formatMidiChannelTooltip (savedChannel, audioProcessor.getMidiDelayMs()));
    TR::configureSimpleTransparentLabel (midiChannelDisplay, activeScheme);
    addAndMakeVisible (midiChannelDisplay);

    // Retrig tooltip overlay (over SYNC label)
    const bool savedRetrig = audioProcessor.apvts.getParameterAsValue (
        FREQTRAudioProcessor::kParamRetrig).getValue();
    retrigDisplay.setText ("", juce::dontSendNotification);
    retrigDisplay.setInterceptsMouseClicks (true, false);
    retrigDisplay.addMouseListener (this, false);
    retrigDisplay.setTooltip (formatRetrigTooltip (savedRetrig));
    TR::configureSimpleTransparentLabel (retrigDisplay, activeScheme);
    addAndMakeVisible (retrigDisplay);

    pdcDisplay.setText ("", juce::dontSendNotification);
    pdcDisplay.setInterceptsMouseClicks (true, false);
    pdcDisplay.addMouseListener (this, false);
    pdcDisplay.setTooltip (formatPdcTooltip (
        pdcButton.getToggleState(),
        (int) std::lround (audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamMaxWindow)->load())));
    TR::configureSimpleTransparentLabel (pdcDisplay, activeScheme);
    addAndMakeVisible (pdcDisplay);

    sidechainDisplay.setText ("", juce::dontSendNotification);
    sidechainDisplay.setInterceptsMouseClicks (true, false);
    sidechainDisplay.addMouseListener (this, false);
    sidechainDisplay.setTooltip (formatSidechainTooltip (
        audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainGain)->load(),
        audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainSmooth)->load(),
        audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainHpOn)->load() > 0.5f,
        audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainHp)->load(),
        (int) std::lround (audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainHpSlope)->load()),
        audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainLpOn)->load() > 0.5f,
        audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainLp)->load(),
        (int) std::lround (audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainLpSlope)->load())));
    TR::configureSimpleTransparentLabel (sidechainDisplay, activeScheme);
    addAndMakeVisible (sidechainDisplay);

    auto bindSlider = [&] (std::unique_ptr<SliderAttachment>& attachment,
                           const char* paramId,
                           BarSlider& slider,
                           double defaultValue)
    {
        attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, paramId, slider);
        slider.setDoubleClickReturnValue (true, defaultValue);
    };

    bindSlider (freqAttachment,    FREQTRAudioProcessor::kParamFreq,    freqSlider,    (double) FREQTRAudioProcessor::kFreqDefault);
    bindSlider (modAttachment,     FREQTRAudioProcessor::kParamMod,     modSlider,     (double) FREQTRAudioProcessor::kModDefault);
    bindSlider (combAttachment,    FREQTRAudioProcessor::kParamComb,    combSlider,    (double) FREQTRAudioProcessor::kCombDefault);
    bindSlider (feedbackAttachment, FREQTRAudioProcessor::kParamFeedback, feedbackSlider, (double) FREQTRAudioProcessor::kFeedbackDefault);
    bindSlider (jitterAttachment,  FREQTRAudioProcessor::kParamJitter,  jitterSlider,  (double) FREQTRAudioProcessor::kJitterDefault);
    bindSlider (engineAttachment,  FREQTRAudioProcessor::kParamEngine,  engineSlider,  (double) FREQTRAudioProcessor::kEngineDefault);
    bindSlider (windowAttachment,  FREQTRAudioProcessor::kParamWindow,  windowSlider,  (double) FREQTRAudioProcessor::kHilbertWindowDefault);
    bindSlider (styleAttachment,   FREQTRAudioProcessor::kParamStyle,   styleSlider,   (double) FREQTRAudioProcessor::kStyleDefault);
    bindSlider (harmAttachment,    FREQTRAudioProcessor::kParamHarm,    harmSlider,    (double) FREQTRAudioProcessor::kHarmDefault);
    bindSlider (sidechainShadowAttachment, FREQTRAudioProcessor::kParamSidechainShadow, sidechainShadowSlider, (double) FREQTRAudioProcessor::kSidechainShadowDefault);
    bindSlider (polarityAttachment, FREQTRAudioProcessor::kParamPolarity, polaritySlider, kDefaultPolarity);
    bindSlider (mixAttachment,      FREQTRAudioProcessor::kParamMix,     mixSlider,     (double) FREQTRAudioProcessor::kMixDefault);
    bindSlider (inputAttachment,    FREQTRAudioProcessor::kParamInput,   inputSlider,   (double) FREQTRAudioProcessor::kInputDefault);
    bindSlider (outputAttachment,   FREQTRAudioProcessor::kParamOutput,  outputSlider,  (double) FREQTRAudioProcessor::kOutputDefault);
    bindSlider (tiltAttachment,     FREQTRAudioProcessor::kParamTilt,    tiltSlider,    (double) FREQTRAudioProcessor::kTiltDefault);
    bindSlider (panAttachment,      FREQTRAudioProcessor::kParamPan,     panSlider,     (double) FREQTRAudioProcessor::kPanDefault);
    bindSlider (limThresholdAttachment, FREQTRAudioProcessor::kParamLimThreshold, limThresholdSlider, kDefaultLimThreshold);

    // Keep Comb editable even when Feedback is zero; it is part of the feedback tone setup.
    updateCombEnabled();
    updateWindowEnabled();

    auto bindButton = [&] (std::unique_ptr<ButtonAttachment>& attachment,
                           const char* paramId,
                           juce::Button& button)
    {
        attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, paramId, button);
    };

    bindButton (syncAttachment,  FREQTRAudioProcessor::kParamSync,  syncButton);
    bindButton (midiAttachment,  FREQTRAudioProcessor::kParamMidi,  midiButton);
    bindButton (alignAttachment, FREQTRAudioProcessor::kParamAlign, alignButton);
    bindButton (pdcAttachment,   FREQTRAudioProcessor::kParamPdc,   pdcButton);
    bindButton (sidechainAttachment, FREQTRAudioProcessor::kParamSidechain, sidechainButton);
    bindButton (chaosFilterAttachment, FREQTRAudioProcessor::kParamChaos, chaosFilterButton);
    bindButton (chaosDelayAttachment,  FREQTRAudioProcessor::kParamChaosD, chaosDelayButton);

    modeInAttachment  = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, FREQTRAudioProcessor::kParamModeIn,  modeInCombo);
    modeOutAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, FREQTRAudioProcessor::kParamModeOut, modeOutCombo);
    sumBusAttachment  = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, FREQTRAudioProcessor::kParamSumBus,  sumBusCombo);
    limModeAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, FREQTRAudioProcessor::kParamLimMode, limModeCombo);
    invPolAttachment  = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, FREQTRAudioProcessor::kParamInvPol,  invPolCombo);
    invStrAttachment  = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, FREQTRAudioProcessor::kParamInvStr,  invStrCombo);
    mixModeAttachment   = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, FREQTRAudioProcessor::kParamMixMode,   mixModeCombo);
    filterPosAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, FREQTRAudioProcessor::kParamFilterPos, filterPosCombo);

    for (auto* paramId : kUiMirrorParamIds)
        audioProcessor.apvts.addParameterListener (paramId, this);

    audioProcessor.apvts.addParameterListener (FREQTRAudioProcessor::kParamSync, this);
    audioProcessor.apvts.addParameterListener (FREQTRAudioProcessor::kParamSidechain, this);
    audioProcessor.apvts.addParameterListener (FREQTRAudioProcessor::kParamPdc, this);
    audioProcessor.apvts.addParameterListener (FREQTRAudioProcessor::kParamWindow, this);
    audioProcessor.apvts.addParameterListener (FREQTRAudioProcessor::kParamMaxWindow, this);

    // Apply initial SYNC state
    if (syncButton.getToggleState())
        updateFreqSliderForSyncMode();
    syncWindowToMax (true);
    updatePdcTooltip();
    updateSidechainDependentControls();

    TR::SimpleEditorLifecycle::scheduleUiRestore (*this);

    applyCrtState (crtEnabled);
    refreshLegendTextCache();
    resized();
}

FREQTRAudioProcessorEditor::~FREQTRAudioProcessorEditor()
{
    setComponentEffect (nullptr);
    stopTimer();

    for (auto* paramId : kUiMirrorParamIds)
        audioProcessor.apvts.removeParameterListener (paramId, this);

    audioProcessor.apvts.removeParameterListener (FREQTRAudioProcessor::kParamSync, this);
    audioProcessor.apvts.removeParameterListener (FREQTRAudioProcessor::kParamSidechain, this);
    audioProcessor.apvts.removeParameterListener (FREQTRAudioProcessor::kParamPdc, this);
    audioProcessor.apvts.removeParameterListener (FREQTRAudioProcessor::kParamWindow, this);
    audioProcessor.apvts.removeParameterListener (FREQTRAudioProcessor::kParamMaxWindow, this);

    audioProcessor.setUiUseCustomPalette (useCustomPalette);
    audioProcessor.setUiCrtEnabled (crtEnabled);
    audioProcessor.setUiIoFxEnabled (ioFxEnabled);

    dismissEditorOwnedModalPrompts (lnf);
    setPromptOverlayActive (false);

    const std::array<BarSlider*, 16> barSliders { &inputSlider, &outputSlider, &mixSlider, &freqSlider, &modSlider, &combSlider, &feedbackSlider, &engineSlider, &windowSlider, &harmSlider, &polaritySlider, &jitterSlider, &styleSlider, &tiltSlider, &panSlider, &limThresholdSlider };
    for (auto* slider : barSliders)
        slider->removeListener (this);
    sidechainShadowSlider.removeListener (this);

    if (tooltipWindow != nullptr)
        tooltipWindow->setLookAndFeel (nullptr);

    modeInCombo.setLookAndFeel (nullptr);
    modeOutCombo.setLookAndFeel (nullptr);
    sumBusCombo.setLookAndFeel (nullptr);
    limModeCombo.setLookAndFeel (nullptr);
    invPolCombo.setLookAndFeel (nullptr);
    invStrCombo.setLookAndFeel (nullptr);
    mixModeCombo.setLookAndFeel (nullptr);
    filterPosCombo.setLookAndFeel (nullptr);

    setLookAndFeel (nullptr);
}

void FREQTRAudioProcessorEditor::applyActivePalette()
{
    const auto& palette = useCustomPalette ? customPalette : defaultPalette;

    activeScheme = TR::applySimplePalette (palette, lnf,
        { &chaosFilterDisplay, &chaosDelayDisplay, &midiChannelDisplay, &retrigDisplay, &pdcDisplay, &sidechainDisplay },
        { &inputSlider, &outputSlider, &mixSlider, &freqSlider, &modSlider, &feedbackSlider, &jitterSlider, &combSlider, &engineSlider, &windowSlider, &harmSlider, &sidechainShadowSlider, &polaritySlider, &styleSlider, &tiltSlider, &panSlider, &limThresholdSlider },
        { &modeInCombo, &modeOutCombo, &sumBusCombo, &limModeCombo, &invPolCombo, &invStrCombo, &mixModeCombo, &filterPosCombo });

    filterBar_.setScheme (activeScheme);
    dualMixBar_.setScheme (activeScheme);
    updateIoFxMeterSliders();
}

void FREQTRAudioProcessorEditor::applyCrtState (bool enabled)
{
    crtEnabled = enabled;
    TR::SimpleUIController::applyCrt (crtEnabled, *this, *this, crtEffect, crtTime, kCrtTimerHz, kIdleTimerHz);
}

void FREQTRAudioProcessorEditor::applyIoFxState (bool enabled)
{
    ioFxEnabled = enabled;
    updateIoFxMeterSliders();
}

void FREQTRAudioProcessorEditor::updateIoFxMeterSliders()
{
    TR::SimpleUIController::updateIoMeters (defaultPalette, customPalette, useCustomPalette,
        inputSlider, outputSlider, ioFxEnabled,
        lastInputSignalMs, lastOutputSignalMs,
        audioProcessor.getInputMeterPeak(), audioProcessor.getOutputMeterPeak());
}

void FREQTRAudioProcessorEditor::applyLabelTextColour (juce::Label& label, juce::Colour colour)
{
    TR::applySimpleLabelTextColour (label, colour);
}

void FREQTRAudioProcessorEditor::sliderValueChanged (juce::Slider* slider)
{
    auto isBarSlider = [&] (const juce::Slider* s)
    {
        return s == &freqSlider || s == &modSlider || s == &feedbackSlider || s == &jitterSlider || s == &combSlider
            || s == &engineSlider || s == &windowSlider || s == &styleSlider || s == &harmSlider || s == &polaritySlider
            || s == &sidechainShadowSlider
            || s == &inputSlider || s == &outputSlider || s == &tiltSlider || s == &panSlider || s == &mixSlider
            || s == &limThresholdSlider;
    };

    if (slider == &feedbackSlider)
        updateCombEnabled();
    if (slider == &engineSlider)
        updateWindowEnabled();
    if (slider == &windowSlider)
    {
        const double snapped = (double) juce::jmin (
            FREQTRAudioProcessor::getCanonicalHilbertWindow ((int) std::lround (windowSlider.getValue())),
            getCurrentMaxHilbertWindow());
        if (std::abs (windowSlider.getValue() - snapped) > 0.5)
            windowSlider.setValue (snapped, juce::sendNotificationSync);
    }

    refreshLegendTextCache();

    if (slider == nullptr)
    {
        repaint();
        return;
    }

    if (isBarSlider (slider))
    {
        repaint (getRowRepaintBounds (*slider));
        return;
    }

    repaint();
}

void FREQTRAudioProcessorEditor::setPromptOverlayActive (bool shouldBeActive)
{
    if (promptOverlayActive == shouldBeActive)
        return;

    TR::SimpleUIController::setOverlayActive (*this, promptOverlay, promptOverlayActive, shouldBeActive, lnf);

    if (! shouldBeActive)
    {
        updateCombEnabled();
        updateWindowEnabled();
        updateSidechainDependentControls();
    }
}

void FREQTRAudioProcessorEditor::moved()
{
    TR::SimpleUIController::anchorPromptsOnMove (*this, promptOverlayActive, promptOverlay, lnf);
}

void FREQTRAudioProcessorEditor::parentHierarchyChanged()
{
    TR::SimpleUIController::darkenWindowBackground_Hwnd (*this);
}

void FREQTRAudioProcessorEditor::parameterChanged (const juce::String& parameterID, float newValue)
{
    // Handle SYNC toggle
    if (parameterID == FREQTRAudioProcessor::kParamSync)
    {
        juce::ignoreUnused (newValue);
        juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]()
        {
            if (safeThis == nullptr) return;
            safeThis->updateFreqSliderForSyncMode();
        });
        return;
    }

    if (parameterID == FREQTRAudioProcessor::kParamSidechain)
    {
        juce::ignoreUnused (newValue);
        juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]()
        {
            if (safeThis == nullptr) return;
            safeThis->updateSidechainDependentControls();
            safeThis->refreshLegendTextCache();
            safeThis->updateCachedLayout();
            safeThis->repaint();
        });
        return;
    }

    if (parameterID == FREQTRAudioProcessor::kParamPdc)
    {
        juce::ignoreUnused (newValue);
        juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]()
        {
            if (safeThis == nullptr) return;
            safeThis->updatePdcTooltip();
        });
        return;
    }

    if (parameterID == FREQTRAudioProcessor::kParamWindow
        || parameterID == FREQTRAudioProcessor::kParamMaxWindow)
    {
        juce::ignoreUnused (newValue);
        juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]()
        {
            if (safeThis == nullptr) return;
            safeThis->syncWindowToMax (true);
            safeThis->updatePdcTooltip();
            safeThis->refreshLegendTextCache();
            safeThis->repaint();
        });
        return;
    }

    const bool isSizeParam = parameterID == FREQTRAudioProcessor::kParamUiWidth
                         || parameterID == FREQTRAudioProcessor::kParamUiHeight;

    const bool isUiVisualParam = parameterID == FREQTRAudioProcessor::kParamUiPalette
                             || parameterID == FREQTRAudioProcessor::kParamUiCrt
                             || parameterID == FREQTRAudioProcessor::kParamUiIoFx
                             || parameterID == FREQTRAudioProcessor::kParamUiColor0
                             || parameterID == FREQTRAudioProcessor::kParamUiColor1
                             || parameterID == FREQTRAudioProcessor::kParamUiColor2
                             || parameterID == FREQTRAudioProcessor::kParamUiColor3;

    if (! isSizeParam && ! isUiVisualParam)
        return;

    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis, isSizeParam]()
    {
        if (safeThis == nullptr) return;

        if (isSizeParam)
            safeThis->applyPersistedUiStateFromProcessor (true, false);
        else
            safeThis->applyPersistedUiStateFromProcessor (false, true);
    });
}

void FREQTRAudioProcessorEditor::timerCallback()
{
    if (suppressSizePersistence)
        return;

    // MIDI display update
    const auto newMidiDisplay = audioProcessor.getCurrentFreqDisplay();
    if (newMidiDisplay != cachedMidiDisplay)
    {
        cachedMidiDisplay = newMidiDisplay;
        if (refreshLegendTextCache())
            updateCachedLayout();
        repaint (getRowRepaintBounds (freqSlider));
    }

    const int w = getWidth();
    const int h = getHeight();

    const uint32_t last = lastUserInteractionMs.load (std::memory_order_relaxed);
    const uint32_t now = juce::Time::getMillisecondCounter();
    const bool userRecent = (now - last) <= (uint32_t) kUserInteractionPersistWindowMs;

    if ((w != lastPersistedEditorW || h != lastPersistedEditorH) && userRecent)
    {
        audioProcessor.setUiEditorSize (w, h);
        lastPersistedEditorW = w;
        lastPersistedEditorH = h;
    }

    if (crtEnabled && w > 0 && h > 0)
    {
        crtTime += 0.1f;
        crtEffect.setTime (crtTime);

        const bool anySliderDragging = freqSlider.isMouseButtonDown()
                                    || modSlider.isMouseButtonDown()
                                    || feedbackSlider.isMouseButtonDown()
                                    || jitterSlider.isMouseButtonDown()
                                    || engineSlider.isMouseButtonDown()
                                    || styleSlider.isMouseButtonDown()
                                    || harmSlider.isMouseButtonDown()
                                    || sidechainShadowSlider.isMouseButtonDown()
                                    || polaritySlider.isMouseButtonDown()
                                    || mixSlider.isMouseButtonDown()
                                    || inputSlider.isMouseButtonDown()
                                    || outputSlider.isMouseButtonDown();
        if (! anySliderDragging)
            repaint();
    }

    // Keep filter bar markers up to date
    if (filterBar_.isVisible())
        filterBar_.updateFromProcessor();

    // Keep dual mix bar markers up to date + visibility swap
    if (ioSectionExpanded_)
    {
        const float prevDry = dualMixBar_.getDryLevel();
        const float prevWet = dualMixBar_.getWetLevel();
        dualMixBar_.updateFromProcessor();
        const bool isSendMode = mixModeCombo.getSelectedId() == 2;

        // Refresh legend when levels change in SEND mode
        if (isSendMode && (dualMixBar_.getDryLevel() != prevDry || dualMixBar_.getWetLevel() != prevWet))
        {
            if (refreshLegendTextCache())
                updateCachedLayout();
            repaint();
        }

        if (mixSlider.isVisible() == isSendMode)
        {
            TR::setSimpleComponentVisible (mixSlider, ! isSendMode);
            TR::setSimpleComponentVisible (dualMixBar_, isSendMode);
            if (refreshLegendTextCache())
                updateCachedLayout();
            repaint();
        }
    }
    else
    {
        if (dualMixBar_.isVisible())
            dualMixBar_.updateFromProcessor();

        const bool isSend = (mixModeCombo.getSelectedItemIndex() == 1);
        if (isSend && mixSlider.isVisible())
        {
            TR::setSimpleComponentVisible (mixSlider, false);
            TR::setSimpleComponentVisible (dualMixBar_, true);
        }
        else if (! isSend && dualMixBar_.isVisible())
        {
            TR::setSimpleComponentVisible (dualMixBar_, false);
            TR::setSimpleComponentVisible (mixSlider, true);
        }
    }
    updateIoFxMeterSliders();

}

void FREQTRAudioProcessorEditor::applyPersistedUiStateFromProcessor (bool applySize, bool applyPaletteAndFx)
{
    if (applySize)
    {
        const int targetW = juce::jlimit (kMinW, kMaxW, audioProcessor.getUiEditorWidth());
        const int targetH = juce::jlimit (kMinH, kMaxH, audioProcessor.getUiEditorHeight());

        if (getWidth() != targetW || getHeight() != targetH)
        {
            suppressSizePersistence = true;
            setSize (targetW, targetH);
            suppressSizePersistence = false;
        }
    }

    if (applyPaletteAndFx)
    {
        bool paletteChanged = false;
        for (int i = 0; i < 4; ++i)
        {
            const auto c = audioProcessor.getUiCustomPaletteColour (i);
            if (customPalette[(size_t) i].getARGB() != c.getARGB())
            {
                customPalette[(size_t) i] = c;
                paletteChanged = true;
            }
        }

        const bool targetUseCustomPalette = audioProcessor.getUiUseCustomPalette();
        const bool targetCrtEnabled = audioProcessor.getUiCrtEnabled();
        const bool targetIoFxEnabled = audioProcessor.getUiIoFxEnabled();

        const bool paletteSwitchChanged = (useCustomPalette != targetUseCustomPalette);
        const bool fxChanged = (crtEnabled != targetCrtEnabled);
        const bool ioFxChanged = (ioFxEnabled != targetIoFxEnabled);

        if (paletteSwitchChanged)
            useCustomPalette = targetUseCustomPalette;

        if (fxChanged)
            applyCrtState (targetCrtEnabled);

        if (ioFxChanged)
            applyIoFxState (targetIoFxEnabled);

        const bool targetIoExpanded = audioProcessor.getUiIoExpanded();
        const bool ioChanged = (ioSectionExpanded_ != targetIoExpanded);
        if (ioChanged)
        {
            ioSectionExpanded_ = targetIoExpanded;
            resized();
        }

        if (paletteChanged || paletteSwitchChanged)
            applyActivePalette();

        if (paletteChanged || paletteSwitchChanged || fxChanged || ioFxChanged || ioChanged)
            repaint();
    }
}

//========================== Text getters ==========================

juce::String FREQTRAudioProcessorEditor::getFreqText() const
{
    if (cachedMidiDisplay.isNotEmpty() && ! freqSlider.isMouseButtonDown())
        return cachedMidiDisplay;

    const bool isSyncOn = syncButton.getToggleState();
    if (isSyncOn)
    {
        const int idx = (int) freqSlider.getValue();
        return audioProcessor.getFreqSyncName (idx) + " FREQ";
    }

    const float hz = (float) freqSlider.getValue();
    return formatInlineFrequency (hz) + " FREQ";
}

juce::String FREQTRAudioProcessorEditor::getFreqTextShort() const
{
    if (cachedMidiDisplay.isNotEmpty() && ! freqSlider.isMouseButtonDown())
        return cachedMidiDisplay;

    const bool isSyncOn = syncButton.getToggleState();
    if (isSyncOn)
    {
        const int idx = (int) freqSlider.getValue();
        return audioProcessor.getFreqSyncName (idx);
    }

    const float hz = (float) freqSlider.getValue();
    return formatInlineFrequency (hz) + " FREQ";
}

juce::String FREQTRAudioProcessorEditor::getModText() const
{
    if (isModHarmEnabled (audioProcessor))
        return formatModHarmText (modSlider.getValue(), true);

    const float mult = (float) modSliderToMultiplier (modSlider.getValue());
    if (std::abs (mult - 1.0f) < kMultEpsilon)
        return "X1 MOD";
    return "X" + juce::String (mult, 2) + " MOD";
}

juce::String FREQTRAudioProcessorEditor::getModTextShort() const
{
    if (isModHarmEnabled (audioProcessor))
        return formatModHarmText (modSlider.getValue(), false);

    const float mult = (float) modSliderToMultiplier (modSlider.getValue());
    if (std::abs (mult - 1.0f) < kMultEpsilon)
        return "X1 MOD";
    return "X" + juce::String (mult, 2) + " MOD";
}

juce::String FREQTRAudioProcessorEditor::getFeedbackText() const
{
    const int pct = (int) std::lround (juce::jlimit (0.0, 1.0, feedbackSlider.getValue()) * 100.0);
    return juce::String (pct) + "% FBK";
}

juce::String FREQTRAudioProcessorEditor::getFeedbackTextShort() const
{
    const int pct = (int) std::lround (juce::jlimit (0.0, 1.0, feedbackSlider.getValue()) * 100.0);
    return juce::String (pct) + "% FBK";
}

juce::String FREQTRAudioProcessorEditor::getJitterText() const
{
    const int pct = (int) std::lround (juce::jlimit (0.0, 1.0, jitterSlider.getValue()) * 100.0);
    return juce::String (pct) + "% JITTER";
}

juce::String FREQTRAudioProcessorEditor::getJitterTextShort() const
{
    const int pct = (int) std::lround (juce::jlimit (0.0, 1.0, jitterSlider.getValue()) * 100.0);
    return juce::String (pct) + "% JIT";
}

juce::String FREQTRAudioProcessorEditor::getCombText() const
{
    const float hz = (float) combSlider.getValue();
    return formatInlineFrequency (hz) + " COMB";
}

juce::String FREQTRAudioProcessorEditor::getCombTextShort() const
{
    const float hz = (float) combSlider.getValue();
    return formatInlineFrequency (hz) + " COMB";
}

void FREQTRAudioProcessorEditor::updateCombEnabled()
{
    combSlider.setAlpha (1.0f);
    combSlider.setEnabled (true);
    repaint();
}

void FREQTRAudioProcessorEditor::updateWindowEnabled()
{
    const bool active = (engineSlider.getValue() > 0.5);
    windowSlider.setAlpha (active ? 1.0f : 0.35f);
    windowSlider.setEnabled (active);
    repaint();
}

int FREQTRAudioProcessorEditor::getCurrentMaxHilbertWindow() const
{
    return FREQTRAudioProcessor::getCanonicalHilbertWindow ((int) std::lround (
        audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamMaxWindow)->load()));
}

void FREQTRAudioProcessorEditor::syncWindowToMax (bool notifyHost)
{
    const int maxWindow = getCurrentMaxHilbertWindow();
    const double sliderMin = maxWindow > FREQTRAudioProcessor::kHilbertWindowMin
                           ? (double) FREQTRAudioProcessor::kHilbertWindowMin
                           : 0.0;
    windowSlider.setRange (sliderMin,
                           (double) maxWindow,
                           1.0);

    const int currentWindow = FREQTRAudioProcessor::getCanonicalHilbertWindow ((int) std::lround (
        audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamWindow)->load()));
    const int clampedWindow = juce::jmin (currentWindow, maxWindow);

    if (currentWindow != clampedWindow)
    {
        if (notifyHost)
        {
            if (auto* p = audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamWindow))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) clampedWindow));
        }
    }

    const int sliderWindow = FREQTRAudioProcessor::getCanonicalHilbertWindow ((int) std::lround (windowSlider.getValue()));
    if (sliderWindow != clampedWindow)
        windowSlider.setValue ((double) clampedWindow, juce::dontSendNotification);
}

void FREQTRAudioProcessorEditor::updatePdcTooltip()
{
    const int maxWindow = getCurrentMaxHilbertWindow();
    const auto tooltip = formatPdcTooltip (pdcButton.getToggleState(), maxWindow);
    pdcDisplay.setTooltip (tooltip);
}

void FREQTRAudioProcessorEditor::updateSidechainDependentControls()
{
    if (promptOverlayActive)
        return;

    const bool sidechainOn = sidechainButton.getToggleState();
    const float harmonicAlpha = sidechainOn ? 0.35f : 1.0f;

    freqSlider.setAlpha (1.0f);
    modSlider.setAlpha (1.0f);
    harmSlider.setAlpha (harmonicAlpha);
    sidechainShadowSlider.setAlpha (1.0f);
    syncButton.setAlpha (1.0f);
    midiButton.setAlpha (1.0f);
    retrigDisplay.setAlpha (1.0f);
    midiChannelDisplay.setAlpha (1.0f);

    freqSlider.setEnabled (true);
    modSlider.setEnabled (true);
    const bool showShadow = sidechainOn && ! ioSectionExpanded_ && harmSlider.getWidth() > 0 && harmSlider.getHeight() > 0;
    if (showShadow)
    {
        sidechainShadowSlider.setBounds (harmSlider.getBounds());
        TR::setSimpleComponentVisible (sidechainShadowSlider, true);
        sidechainShadowSlider.setEnabled (true);
        TR::setSimpleComponentVisible (harmSlider, false);
        harmSlider.setEnabled (false);
    }
    else
    {
        TR::setSimpleComponentVisible (sidechainShadowSlider, false);
        sidechainShadowSlider.setEnabled (false);
        if (! ioSectionExpanded_ && harmSlider.getWidth() > 0 && harmSlider.getHeight() > 0)
            TR::setSimpleComponentVisible (harmSlider, true);
        harmSlider.setEnabled (! sidechainOn);
    }
    syncButton.setEnabled (true);
    midiButton.setEnabled (true);
    retrigDisplay.setEnabled (true);
    midiChannelDisplay.setEnabled (true);

    repaint();
}

juce::String FREQTRAudioProcessorEditor::getEngineText() const
{
    const float val = (float) engineSlider.getValue();
    if (val < 0.01f)
        return "AM ENGINE";
    if (std::abs (val - 0.5f) < 0.01f)
        return "RM ENGINE";
    if (val > 0.99f)
        return "FREQ SHIFT ENGINE";
    if (val < 0.5f)
    {
        const int pct = (int) std::lround (val * 200.0f);
        return "AM|RM " + juce::String (pct) + "% ENGINE";
    }

    const int pct = (int) std::lround ((val - 0.5f) * 200.0f);
    return "RM|FS " + juce::String (pct) + "% ENGINE";
}

juce::String FREQTRAudioProcessorEditor::getEngineTextShort() const
{
    const float val = (float) engineSlider.getValue();
    if (val < 0.01f)
        return "AM";
    if (std::abs (val - 0.5f) < 0.01f)
        return "RM";
    if (val > 0.99f)
        return "FREQ SHIFT";
    if (val < 0.5f)
    {
        const int pct = (int) std::lround (val * 200.0f);
        return "AM|RM " + juce::String (pct) + "%";
    }

    const int pct = (int) std::lround ((val - 0.5f) * 200.0f);
    return "RM|FS " + juce::String (pct) + "%";
}

juce::String FREQTRAudioProcessorEditor::getWindowText() const
{
    const int window = juce::jmin (
        FREQTRAudioProcessor::getCanonicalHilbertWindow ((int) std::lround (windowSlider.getValue())),
        getCurrentMaxHilbertWindow());
    return juce::String (window) + " WINDOW";
}

juce::String FREQTRAudioProcessorEditor::getWindowTextShort() const
{
    const int window = juce::jmin (
        FREQTRAudioProcessor::getCanonicalHilbertWindow ((int) std::lround (windowSlider.getValue())),
        getCurrentMaxHilbertWindow());
    return juce::String (window) + " WIN";
}

juce::String FREQTRAudioProcessorEditor::getStyleText() const
{
    const int style = (int) styleSlider.getValue();
    if (style == 0) return "MONO STYLE";
    if (style == 1) return "STEREO STYLE";
    if (style == 2) return "WIDE STYLE";
    return "DUAL STYLE";
}

juce::String FREQTRAudioProcessorEditor::getStyleTextShort() const
{
    const int style = (int) styleSlider.getValue();
    if (style == 0) return "MONO";
    if (style == 1) return "STEREO";
    if (style == 2) return "WIDE";
    return "DUAL";
}

juce::String FREQTRAudioProcessorEditor::getHarmText() const
{
    const int pct = juce::roundToInt ((float) harmSlider.getValue() * 100.0f);
    return juce::String (pct) + "% HARM";
}

juce::String FREQTRAudioProcessorEditor::getHarmTextShort() const
{
    const int pct = juce::roundToInt ((float) harmSlider.getValue() * 100.0f);
    return juce::String (pct) + "% HRM";
}

juce::String FREQTRAudioProcessorEditor::getSidechainShadowText() const
{
    const int pct = juce::roundToInt ((float) sidechainShadowSlider.getValue() * 100.0f);
    return juce::String (pct) + "% SHADOW";
}

juce::String FREQTRAudioProcessorEditor::getSidechainShadowTextShort() const
{
    const int pct = juce::roundToInt ((float) sidechainShadowSlider.getValue() * 100.0f);
    return juce::String (pct) + "% SHD";
}

juce::String FREQTRAudioProcessorEditor::getPolarityText() const
{
    const float val = (float) polaritySlider.getValue();
    if (val <= -0.995f)
        return "-1 POLARITY";
    if (val >= 0.995f)
        return "+1 POLARITY";
    if (std::abs (val) < 0.005f)
        return "0 POLARITY";
    return juce::String (val, 2) + " POLARITY";
}

juce::String FREQTRAudioProcessorEditor::getPolarityTextShort() const
{
    const float val = (float) polaritySlider.getValue();
    if (val <= -0.995f)
        return "-1 POL";
    if (val >= 0.995f)
        return "+1 POL";
    if (std::abs (val) < 0.005f)
        return "0 POL";
    return juce::String (val, 2) + " POL";
}

juce::String FREQTRAudioProcessorEditor::getMixText() const
{
    if (mixModeCombo.getSelectedId() == 2)
    {
        const bool isDry = (dualMixBar_.getLastTouched() != DualMixBarComponent::WET);
        const float level = isDry ? dualMixBar_.getDryLevel() : dualMixBar_.getWetLevel();
        const float dB = (level <= 0.0001f) ? -100.0f : 20.0f * std::log10 (level);
        const juce::String suffix = isDry ? " DRY" : " WET";
        if (dB <= -100.0f) return "-INF dB" + suffix;
        if (std::abs (dB) < 0.05f) return "0.0 dB" + suffix;
        return juce::String (dB, 1) + " dB" + suffix;
    }
    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);
    return juce::String (pct) + "% MIX";
}

juce::String FREQTRAudioProcessorEditor::getMixTextShort() const
{
    if (mixModeCombo.getSelectedId() == 2)
    {
        const bool isDry = (dualMixBar_.getLastTouched() != DualMixBarComponent::WET);
        const float level = isDry ? dualMixBar_.getDryLevel() : dualMixBar_.getWetLevel();
        const float dB = (level <= 0.0001f) ? -100.0f : 20.0f * std::log10 (level);
        const juce::String suffix = isDry ? " DRY" : " WET";
        if (dB <= -100.0f) return "-INF" + suffix;
        if (std::abs (dB) < 0.05f) return "0.0dB" + suffix;
        return juce::String (dB, 1) + "dB" + suffix;
    }
    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);
    return juce::String (pct) + "% MIX";
}

juce::String FREQTRAudioProcessorEditor::getInputText() const
{
    const float db = (float) inputSlider.getValue();
    return formatGainFaderDb (db) + " INPUT";
}

juce::String FREQTRAudioProcessorEditor::getInputTextShort() const
{
    const float db = (float) inputSlider.getValue();
    return formatGainFaderDb (db) + " IN";
}

juce::String FREQTRAudioProcessorEditor::getOutputText() const
{
    const float db = (float) outputSlider.getValue();
    return formatGainFaderDb (db) + " OUTPUT";
}

juce::String FREQTRAudioProcessorEditor::getOutputTextShort() const
{
    const float db = (float) outputSlider.getValue();
    return formatGainFaderDb (db) + " OUT";
}

juce::String FREQTRAudioProcessorEditor::getTiltText() const
{
    const float db = (float) tiltSlider.getValue();
    if (std::abs (db) < 0.05f)
        return "0.0 dB TILT";
    return juce::String (db, 1) + " dB TILT";
}

juce::String FREQTRAudioProcessorEditor::getTiltTextShort() const
{
    const float db = (float) tiltSlider.getValue();
    if (std::abs (db) < 0.05f)
        return "0.0 dB TLT";
    return juce::String (db, 1) + " dB TLT";
}

juce::String FREQTRAudioProcessorEditor::getPanText() const
{
    const float v = (float) panSlider.getValue();
    const int pct = juce::roundToInt ((v - 0.5f) * 200.0f);
    if (pct == 0) return "C PAN";
    if (pct < 0)  return "L" + juce::String (-pct) + " PAN";
    return "R" + juce::String (pct) + " PAN";
}

juce::String FREQTRAudioProcessorEditor::getPanTextShort() const
{
    const float v = (float) panSlider.getValue();
    const int pct = juce::roundToInt ((v - 0.5f) * 200.0f);
    if (pct == 0) return "C";
    if (pct < 0)  return "L" + juce::String (-pct);
    return "R" + juce::String (pct);
}

bool FREQTRAudioProcessorEditor::refreshLegendTextCache()
{
    const auto oldFreqFull     = cachedFreqTextFull;
    const auto oldFreqShort    = cachedFreqTextShort;
    const auto oldModFull      = cachedModTextFull;
    const auto oldModShort     = cachedModTextShort;
    const auto oldFeedbackFull = cachedFeedbackTextFull;
    const auto oldFeedbackShort = cachedFeedbackTextShort;
    const auto oldJitterFull   = cachedJitterTextFull;
    const auto oldJitterShort  = cachedJitterTextShort;
    const auto oldCombFull     = cachedCombTextFull;
    const auto oldCombShort    = cachedCombTextShort;
    const auto oldEngineFull   = cachedEngineTextFull;
    const auto oldEngineShort  = cachedEngineTextShort;
    const auto oldWindowFull   = cachedWindowTextFull;
    const auto oldWindowShort  = cachedWindowTextShort;
    const auto oldStyleFull    = cachedStyleTextFull;
    const auto oldStyleShort   = cachedStyleTextShort;
    const auto oldHarmFull     = cachedHarmTextFull;
    const auto oldHarmShort    = cachedHarmTextShort;
    const auto oldPolarityFull = cachedPolarityTextFull;
    const auto oldPolarityShort = cachedPolarityTextShort;
    const auto oldMixFull      = cachedMixTextFull;
    const auto oldMixShort     = cachedMixTextShort;
    const auto oldInputFull    = cachedInputTextFull;
    const auto oldInputShort   = cachedInputTextShort;
    const auto oldOutputFull   = cachedOutputTextFull;
    const auto oldOutputShort  = cachedOutputTextShort;
    const auto oldTiltFull     = cachedTiltTextFull;
    const auto oldTiltShort    = cachedTiltTextShort;
    const auto oldPanFull      = cachedPanTextFull;
    const auto oldPanShort     = cachedPanTextShort;
    const auto oldLimFull      = cachedLimThresholdTextFull;
    const auto oldLimShort     = cachedLimThresholdTextShort;

    cachedFreqTextFull      = getFreqText();
    cachedFreqTextShort     = getFreqTextShort();
    cachedModTextFull       = getModText();
    cachedModTextShort      = getModTextShort();
    cachedFeedbackTextFull  = getFeedbackText();
    cachedFeedbackTextShort = getFeedbackTextShort();
    cachedJitterTextFull    = getJitterText();
    cachedJitterTextShort   = getJitterTextShort();
    cachedCombTextFull      = getCombText();
    cachedCombTextShort     = getCombTextShort();
    cachedEngineTextFull    = getEngineText();
    cachedEngineTextShort   = getEngineTextShort();
    cachedWindowTextFull    = getWindowText();
    cachedWindowTextShort   = getWindowTextShort();
    cachedStyleTextFull     = getStyleText();
    cachedStyleTextShort    = getStyleTextShort();
    cachedHarmTextFull      = getHarmText();
    cachedHarmTextShort     = getHarmTextShort();
    cachedPolarityTextFull  = getPolarityText();
    cachedPolarityTextShort = getPolarityTextShort();
    cachedMixTextFull       = getMixText();
    cachedMixTextShort      = getMixTextShort();

    cachedInputTextFull = getInputText();
    cachedInputTextShort = getInputTextShort();
    {
        const float inDb = (float) inputSlider.getValue();
        cachedInputIntOnly = formatGainFaderDbCompact (inDb);
    }

    cachedOutputTextFull = getOutputText();
    cachedOutputTextShort = getOutputTextShort();
    {
        const float outDb = (float) outputSlider.getValue();
        cachedOutputIntOnly = formatGainFaderDbCompact (outDb);
    }

    cachedTiltTextFull = getTiltText();
    cachedTiltTextShort = getTiltTextShort();
    {
        const float tDb = (float) tiltSlider.getValue();
        if (std::abs (tDb) < 0.05f)
            cachedTiltIntOnly = "0.0dB";
        else
            cachedTiltIntOnly = juce::String (tDb, 1) + "dB";
    }

    if (mixModeCombo.getSelectedId() == 2)
    {
        const bool isDry = (dualMixBar_.getLastTouched() != DualMixBarComponent::WET);
        const float level = isDry ? dualMixBar_.getDryLevel() : dualMixBar_.getWetLevel();
        const float dB = (level <= 0.0001f) ? -100.0f : 20.0f * std::log10 (level);
        const juce::String suffix = isDry ? " DRY" : " WET";
        if (dB <= -100.0f) cachedMixIntOnly = "-INF" + suffix;
        else if (std::abs (dB) < 0.05f) cachedMixIntOnly = "0.0dB" + suffix;
        else cachedMixIntOnly = juce::String (dB, 1) + "dB" + suffix;
    }
    else
    {
        cachedMixIntOnly = juce::String ((int) std::lround (mixSlider.getValue() * 100.0)) + "%";
    }
    if (cachedMidiDisplay.isNotEmpty() && ! freqSlider.isMouseButtonDown())
        cachedFreqIntOnly = cachedMidiDisplay;
    else if (syncButton.getToggleState())
        cachedFreqIntOnly = audioProcessor.getFreqSyncName ((int) freqSlider.getValue());
    else
        cachedFreqIntOnly = formatInlineFrequency ((float) freqSlider.getValue());
    cachedModIntOnly      = isModHarmEnabled (audioProcessor) ? formatModHarmText (modSlider.getValue(), false) : juce::String ((int) modSlider.getValue());
    cachedFeedbackIntOnly = juce::String ((int) std::lround (feedbackSlider.getValue() * 100.0)) + "%";
    cachedJitterIntOnly   = juce::String ((int) std::lround (jitterSlider.getValue() * 100.0)) + "%";
    {
        const float chz = (float) combSlider.getValue();
        cachedCombIntOnly = formatInlineFrequency (chz);
    }
    cachedEngineIntOnly   = juce::String ((int) std::lround (engineSlider.getValue() * 100.0)) + "%";
    cachedWindowIntOnly   = juce::String (FREQTRAudioProcessor::getCanonicalHilbertWindow ((int) std::lround (windowSlider.getValue())));
    cachedHarmIntOnly     = getHarmTextShort();
    cachedPolarityIntOnly = juce::String (polaritySlider.getValue(), 1);
    cachedStyleIntOnly    = juce::String ((int) styleSlider.getValue());

    cachedFilterTextFull  = "FILTER";
    cachedFilterTextShort = "FLTR";

    cachedPanTextFull  = getPanText();
    cachedPanTextShort = getPanTextShort();

    {
        const float limDb = (float) limThresholdSlider.getValue();
        const auto limText = juce::String (std::abs (limDb) < 0.05f ? 0.0f : limDb, 1);
        if (limDb >= -0.05f)
        {
            cachedLimThresholdTextFull  = "0.0 dB LIM";
            cachedLimThresholdTextShort = "0.0 dB LIM";
            cachedLimThresholdIntOnly   = "0.0dB";
        }
        else
        {
            cachedLimThresholdTextFull  = limText + " dB LIM";
            cachedLimThresholdTextShort = limText + " dB LIM";
            cachedLimThresholdIntOnly   = limText + "dB";
        }
    }

    auto setLabelOnly = [] (juce::String& full, juce::String& shortText, juce::String& intOnly,
                             const char* label, const char* shortLabel = nullptr)
    {
        full = label;
        shortText = shortLabel != nullptr ? shortLabel : label;
        intOnly = shortText;
    };

    const bool sidechainOn = sidechainButton.getToggleState();
    const bool freqShiftActive = (engineSlider.getValue() > 0.5);

    if (sidechainOn)
    {
        cachedHarmTextFull = getSidechainShadowText();
        cachedHarmTextShort = getSidechainShadowTextShort();
        cachedHarmIntOnly = cachedHarmTextShort;
    }

    if (! freqShiftActive)
    {
        setLabelOnly (cachedWindowTextFull, cachedWindowTextShort, cachedWindowIntOnly, "WINDOW", "WIN");
    }

    return oldFreqFull      != cachedFreqTextFull
        || oldFreqShort     != cachedFreqTextShort
        || oldModFull       != cachedModTextFull
        || oldModShort      != cachedModTextShort
        || oldFeedbackFull  != cachedFeedbackTextFull
        || oldFeedbackShort != cachedFeedbackTextShort
        || oldJitterFull    != cachedJitterTextFull
        || oldJitterShort   != cachedJitterTextShort
        || oldCombFull      != cachedCombTextFull
        || oldCombShort     != cachedCombTextShort
        || oldEngineFull    != cachedEngineTextFull
        || oldEngineShort   != cachedEngineTextShort
        || oldWindowFull    != cachedWindowTextFull
        || oldWindowShort   != cachedWindowTextShort
        || oldStyleFull     != cachedStyleTextFull
        || oldStyleShort    != cachedStyleTextShort
        || oldHarmFull      != cachedHarmTextFull
        || oldHarmShort     != cachedHarmTextShort
        || oldPolarityFull  != cachedPolarityTextFull
        || oldPolarityShort != cachedPolarityTextShort
        || oldMixFull       != cachedMixTextFull
        || oldMixShort      != cachedMixTextShort
        || oldInputFull     != cachedInputTextFull
        || oldInputShort    != cachedInputTextShort
        || oldOutputFull    != cachedOutputTextFull
        || oldOutputShort   != cachedOutputTextShort
        || oldTiltFull      != cachedTiltTextFull
        || oldTiltShort     != cachedTiltTextShort
        || oldPanFull       != cachedPanTextFull
        || oldPanShort      != cachedPanTextShort
        || oldLimFull       != cachedLimThresholdTextFull
        || oldLimShort      != cachedLimThresholdTextShort;
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getRowRepaintBounds (const juce::Slider& s) const
{
    auto bounds = s.getBounds().getUnion (getValueAreaFor (s.getBounds()));
    return bounds.expanded (8, 8).getIntersection (getLocalBounds());
}

void FREQTRAudioProcessorEditor::setupBar (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearBar);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setPopupDisplayEnabled (false, false, this);
    s.setTooltip (juce::String());
    s.setPopupMenuEnabled (false);

    TR::applySimpleTransparentSliderColours (s, activeScheme);
}

//========================== Sync mode ==========================

void FREQTRAudioProcessorEditor::updateFreqSliderForSyncMode()
{
    const bool syncEnabled = syncButton.getToggleState();

    if (syncEnabled)
    {
        // Destroy Hz attachment and create SYNC attachment
        freqAttachment.reset();
        freqSyncAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts,
                                                                  FREQTRAudioProcessor::kParamFreqSync,
                                                                  freqSlider);
        freqSlider.setRange ((double) FREQTRAudioProcessor::kFreqSyncMin,
                             (double) FREQTRAudioProcessor::kFreqSyncMax,
                             1.0);
        freqSlider.setDoubleClickReturnValue (true, (double) FREQTRAudioProcessor::kFreqSyncDefault);
        freqSlider.setSkewFactor (1.0);
    }
    else
    {
        // Destroy SYNC attachment and create Hz attachment
        freqSyncAttachment.reset();
        freqAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts,
                                                              FREQTRAudioProcessor::kParamFreq,
                                                              freqSlider);
        freqSlider.setRange ((double) FREQTRAudioProcessor::kFreqMin,
                             (double) FREQTRAudioProcessor::kFreqMax,
                             0.0);
        freqSlider.setDoubleClickReturnValue (true, (double) FREQTRAudioProcessor::kFreqDefault);
        freqSlider.setSkewFactor ((double) FREQTRAudioProcessor::kFreqSkew);
    }

    refreshLegendTextCache();
    repaint();
}


void FREQTRAudioProcessorEditor::openEngineBiasPrompt()
{
    lnf.setScheme (activeScheme);

    std::vector<TR::SimpleRowsPromptContent::Row> rows;

    rows.push_back ({
        "BIAS",
        "%",
        FREQTRAudioProcessor::kEngineBiasMin,
        FREQTRAudioProcessor::kEngineBiasMax,
        TR::getParameterPlain (audioProcessor.apvts,
                               FREQTRAudioProcessor::kParamEngineBias,
                               FREQTRAudioProcessor::kEngineBiasDefault),
        FREQTRAudioProcessor::kEngineBiasDefault,
        1,
        [this] (float value)
        {
            TR::setParameterPlain (audioProcessor.apvts,
                                   FREQTRAudioProcessor::kParamEngineBias,
                                   juce::jlimit (FREQTRAudioProcessor::kEngineBiasMin,
                                                 FREQTRAudioProcessor::kEngineBiasMax,
                                                 value));
        }
    });

    rows.push_back ({
        "FOCUS",
        "%",
        FREQTRAudioProcessor::kEngineFocusMin,
        FREQTRAudioProcessor::kEngineFocusMax,
        TR::getParameterPlain (audioProcessor.apvts,
                               FREQTRAudioProcessor::kParamEngineFocus,
                               FREQTRAudioProcessor::kEngineFocusDefault),
        FREQTRAudioProcessor::kEngineFocusDefault,
        1,
        [this] (float value)
        {
            TR::setParameterPlain (audioProcessor.apvts,
                                   FREQTRAudioProcessor::kParamEngineFocus,
                                   juce::jlimit (FREQTRAudioProcessor::kEngineFocusMin,
                                                 FREQTRAudioProcessor::kEngineFocusMax,
                                                 value));
        }
    });

    TR::openRowsPromptShared (this, lnf, activeScheme, "ENGINE", std::move (rows));
}

//========================== Popup helper classes ==========================

using PopupSwatchButton = TR::PopupSwatchButton;

using PopupClickableLabel = TR::PopupClickableLabel;

//========================== Popup static layout helpers ==========================

//========================== Right-click numeric popup (stub) ==========================

void FREQTRAudioProcessorEditor::openNumericEntryPopupForSlider (juce::Slider& s)
{
    if (&s == &windowSlider || &s == &styleSlider)
        return;

    lnf.setScheme (activeScheme);

    const bool isFreqSyncMode = (&s == &freqSlider && syncButton.getToggleState());
    const bool isModHarmPrompt = (&s == &modSlider && isModHarmEnabled (audioProcessor));

    TR::NumericEntryPromptSpec spec;

    if (&s == &freqSlider)
    {
        if (! isFreqSyncMode) { spec.suffix = " Hz"; spec.suffixShort = " Hz"; }
    }
    else if (&s == &modSlider)       { if (! isModHarmPrompt) spec.prefix = "X"; spec.suffix = " MOD"; spec.suffixShort = " MOD"; }
    else if (&s == &feedbackSlider)  { spec.suffix = " % FBK";      spec.suffixShort = " % FBK"; }
    else if (&s == &jitterSlider)    { spec.suffix = " % JITTER";   spec.suffixShort = " % JIT"; }
    else if (&s == &combSlider)      { spec.suffix = " Hz";         spec.suffixShort = " Hz"; }
    else if (&s == &engineSlider)    { spec.suffix = " % ENGINE";   spec.suffixShort = " % ENG"; }
    else if (&s == &harmSlider)      { spec.suffix = " % HARM";     spec.suffixShort = " % HARM"; }
    else if (&s == &sidechainShadowSlider) { spec.suffix = " % SHADOW"; spec.suffixShort = " % SHD"; }
    else if (&s == &polaritySlider)  { spec.suffix = " POLARITY";   spec.suffixShort = " POL"; }
    else if (&s == &mixSlider)       { spec.suffix = " % MIX";      spec.suffixShort = " % MIX"; }
    else if (&s == &panSlider)       { spec.suffix = " % PAN";      spec.suffixShort = " % PAN"; }
    else if (&s == &inputSlider)     { spec.suffix = " dB INPUT";   spec.suffixShort = " dB IN"; }
    else if (&s == &outputSlider)    { spec.suffix = " dB OUTPUT";  spec.suffixShort = " dB OUT"; }
    else if (&s == &tiltSlider)      { spec.suffix = " dB TILT";    spec.suffixShort = " dB TILT"; }
    else if (&s == &limThresholdSlider) { spec.suffix = " dB LIM";  spec.suffixShort = " dB LIM"; }

    if (&s == &modSlider)
        spec.currentDisplay = isModHarmPrompt ? formatModHarmText (s.getValue(), false)
                                              : juce::String (modSliderToMultiplier (s.getValue()), 2);
    else if (&s == &engineSlider || &s == &feedbackSlider || &s == &mixSlider)
        spec.currentDisplay = juce::String (s.getValue() * 100.0, 2);
    else if (&s == &jitterSlider)
        spec.currentDisplay = juce::String (juce::jlimit (0.0, 100.0, s.getValue() * 100.0), 2);
    else if (&s == &panSlider)
        spec.currentDisplay = juce::String (juce::jlimit (0.0, 100.0, s.getValue() * 100.0), 0);
    else if (&s == &freqSlider && ! isFreqSyncMode)
        spec.currentDisplay = juce::String (s.getValue(), 3);
    else if (&s == &combSlider)
        spec.currentDisplay = juce::String (s.getValue(), 3);
    else
        spec.currentDisplay = s.getTextFromValue (s.getValue());

    if (&s == &freqSlider)
    {
        if (isFreqSyncMode)
        {
            spec.minValue = 0.0; spec.maxValue = (double) FREQTRAudioProcessor::kFreqSyncMax;
            spec.maxDecimals = 0; spec.maxLength = 6; spec.worstCaseText = "1/64T.";
            spec.inputKind = TR::NumericEntryPromptInputKind::SyncDivision;
        }
        else { spec.minValue = 0.0; spec.maxValue = 5000.0; spec.maxDecimals = 3; spec.maxLength = 8; spec.worstCaseText = "5000.000"; }
    }
    else if (&s == &modSlider)
    {
        if (isModHarmPrompt) { spec.minValue = -8.0; spec.maxValue = 8.0; spec.maxDecimals = 0; spec.maxLength = 4; spec.worstCaseText = "H+8"; spec.inputKind = TR::NumericEntryPromptInputKind::HarmonicStep; }
        else { spec.minValue = 0.0; spec.maxValue = 4.0; spec.maxDecimals = 2; spec.maxLength = 4; spec.worstCaseText = "4.00"; }
    }
    else if (&s == &combSlider) { spec.minValue = FREQTRAudioProcessor::kCombMin; spec.maxValue = FREQTRAudioProcessor::kCombMax; spec.maxDecimals = 3; spec.maxLength = 8; spec.worstCaseText = "5000.000"; }
    else if (&s == &feedbackSlider || &s == &jitterSlider || &s == &engineSlider || &s == &harmSlider || &s == &mixSlider) { spec.minValue = 0.0; spec.maxValue = 100.0; spec.maxDecimals = 2; spec.maxLength = 6; spec.worstCaseText = "100.00"; }
    else if (&s == &polaritySlider) { spec.minValue = -1.0; spec.maxValue = 1.0; spec.maxDecimals = 2; spec.maxLength = 5; spec.worstCaseText = "-1.00"; }
    else if (&s == &inputSlider || &s == &outputSlider) { spec.minValue = FREQTRAudioProcessor::kGainFloorDb; spec.maxValue = FREQTRAudioProcessor::kGainMaxDb; spec.maxDecimals = 1; spec.maxLength = 6; spec.worstCaseText = "-144.0"; }
    else if (&s == &tiltSlider) { spec.minValue = FREQTRAudioProcessor::kTiltMin; spec.maxValue = FREQTRAudioProcessor::kTiltMax; spec.maxDecimals = 1; spec.maxLength = 4; spec.worstCaseText = "-6.0"; }
    else if (&s == &limThresholdSlider) { spec.minValue = FREQTRAudioProcessor::kLimThresholdMin; spec.maxValue = FREQTRAudioProcessor::kLimThresholdMax; spec.maxDecimals = 1; spec.maxLength = 5; spec.worstCaseText = "-36.0"; }
    else if (&s == &panSlider) { spec.minValue = 0.0; spec.maxValue = 100.0; spec.maxDecimals = 0; spec.maxLength = 3; spec.worstCaseText = "100"; }

    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
    juce::Slider* sliderPtr = &s;
    spec.onAccept = [safeThis, sliderPtr] (const juce::String& txt)
    {
        if (safeThis == nullptr || sliderPtr == nullptr)
            return;

        auto normalised = txt.replaceCharacter (',', '.');
        double v = 0.0;

        if (sliderPtr == &safeThis->freqSlider && safeThis->syncButton.getToggleState())
        {
            int foundIndex = -1;
            auto choices = FREQTRAudioProcessor::getFreqSyncChoices();
            for (int i = 0; i < choices.size(); ++i)
                if (txt.equalsIgnoreCase (choices[i]) || txt.equalsIgnoreCase (choices[i].replace ("/", ""))) { foundIndex = i; break; }

            if (foundIndex < 0)
            {
                juce::String t = normalised.trimStart();
                while (t.startsWithChar ('+')) t = t.substring (1).trimStart();
                foundIndex = t.initialSectionContainingOnly ("0123456789").getIntValue();
            }

            v = (double) juce::jlimit (FREQTRAudioProcessor::kFreqSyncMin, FREQTRAudioProcessor::kFreqSyncMax, foundIndex);
        }
        else
        {
            juce::String t = normalised.trimStart();
            while (t.startsWithChar ('+')) t = t.substring (1).trimStart();
            v = t.initialSectionContainingOnly ("0123456789.,-").getDoubleValue();

            if (sliderPtr == &safeThis->engineSlider || sliderPtr == &safeThis->harmSlider || sliderPtr == &safeThis->sidechainShadowSlider
                || sliderPtr == &safeThis->mixSlider || sliderPtr == &safeThis->feedbackSlider || sliderPtr == &safeThis->jitterSlider || sliderPtr == &safeThis->panSlider)
                v *= 0.01;

            if (sliderPtr == &safeThis->modSlider)
            {
                if (isModHarmEnabled (safeThis->audioProcessor))
                {
                    juce::String h = normalised.trim().toUpperCase();
                    if (h.startsWithChar ('H')) h = h.substring (1).trimStart();
                    while (h.startsWithChar ('+')) h = h.substring (1).trimStart();
                    const int step = juce::jlimit (-8, 8, h.getIntValue());
                    v = ((double) step + 8.0) / 16.0;
                }
                else v = multiplierToModSlider (v);
            }
        }

        const auto range = sliderPtr->getRange();
        double clamped = juce::jlimit (range.getStart(), range.getEnd(), v);
        if (sliderPtr == &safeThis->freqSlider && ! safeThis->syncButton.getToggleState())
            clamped = roundToDecimals (clamped, 3);
        sliderPtr->setValue (clamped, juce::sendNotificationSync);
    };

    TR::openNumericEntryPopupShared (this, lnf, activeScheme, spec);
}

// â”€â”€ Filter Prompt (HP/LP frequency + slope) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void FREQTRAudioProcessorEditor::openFilterPrompt()
{
    lnf.setScheme (activeScheme);
    auto& vts = audioProcessor.apvts;

    FilterPromptSpec spec;
    spec.hpParam = FREQTRAudioProcessor::kParamFilterHpFreq;
    spec.lpParam = FREQTRAudioProcessor::kParamFilterLpFreq;
    spec.hpOnParam = FREQTRAudioProcessor::kParamFilterHpOn;
    spec.lpOnParam = FREQTRAudioProcessor::kParamFilterLpOn;
    spec.hpSlopeParam = FREQTRAudioProcessor::kParamFilterHpSlope;
    spec.lpSlopeParam = FREQTRAudioProcessor::kParamFilterLpSlope;
    spec.freqMin = 20.0f;
    spec.freqMax = 20000.0f;
    spec.hpDefault = FREQTRAudioProcessor::kFilterHpFreqDefault;
    spec.lpDefault = FREQTRAudioProcessor::kFilterLpFreqDefault;
    spec.slopeMin = FREQTRAudioProcessor::kFilterSlopeMin;
    spec.slopeMax = FREQTRAudioProcessor::kFilterSlopeMax;
    spec.refreshFilterDisplay = [this] { filterBar_.updateFromProcessor(); };

    openFilterPromptShared (this, lnf, activeScheme, vts, spec);
}


void FREQTRAudioProcessorEditor::openRetrigPrompt()
{
    TR::toggleSimpleBoolParameterWithPersistentTooltip (audioProcessor.apvts,
        FREQTRAudioProcessor::kParamRetrig,
        retrigDisplay,
        tooltipWindow.get(),
        [] (bool enabled) { return formatRetrigTooltip (enabled); });
}

void FREQTRAudioProcessorEditor::openPdcMaxWindowPrompt()
{
    lnf.setScheme (activeScheme);

    const int currentMaxWindow = getCurrentMaxHilbertWindow();
    const int currentPluginWindow = FREQTRAudioProcessor::getCanonicalHilbertWindow ((int) std::lround (
        audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamWindow)->load()));

    auto windowToBar = [] (int window) noexcept
    {
        const int lane = FREQTRAudioProcessor::getHilbertWindowLane (window);
        return (float) lane / (float) (FREQTRAudioProcessor::kNumHilbertWindows - 1);
    };

    auto barToWindow = [] (float value01) noexcept
    {
        const int lane = juce::jlimit (0, FREQTRAudioProcessor::kNumHilbertWindows - 1,
            (int) std::lround (juce::jlimit (0.0f, 1.0f, value01)
                * (float) (FREQTRAudioProcessor::kNumHilbertWindows - 1)));
        return FREQTRAudioProcessor::kHilbertWindows[lane];
    };

    auto applyMaxWindow = [this] (int window)
    {
        const int canonical = FREQTRAudioProcessor::getCanonicalHilbertWindow (window);
        if (auto* p = audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamMaxWindow))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) canonical));
        syncWindowToMax (true);
        updatePdcTooltip();
    };

    TR::openIntegerBarPromptShared<FREQTRAudioProcessorEditor> (
        this, lnf, activeScheme, "maxwin", "MAX WIN", {},
        currentMaxWindow, FREQTRAudioProcessor::kHilbertMaxWindowDefault,
        0, FREQTRAudioProcessor::kHilbertWindowMax, 4,
        windowToBar,
        barToWindow,
        [] (int value) { return FREQTRAudioProcessor::getCanonicalHilbertWindow (value); },
        applyMaxWindow,
        [this, currentMaxWindow, currentPluginWindow]
        {
            if (auto* p = audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamMaxWindow))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) currentMaxWindow));
            if (auto* p = audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamWindow))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jmin (currentPluginWindow, currentMaxWindow)));
            syncWindowToMax (true);
            updatePdcTooltip();
        },
        [applyMaxWindow] (int value) { applyMaxWindow (value); });
}


void FREQTRAudioProcessorEditor::openSidechainPrompt()
{
    lnf.setScheme (activeScheme);
    auto& vts = audioProcessor.apvts;

    SidechainPromptSpec spec;
    spec.gainParam = FREQTRAudioProcessor::kParamSidechainGain;
    spec.smoothParam = FREQTRAudioProcessor::kParamSidechainSmooth;
    spec.hpParam = FREQTRAudioProcessor::kParamSidechainHp;
    spec.lpParam = FREQTRAudioProcessor::kParamSidechainLp;
    spec.hpOnParam = FREQTRAudioProcessor::kParamSidechainHpOn;
    spec.lpOnParam = FREQTRAudioProcessor::kParamSidechainLpOn;
    spec.hpSlopeParam = FREQTRAudioProcessor::kParamSidechainHpSlope;
    spec.lpSlopeParam = FREQTRAudioProcessor::kParamSidechainLpSlope;

    spec.gainMin = FREQTRAudioProcessor::kSidechainGainMin;
    spec.gainMax = FREQTRAudioProcessor::kSidechainGainMax;
    spec.gainDefault = FREQTRAudioProcessor::kSidechainGainDefault;
    spec.gainSkew = FREQTRAudioProcessor::kGainSkew;
    spec.smoothMin = FREQTRAudioProcessor::kSidechainSmoothMin;
    spec.smoothMax = FREQTRAudioProcessor::kSidechainSmoothMax;
    spec.smoothDefault = FREQTRAudioProcessor::kSidechainSmoothDefault;
    spec.freqMin = FREQTRAudioProcessor::kSidechainFilterFreqMin;
    spec.freqMax = FREQTRAudioProcessor::kSidechainFilterFreqMax;
    spec.hpDefault = FREQTRAudioProcessor::kSidechainHpDefault;
    spec.lpDefault = FREQTRAudioProcessor::kSidechainLpDefault;
    spec.slopeMin = FREQTRAudioProcessor::kFilterSlopeMin;
    spec.slopeMax = FREQTRAudioProcessor::kFilterSlopeMax;

    spec.refreshTooltip = [this]
    {
        auto& state = audioProcessor.apvts;
        sidechainDisplay.setTooltip (formatSidechainTooltip (
            state.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainGain)->load(),
            state.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainSmooth)->load(),
            state.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainHpOn)->load() > 0.5f,
            state.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainHp)->load(),
            (int) std::lround (state.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainHpSlope)->load()),
            state.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainLpOn)->load() > 0.5f,
            state.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainLp)->load(),
            (int) std::lround (state.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainLpSlope)->load())));
    };

    openSidechainPromptShared (this, lnf, activeScheme, vts, spec);
}


void FREQTRAudioProcessorEditor::openMidiChannelPrompt()
{
    TR::openMidiChannelDelayPromptShared<FREQTRAudioProcessorEditor> (this,
                                                   lnf,
                                                   activeScheme,
                                                   [this]() { return audioProcessor.getMidiChannel(); },
                                                   [this] (int ch) { audioProcessor.setMidiChannel (ch); },
                                                   [this]() { return audioProcessor.getMidiDelayMs(); },
                                                   [this] (int delayMs) { audioProcessor.setMidiDelayMs (delayMs); },
                                                   [this] (int ch, int delayMs)
                                                   {
                                                       midiChannelDisplay.setTooltip (formatMidiChannelTooltip (ch, delayMs));
                                                   });
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• Chaos prompts â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

void FREQTRAudioProcessorEditor::openChaosConfigPrompt (const char* amtParamId,
                                                 const char* spdParamId,
                                                 const juce::String& title)
{
    auto& vts = audioProcessor.apvts;
    const bool isFilterChaos = title == "CHSF";
    const TR::SimpleChaosPromptBinding binding {
        amtParamId,
        spdParamId,
        isFilterChaos ? vts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosAmtFilter)->load()
                      : vts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosAmt)->load(),
        isFilterChaos ? vts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosSpdFilter)->load()
                      : vts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosSpd)->load()
    };

    TR::openSimpleChaosPromptAction<FREQTRAudioProcessorEditor> (this,
                                            lnf,
                                            activeScheme,
                                            vts,
                                            binding,
                                            [this, isFilterChaos, amtParamId, spdParamId]
                                            {
                                                const auto amt = audioProcessor.apvts.getRawParameterValue (amtParamId)->load();
                                                const auto spd = audioProcessor.apvts.getRawParameterValue (spdParamId)->load();
                                                const auto tip = formatChaosTooltip (amt, spd);
                                                if (isFilterChaos)
                                                    chaosFilterDisplay.setTooltip (tip);
                                                else
                                                    chaosDelayDisplay.setTooltip (tip);
                                                repaint();
                                            });
}

void FREQTRAudioProcessorEditor::openChaosFilterPrompt()
{
    TR::openSimpleChaosSelectorPromptAction (
        [this] (const char* amountParamId, const char* speedParamId, const juce::String& title)
        {
            openChaosConfigPrompt (amountParamId, speedParamId, title);
        },
        FREQTRAudioProcessor::kParamChaosAmtFilter,
        FREQTRAudioProcessor::kParamChaosSpdFilter,
        true);
}

void FREQTRAudioProcessorEditor::openChaosDelayPrompt()
{
    TR::openSimpleChaosSelectorPromptAction (
        [this] (const char* amountParamId, const char* speedParamId, const juce::String& title)
        {
            openChaosConfigPrompt (amountParamId, speedParamId, title);
        },
        FREQTRAudioProcessor::kParamChaosAmt,
        FREQTRAudioProcessor::kParamChaosSpd,
        false);
}

void FREQTRAudioProcessorEditor::openMixSendPrompt()
{
    TR::openMixSendPromptShared<FREQTRAudioProcessorEditor> (this,
                                          lnf,
                                          activeScheme,
                                          audioProcessor.apvts,
                                          FREQTRAudioProcessor::kParamDryLevel,
                                          FREQTRAudioProcessor::kParamWetLevel,
                                          FREQTRAudioProcessor::kDryLevelDefault,
                                          FREQTRAudioProcessor::kWetLevelDefault,
                                          [this]() { dualMixBar_.updateFromProcessor(); });
}

void FREQTRAudioProcessorEditor::openInfoPopup()
{
    lnf.setScheme (activeScheme);
    TR::openInfoPopupFromXmlShared<FREQTRAudioProcessorEditor> (this,
                                           lnf,
                                           activeScheme,
                                           InfoContent::xml,
                                           [this]() { openGraphicsPopup(); });
}


void FREQTRAudioProcessorEditor::openGraphicsPopup()
{
    lnf.setScheme (activeScheme);
    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    crtEnabled = false;
    ioFxEnabled = audioProcessor.getUiIoFxEnabled();
    crtEffect.setEnabled (false);
    applyActivePalette();

    TR::openGraphicsPopupShared<FREQTRAudioProcessorEditor> (this,
                                        lnf,
                                        activeScheme,
                                        defaultPalette,
                                        customPalette,
                                        useCustomPalette,
                                        ioFxEnabled,
                                        [this] (bool enabled)
                                        {
                                            useCustomPalette = enabled;
                                            audioProcessor.setUiUseCustomPalette (enabled);
                                        },
                                        [this] (int index, juce::Colour colour)
                                        {
                                            customPalette[(size_t) index] = colour;
                                            audioProcessor.setUiCustomPaletteColour (index, colour);
                                        },
                                        [this] (bool enabled)
                                        {
                                            applyIoFxState (enabled);
                                            audioProcessor.setUiIoFxEnabled (ioFxEnabled);
                                        },
                                        [this]()
                                        {
                                            applyActivePalette();
                                            updateIoFxMeterSliders();
                                            repaint();
                                        });
}


//========================== Mouse interactions ==========================
void FREQTRAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    const auto pt = e.getEventRelativeTo (this).getPosition();

    // Title hit area (FREQ-specific: toggle freq shift hilbert mode)
    if (getTitleHitArea().contains (pt))
    {
        if (e.mods.isPopupMenu())
        {
            audioProcessor.toggleFreqShiftHilbertMode();
            repaint (getTitleHitArea().expanded (4));
        }

        showFreqShiftHilbertModeTooltip();
        juce::Timer::callAfterDelay (80,
            [safeThis = juce::Component::SafePointer<FREQTRAudioProcessorEditor> (this)]()
            {
                if (safeThis == nullptr)
                    return;

                const auto localMouse = safeThis->getLocalPoint (nullptr, juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt());
                if (safeThis->getTitleHitArea().contains (localMouse))
                    safeThis->showFreqShiftHilbertModeTooltip();
            });
        return;
    }

    // Engine slider right-click (FREQ-specific: open engine bias prompt)
    if (e.mods.isPopupMenu()
        && engineSlider.isVisible()
        && engineSlider.isEnabled()
        && (getValueAreaFor (engineSlider.getBounds()).contains (pt)
))
    {
        openEngineBiasPrompt();
        return;
    }

    if (TR::SimpleMouseRouter::routeMouseDown (*this, e, pt,
            cachedToggleBarArea_, ioSectionExpanded_,
            modSlider, getValueAreaFor (modSlider.getBounds()),
            [this] { return isModHarmEnabled (audioProcessor); },
            [this] (bool v) { setModHarmEnabled (audioProcessor, v); },
            [this] (bool v) { return formatModHarmTooltip (v); },
            [this] {
                if (refreshLegendTextCache()) updateCachedLayout();
            },
            filterBar_, cachedFilterValueArea_,
            [this] { openFilterPrompt(); },
            [this] (juce::Point<int> p) { return getSliderForValueAreaPoint (p); },
            [this] (juce::Slider& s) { openNumericEntryPopupForSlider (s); },
            getInfoIconArea(), crtEnabled,
            [this] { openInfoPopup(); },
            {
                TR::SimpleMouseRouter::ToggleBinding { &syncButton,       getSyncLabelArea(),       &retrigDisplay,       [this] { openRetrigPrompt(); },          true },
                TR::SimpleMouseRouter::ToggleBinding { &midiButton,       getMidiLabelArea(),       &midiChannelDisplay,  [this] { openMidiChannelPrompt(); },    true },
                TR::SimpleMouseRouter::ToggleBinding { &alignButton,      getAlignLabelArea(),      nullptr,              {},                                      false },
                TR::SimpleMouseRouter::ToggleBinding { &pdcButton,        getPdcLabelArea(),        &pdcDisplay,          [this] { openPdcMaxWindowPrompt(); } },
                TR::SimpleMouseRouter::ToggleBinding { &sidechainButton,  getSidechainLabelArea(),  &sidechainDisplay,    [this] { openSidechainPrompt(); } },
                TR::SimpleMouseRouter::ToggleBinding { &chaosFilterButton,getChaosFilterLabelArea(),&chaosFilterDisplay,  [this] { openChaosFilterPrompt(); } },
                TR::SimpleMouseRouter::ToggleBinding { &chaosDelayButton, getChaosDelayLabelArea(), &chaosDelayDisplay,   [this] { openChaosDelayPrompt(); } },
            }))
        return;
}

void FREQTRAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
    TR::SimpleMouseRouter::routeMouseDoubleClick (*this, e.getPosition(),
        [this] (juce::Point<int> p) { return getSliderForValueAreaPoint (p); },
        {
            { &freqSlider,          (double) FREQTRAudioProcessor::kFreqDefault },
            { &modSlider,           (double) FREQTRAudioProcessor::kModDefault },
            { &combSlider,          (double) FREQTRAudioProcessor::kCombDefault },
            { &feedbackSlider,      (double) FREQTRAudioProcessor::kFeedbackDefault },
            { &jitterSlider,        (double) FREQTRAudioProcessor::kJitterDefault },
            { &engineSlider,        (double) FREQTRAudioProcessor::kEngineDefault },
            { &windowSlider,        (double) FREQTRAudioProcessor::kHilbertWindowDefault },
            { &styleSlider,         (double) FREQTRAudioProcessor::kStyleDefault },
            { &harmSlider,          (double) FREQTRAudioProcessor::kHarmDefault },
            { &sidechainShadowSlider, (double) FREQTRAudioProcessor::kSidechainShadowDefault },
            { &polaritySlider,      kDefaultPolarity },
            { &mixSlider,           (double) FREQTRAudioProcessor::kMixDefault },
            { &inputSlider,         (double) FREQTRAudioProcessor::kInputDefault },
            { &outputSlider,        (double) FREQTRAudioProcessor::kOutputDefault },
            { &tiltSlider,          (double) FREQTRAudioProcessor::kTiltDefault },
            { &panSlider,           (double) FREQTRAudioProcessor::kPanDefault },
            { &limThresholdSlider,  kDefaultLimThreshold },
        });
}



void FREQTRAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
}

void FREQTRAudioProcessorEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto pt = e.getEventRelativeTo (this).getPosition();
    TR::routeSimpleHoverTooltip (*this, tooltipWindow.get(), pt,
    {
        { modSlider.isVisible() ? getValueAreaFor (modSlider.getBounds()) : juce::Rectangle<int>(),
          formatModHarmTooltip (isModHarmEnabled (audioProcessor)) },
        { getTitleHitArea(), getFreqShiftHilbertModeTooltip() }
    });
}

void FREQTRAudioProcessorEditor::mouseExit (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);
    TR::clearSimpleHoverTooltip (*this, tooltipWindow.get());
}

void FREQTRAudioProcessorEditor::showFreqShiftHilbertModeTooltip()
{
    const auto tip = getFreqShiftHilbertModeTooltip();
    setTooltip (tip);

    TR::showSimplePersistentTooltip (*this, tooltipWindow.get(), getTitleHitArea(), tip);
}

//========================== Layout builders ==========================

namespace
{
    constexpr int kResizerCornerPx = 22;
    constexpr int kToggleBoxPx = 72;
    constexpr int kNumBars = 14;
}

FREQTRAudioProcessorEditor::HorizontalLayoutMetrics
FREQTRAudioProcessorEditor::buildHorizontalLayout (int editorW, int valueColW)
{
    return TR::buildSimpleHorizontalLayout (editorW, valueColW);
}

FREQTRAudioProcessorEditor::VerticalLayoutMetrics
FREQTRAudioProcessorEditor::buildVerticalLayout (int editorH, int biasY, bool ioExpanded)
{
    TR::SimpleVerticalLayoutConfig config;
    config.mainRows = 10;
    config.collapsedButtonRows = 3;
    config.collapsedSliderBottomRow = 1;
    config.expandedHasSidechainRow = true;

    return TR::buildSimpleVerticalLayout (editorH, biasY, ioExpanded, config);
}

void FREQTRAudioProcessorEditor::updateCachedLayout()
{
    cachedHLayout_ = buildHorizontalLayout (getWidth(), getTargetValueColumnWidth());
    cachedVLayout_ = buildVerticalLayout (getHeight(), kLayoutVerticalBiasPx, ioSectionExpanded_);

    const juce::Slider* harmRowSlider = sidechainShadowSlider.isVisible() ? static_cast<const juce::Slider*> (&sidechainShadowSlider)
                                                                          : static_cast<const juce::Slider*> (&harmSlider);
    const juce::Slider* sliders[kNumBars] = { &inputSlider, &outputSlider, &mixSlider,
                                               &freqSlider, &modSlider, &combSlider, &feedbackSlider, &engineSlider,
        &windowSlider, harmRowSlider, &polaritySlider, &jitterSlider, &styleSlider, &tiltSlider };

    for (int i = 0; i < kNumBars; ++i)
    {
        if (! sliders[i]->isVisible())
        {
            // MIX row (index 2): use dualMixBar_ bounds when SEND mode is active
            if (i == 2 && dualMixBar_.isVisible())
            {
                const auto& bb = dualMixBar_.getBounds();
                cachedValueAreas_[2] = TR::makeSimpleValueArea (bb, cachedHLayout_, getWidth());
                continue;
            }
            cachedValueAreas_[(size_t) i] = {};
            continue;
        }

        const auto& bb = sliders[i]->getBounds();
                cachedValueAreas_[(size_t) i] = TR::makeSimpleValueArea (bb, cachedHLayout_, getWidth());
    }

    // Filter bar value area
    if (filterBar_.isVisible())
    {
        const auto& fb = filterBar_.getBounds();
                cachedFilterValueArea_ = TR::makeSimpleValueArea (fb, cachedHLayout_, getWidth());
    }
    else
    {
        cachedFilterValueArea_ = {};
    }

    // Pan slider value area
    if (panSlider.isVisible())
    {
        const auto& pb = panSlider.getBounds();
                cachedPanValueArea_ = TR::makeSimpleValueArea (pb, cachedHLayout_, getWidth());
    }
    else
    {
        cachedPanValueArea_ = {};
    }

    // Lim threshold slider value area
    if (limThresholdSlider.isVisible())
    {
        const auto& lb = limThresholdSlider.getBounds();
                cachedLimThresholdValueArea_ = TR::makeSimpleValueArea (lb, cachedHLayout_, getWidth());
    }
    else
    {
        cachedLimThresholdValueArea_ = {};
    }

    // Cache toggle bar area
    cachedToggleBarArea_ = TR::makeSimpleToggleBarArea (cachedHLayout_, cachedVLayout_);
}

int FREQTRAudioProcessorEditor::getTargetValueColumnWidth() const
{
    std::uint64_t key = 1469598103934665603ull;
    auto mix = [&] (std::uint64_t v)
    {
        key ^= v;
        key *= 1099511628211ull;
    };

    mix ((std::uint64_t) getWidth());

    if (key == cachedValueColumnWidthKey)
        return cachedValueColumnWidth;

    cachedValueColumnWidthKey = key;

    const auto& font = kBoldFont40();

    // Measure worst-case legend widths
    constexpr const char* legends[] = {
        "5000.00Hz FREQ", "5.00kHz FREQ",
        "1/64. FREQ",
        "X4.00 MOD",
        "5.00kHz COMB",
        "100% FBK",
        "100% JITTER",
        "FREQ SHIFT ENGINE",
        "100% AM|RM ENGINE",
        "100% RM|FS ENGINE",
        "2048 WINDOW",
        "STEREO STYLE", "DUAL STYLE",
        "100% SHADOW",
        "100% HARM",
        "100% HRM",
        "-1.00 POLARITY",
        "100% MIX",
        "-INF dB INPUT",
        "+24.0 dB OUTPUT",
        "-36.0 dB LIM"
    };

    int maxLegW = 0;
    for (auto* legend : legends)
        maxLegW = juce::jmax (maxLegW, stringWidth (font, legend));

    const int desired = maxLegW + 16;
    const int minW = 90;
    const int maxAllowed = juce::jmax (minW, (int) std::round (getWidth() * 0.40));
    cachedValueColumnWidth = juce::jlimit (minW, maxAllowed, desired);
    return cachedValueColumnWidth;
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getValueAreaFor (const juce::Rectangle<int>& barBounds) const
{
    return TR::makeSimpleValueArea (barBounds, cachedHLayout_, getWidth());
}

juce::Slider* FREQTRAudioProcessorEditor::getSliderForValueAreaPoint (juce::Point<int> p)
{
    juce::Slider* harmRowSlider = sidechainShadowSlider.isVisible() ? static_cast<juce::Slider*> (&sidechainShadowSlider)
                                                                    : static_cast<juce::Slider*> (&harmSlider);

    if (auto* slider = TR::findSimpleSliderForValueAreaPoint (p, cachedValueAreas_, {
            { 0, &inputSlider, true, true },
            { 1, &outputSlider, true, true },
            { 2, &mixSlider, true, true },
            { 3, &freqSlider, true, true },
            { 4, &modSlider, true, true },
            { 5, &combSlider, true, true },
            { 6, &feedbackSlider, true, true },
            { 7, &engineSlider, true, true },
            { 8, &windowSlider, true, true },
            { 9, harmRowSlider, true, true },
            { 10, &polaritySlider, true, true },
            { 11, &jitterSlider, true, true },
            { 12, &styleSlider, true, true },
            { 13, &tiltSlider, true, true } }))
        return slider;

    if (engineSlider.isVisible() && engineSlider.isEnabled()
        && engineSlider.getBounds().getUnion (getValueAreaFor (engineSlider.getBounds())).contains (p))
        return &engineSlider;

    return nullptr;
}

//========================== Label areas ==========================

juce::Rectangle<int> FREQTRAudioProcessorEditor::getSyncLabelArea() const
{
    return TR::makeSimpleToggleLabelArea (syncButton, midiButton.getX() - TR::kSimpleToggleLegendCollisionPadPx, "SYNC", "SYN");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getMidiLabelArea() const
{
    return TR::makeSimpleToggleLabelArea (midiButton, getWidth() - TR::kSimpleToggleLegendCollisionPadPx, "MIDI", "MIDI");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getAlignLabelArea() const
{
    return TR::makeSimpleToggleLabelArea (alignButton, pdcButton.getX() - TR::kSimpleToggleLegendCollisionPadPx, "ALIGN", "ALN");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getPdcLabelArea() const
{
    return TR::makeSimpleToggleLabelArea (pdcButton, getWidth() - TR::kSimpleToggleLegendCollisionPadPx, "PDC", "PD");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getSidechainLabelArea() const
{
    return TR::makeSimpleToggleLabelArea (sidechainButton, getWidth() - TR::kSimpleToggleLegendCollisionPadPx, "SIDECHAIN", "SC");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getChaosFilterLabelArea() const
{
    if (chaosFilterButton.getWidth() <= 0 || chaosFilterButton.getHeight() <= 0)
        return {};

    return TR::makeSimpleToggleLabelArea (chaosFilterButton,
                                          chaosDelayButton.getX() - TR::kSimpleToggleLegendCollisionPadPx,
                                          "CHSF", "CHSF");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getChaosDelayLabelArea() const
{
    if (chaosDelayButton.getWidth() <= 0 || chaosDelayButton.getHeight() <= 0)
        return {};

    return TR::makeSimpleToggleLabelArea (chaosDelayButton,
                                          getWidth() - TR::kSimpleToggleLegendCollisionPadPx,
                                          "CHSD", "CHSD");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getInfoIconArea() const
{
    int contentRight = 0;
    for (size_t i = 0; i < cachedValueAreas_.size(); ++i)
    {
        if (! cachedValueAreas_[i].isEmpty())
        {
            contentRight = cachedValueAreas_[i].getRight();
            break;
        }
    }
    if (contentRight <= 0)
        contentRight = getWidth() - 8;

    const int titleH = cachedVLayout_.titleH;
    const int titleY = cachedVLayout_.titleTopPad;
    const int titleAreaH = cachedVLayout_.titleAreaH;
    const int size = juce::jlimit (20, 36, titleH);

    const int x = contentRight - size;
    const int y = titleY + juce::jmax (0, (titleAreaH - size) / 2);
    return { x, y, size, size };
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getTitleHitArea() const
{
    return TR::makeSimpleTitleHitArea (cachedHLayout_, cachedVLayout_, getWidth(), getInfoIconArea(),
                                       juce::String ("FREQ-TR"), juce::String ("v") + InfoContent::version,
                                       kBoldFont40());
}
juce::String FREQTRAudioProcessorEditor::getFreqShiftHilbertModeTooltip() const
{
    return "FREQSHIFT FILTER: "
         + FREQTRAudioProcessor::getFreqShiftHilbertModeName (audioProcessor.getFreqShiftHilbertMode());
}

void FREQTRAudioProcessorEditor::updateInfoIconCache()
{
    const auto area = getInfoIconArea();
    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return;

    const float cx = area.getCentreX() - (float) area.getX();
    const float cy = area.getCentreY() - (float) area.getY();
    const float toothTipR = (float) area.getWidth() * 0.47f;
    const float toothRootR = toothTipR * 0.78f;

    cachedInfoGearPath.clear();
    constexpr int teeth = 8;
    for (int i = 0; i < teeth * 2; ++i)
    {
        const float angle = (float) i * juce::MathConstants<float>::pi / (float) teeth
                          - juce::MathConstants<float>::halfPi;
        const float r = (i % 2 == 0) ? toothTipR : toothRootR;
        const float px = cx + r * std::cos (angle);
        const float py = cy + r * std::sin (angle);
        if (i == 0)
            cachedInfoGearPath.startNewSubPath (px, py);
        else
            cachedInfoGearPath.lineTo (px, py);
    }
    cachedInfoGearPath.closeSubPath();

    const float holeR = toothTipR * 0.40f;
    cachedInfoGearHole = { cx - holeR, cy - holeR, holeR * 2.0f, holeR * 2.0f };
}

//========================== Paint ==========================

TR::SimpleMainPanelSpec FREQTRAudioProcessorEditor::buildMainPanelSpec()
{
    TR::SimpleMainPanelSpec spec;
    spec.title   = "FREQ-TR";
    spec.version = juce::String ("v") + InfoContent::version;
    spec.ioExpanded   = ioSectionExpanded_;
    spec.toggleBarArea = cachedToggleBarArea_;

    // Collapsed main rows (10)
    {
        static constexpr int kCollapsedRows = 10;
        const int indices[kCollapsedRows] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
        const juce::String* full[kCollapsedRows]  = { &cachedFreqTextFull, &cachedModTextFull, &cachedCombTextFull, &cachedFeedbackTextFull, &cachedEngineTextFull,
                                                       &cachedWindowTextFull, &cachedHarmTextFull, &cachedPolarityTextFull, &cachedJitterTextFull, &cachedStyleTextFull };
        const juce::String* shrt[kCollapsedRows]  = { &cachedFreqTextShort, &cachedModTextShort, &cachedCombTextShort, &cachedFeedbackTextShort, &cachedEngineTextShort,
                                                       &cachedWindowTextShort, &cachedHarmTextShort, &cachedPolarityTextShort, &cachedJitterTextShort, &cachedStyleTextShort };
        const juce::String* intOnly[kCollapsedRows] = { &cachedFreqIntOnly, &cachedModIntOnly, &cachedCombIntOnly, &cachedFeedbackIntOnly, &cachedEngineIntOnly,
                                                          &cachedWindowIntOnly, &cachedHarmIntOnly, &cachedPolarityIntOnly, &cachedJitterIntOnly, &cachedStyleIntOnly };
        for (int i = 0; i < kCollapsedRows; ++i)
            TR::addSimpleMainPanelRow (spec, false, full[i], shrt[i], intOnly[i],
                                       cachedValueAreas_[(size_t) indices[i]]);
    }

    // Expanded-only rows
    {
        auto addIfVisible = [&](int idx, const juce::String* full, const juce::String* shrt, const juce::String* intOnly)
        {
            TR::addSimpleMainPanelRow (spec, true, full, shrt, intOnly,
                                       cachedValueAreas_[(size_t) idx]);
        };
        addIfVisible (0,  &cachedInputTextFull,      &cachedInputTextShort,      &cachedInputIntOnly);
        addIfVisible (1,  &cachedOutputTextFull,     &cachedOutputTextShort,     &cachedOutputIntOnly);
        addIfVisible (13, &cachedTiltTextFull,        &cachedTiltTextShort,       &cachedTiltIntOnly);
        addIfVisible (2,  &cachedMixTextFull,         &cachedMixTextShort,        &cachedMixIntOnly);

        TR::addSimpleMainPanelRow (spec, true, &cachedFilterTextFull, &cachedFilterTextShort, nullptr,
                                   cachedFilterValueArea_);
        TR::addSimpleMainPanelRow (spec, true, &cachedPanTextFull, &cachedPanTextShort, nullptr,
                                   cachedPanValueArea_);
        TR::addSimpleMainPanelRow (spec, true, &cachedLimThresholdTextFull, &cachedLimThresholdTextShort,
                                   &cachedLimThresholdIntOnly, cachedLimThresholdValueArea_,
                                   limThresholdSlider.isVisible());
    }

    // Combo labels
    spec.combosVisible = modeInCombo.isVisible();
    spec.comboLabels = {
        { &modeInCombo, "MODE IN", "IN" },
        { &modeOutCombo, "MODE OUT", "OUT" },
        { &sumBusCombo, "SUM BUS", "SUM" },
        { &limModeCombo, "LIMIT", "LIM" },
        { &mixModeCombo, "MIX", "MIX" },
        { &filterPosCombo, "F / T", "F/T" },
        { &invPolCombo, "INV POL", "POL" },
        { &invStrCombo, "INV STR", "STR" }
    };

    // Toggles
    {
        const int W = getWidth();
        TR::addSimpleMainPanelToggle (spec, false, sidechainButton, getSidechainLabelArea(), "SIDECHAIN", "SC",
                                      TR::makeSimpleMainPanelRightBound (W));
        TR::addSimpleMainPanelToggle (spec, false, chaosFilterButton, getChaosFilterLabelArea(), "CHSF", "CHSF",
                                      TR::makeSimpleMainPanelRightBoundBefore (chaosDelayButton, W));
        TR::addSimpleMainPanelToggle (spec, false, chaosDelayButton, getChaosDelayLabelArea(), "CHSD", "CHSD",
                                      TR::makeSimpleMainPanelRightBound (W));
    }

    // Collapsed toggles
    if (!ioSectionExpanded_)
    {
        const int W = getWidth();
        TR::addSimpleMainPanelToggle (spec, true, alignButton, getAlignLabelArea(), "ALIGN", "ALN",
                                      TR::makeSimpleMainPanelRightBoundBefore (pdcButton, W));
        TR::addSimpleMainPanelToggle (spec, true, pdcButton, getPdcLabelArea(), "PDC", "PD",
                                      TR::makeSimpleMainPanelRightBound (W));
        TR::addSimpleMainPanelToggle (spec, true, syncButton, getSyncLabelArea(), "SYNC", "SYN",
                                      TR::makeSimpleMainPanelRightBoundBefore (midiButton, W));
        TR::addSimpleMainPanelToggle (spec, true, midiButton, getMidiLabelArea(), "MIDI", "MIDI",
                                      TR::makeSimpleMainPanelRightBound (W));
    }

    // Info gear
    if (cachedInfoGearPath.isEmpty())
        updateInfoIconCache();
    TR::setSimpleMainPanelInfoGear (spec, cachedInfoGearPath, cachedInfoGearHole);

    return spec;
}

void FREQTRAudioProcessorEditor::paint (juce::Graphics& g)
{
    TR::SimpleMainPanelRenderer::paint (g, buildMainPanelSpec(), activeScheme, kBoldFont40(), getWidth());
}

void FREQTRAudioProcessorEditor::paintOverChildren (juce::Graphics&)
{
}

//========================== Resized ==========================

void FREQTRAudioProcessorEditor::resized()
{
    refreshLegendTextCache();

    if (! suppressSizePersistence)
    {
        if (juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown()
            || juce::Desktop::getInstance().getMainMouseSource().isDragging())
        {
            lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
        }
    }

    const int W = getWidth();
    const int H = getHeight();

    if (! suppressSizePersistence)
    {
        const uint32_t last = lastUserInteractionMs.load (std::memory_order_relaxed);
        const uint32_t now = juce::Time::getMillisecondCounter();
        const bool userRecent = (now - last) <= (uint32_t) kUserInteractionPersistWindowMs;
        if ((W != lastPersistedEditorW || H != lastPersistedEditorH) && userRecent)
        {
            audioProcessor.setUiEditorSize (W, H);
            lastPersistedEditorW = W;
            lastPersistedEditorH = H;
        }
    }

    const auto horizontalLayout = buildHorizontalLayout (W, getTargetValueColumnWidth());
    const auto verticalLayout = buildVerticalLayout (H, kLayoutVerticalBiasPx, ioSectionExpanded_);

    // Position sliders â€” toggle bar always at top, swaps between main and IO bars

    if (ioSectionExpanded_)
    {
        // Expanded: [toggle bar] â†’ INPUT, OUTPUT, TILT, FILTER, PAN, MIX; chaos buttons; main params hidden
        TR::setSimpleComponentVisible (inputSlider, true);
        TR::setSimpleComponentVisible (outputSlider, true);
        TR::setSimpleComponentVisible (tiltSlider, true);
        TR::setSimpleComponentVisible (filterBar_, true);
        TR::setSimpleComponentVisible (panSlider, true);
        TR::setSimpleComponentVisible (mixSlider, true);
        TR::setSimpleComponentVisible (limThresholdSlider, true);

        TR::placeSimpleRowComponent (inputSlider, horizontalLayout, verticalLayout, 0);
        TR::placeSimpleRowComponent (outputSlider, horizontalLayout, verticalLayout, 1);
        TR::placeSimpleRowComponent (tiltSlider, horizontalLayout, verticalLayout, 2);
        TR::placeSimpleRowComponent (filterBar_, horizontalLayout, verticalLayout, 3);
        TR::placeSimpleRowComponent (panSlider, horizontalLayout, verticalLayout, 4);
        TR::placeSimpleRowComponent (mixSlider, horizontalLayout, verticalLayout, 5);
        TR::placeSimpleRowComponent (limThresholdSlider, horizontalLayout, verticalLayout, 6);
        {
            const int blockTopLimit = limThresholdSlider.getBottom() + verticalLayout.gapY;
            const int blockBottomLimit = verticalLayout.chaosRowY - verticalLayout.gapY;
            TR::placeSimpleIoComboGrid (horizontalLayout, verticalLayout, blockTopLimit, blockBottomLimit,
                                        modeInCombo, modeOutCombo, sumBusCombo, limModeCombo,
                                        mixModeCombo, filterPosCombo, invPolCombo, invStrCombo);
        }
        // DualMixBar at same position as mixSlider
        TR::placeSimpleRowComponent (dualMixBar_, horizontalLayout, verticalLayout, 5);

        // Chaos buttons at chaosRowY
        const int chaosY = verticalLayout.chaosRowY;
        TR::placeSimpleWideTogglePair (chaosFilterButton, chaosDelayButton, horizontalLayout, verticalLayout, chaosY);
        TR::placeSimpleDisplayLabel (chaosFilterDisplay, getChaosFilterLabelArea());
        TR::placeSimpleDisplayLabel (chaosDelayDisplay, getChaosDelayLabelArea());

        TR::setSimpleComponentVisible (modeInCombo, true);
        TR::setSimpleComponentVisible (modeOutCombo, true);
        TR::setSimpleComponentVisible (sumBusCombo, true);
        TR::setSimpleComponentVisible (limModeCombo, true);
        TR::setSimpleComponentVisible (invPolCombo, true);
        TR::setSimpleComponentVisible (invStrCombo, true);
        TR::setSimpleComponentVisible (mixModeCombo, true);
        TR::setSimpleComponentVisible (filterPosCombo, true);
        {
            const bool isSendMode = mixModeCombo.getSelectedId() == 2;
            TR::setSimpleComponentVisible (mixSlider, ! isSendMode);
            TR::setSimpleComponentVisible (dualMixBar_, isSendMode);
        }
        TR::setSimpleComponentVisible (chaosFilterButton, true);
        TR::setSimpleComponentVisible (chaosFilterDisplay, true);
        TR::setSimpleComponentVisible (chaosDelayButton, true);
        TR::setSimpleComponentVisible (chaosDelayDisplay, true);

        TR::setSimpleComponentVisible (syncButton, false);
        TR::setSimpleComponentVisible (midiButton, false);
        TR::setSimpleComponentVisible (midiChannelDisplay, false);
        TR::setSimpleComponentVisible (alignButton, false);
        TR::setSimpleComponentVisible (pdcButton, false);
        TR::setSimpleComponentVisible (pdcDisplay, false);
        TR::setSimpleComponentVisible (retrigDisplay, false);
        TR::setSimpleComponentVisible (sidechainButton, true);
        TR::setSimpleComponentVisible (sidechainDisplay, true);

        TR::hideSimpleComponent (freqSlider);
        TR::hideSimpleComponent (modSlider);
        TR::hideSimpleComponent (feedbackSlider);
        TR::hideSimpleComponent (jitterSlider);
        TR::hideSimpleComponent (combSlider);
        TR::hideSimpleComponent (engineSlider);
        TR::hideSimpleComponent (windowSlider);
        TR::hideSimpleComponent (harmSlider);
        TR::hideSimpleComponent (polaritySlider);
        TR::hideSimpleComponent (styleSlider);

        TR::setSimpleComponentVisible (freqSlider, false);
        TR::setSimpleComponentVisible (modSlider, false);
        TR::setSimpleComponentVisible (feedbackSlider, false);
        TR::setSimpleComponentVisible (jitterSlider, false);
        TR::setSimpleComponentVisible (combSlider, false);
        TR::setSimpleComponentVisible (engineSlider, false);
        TR::setSimpleComponentVisible (windowSlider, false);
        TR::setSimpleComponentVisible (harmSlider, false);
        TR::setSimpleComponentVisible (polaritySlider, false);
        TR::setSimpleComponentVisible (styleSlider, false);
    }
    else
    {
        // Collapsed: [toggle bar] â†’ main params; IO + filter + pan + tilt + chaos hidden
        TR::setSimpleComponentVisible (inputSlider, false);
        TR::setSimpleComponentVisible (outputSlider, false);
        TR::setSimpleComponentVisible (tiltSlider, false);
        TR::setSimpleComponentVisible (filterBar_, false);
        TR::setSimpleComponentVisible (panSlider, false);
        TR::setSimpleComponentVisible (mixSlider, false);
        TR::setSimpleComponentVisible (limThresholdSlider, false);
        TR::setSimpleComponentVisible (chaosFilterButton, false);
        TR::setSimpleComponentVisible (chaosFilterDisplay, false);
        TR::setSimpleComponentVisible (chaosDelayButton, false);
        TR::setSimpleComponentVisible (chaosDelayDisplay, false);
        TR::setSimpleComponentVisible (modeInCombo, false);
        TR::setSimpleComponentVisible (modeOutCombo, false);
        TR::setSimpleComponentVisible (sumBusCombo, false);
        TR::setSimpleComponentVisible (limModeCombo, false);
        TR::setSimpleComponentVisible (invPolCombo, false);
        TR::setSimpleComponentVisible (invStrCombo, false);
        TR::setSimpleComponentVisible (mixModeCombo, false);
        TR::setSimpleComponentVisible (filterPosCombo, false);
        TR::setSimpleComponentVisible (dualMixBar_, false);
        TR::hideSimpleComponent (inputSlider);
        TR::hideSimpleComponent (outputSlider);
        TR::hideSimpleComponent (tiltSlider);
        TR::hideSimpleComponent (filterBar_);
        TR::hideSimpleComponent (panSlider);
        TR::hideSimpleComponent (mixSlider);
        TR::hideSimpleComponent (dualMixBar_);
        TR::hideSimpleComponent (limThresholdSlider);

        TR::setSimpleComponentVisible (syncButton, true);
        TR::setSimpleComponentVisible (midiButton, true);
        TR::setSimpleComponentVisible (midiChannelDisplay, true);
        TR::setSimpleComponentVisible (alignButton, true);
        TR::setSimpleComponentVisible (pdcButton, true);
        TR::setSimpleComponentVisible (pdcDisplay, true);
        TR::setSimpleComponentVisible (retrigDisplay, true);
        TR::setSimpleComponentVisible (sidechainButton, false);
        TR::setSimpleComponentVisible (sidechainDisplay, false);

        TR::setSimpleComponentVisible (freqSlider, true);
        TR::setSimpleComponentVisible (modSlider, true);
        TR::setSimpleComponentVisible (feedbackSlider, true);
        TR::setSimpleComponentVisible (jitterSlider, true);
        TR::setSimpleComponentVisible (combSlider, true);
        TR::setSimpleComponentVisible (engineSlider, true);
        TR::setSimpleComponentVisible (windowSlider, true);
        TR::setSimpleComponentVisible (harmSlider, true);
        TR::setSimpleComponentVisible (polaritySlider, true);
        TR::setSimpleComponentVisible (styleSlider, true);

        TR::placeSimpleRowComponent (freqSlider, horizontalLayout, verticalLayout, 0);
        TR::placeSimpleRowComponent (modSlider, horizontalLayout, verticalLayout, 1);
        TR::placeSimpleRowComponent (combSlider, horizontalLayout, verticalLayout, 2);
        TR::placeSimpleRowComponent (feedbackSlider, horizontalLayout, verticalLayout, 3);
        TR::placeSimpleRowComponent (engineSlider, horizontalLayout, verticalLayout, 4);
        TR::placeSimpleRowComponent (windowSlider, horizontalLayout, verticalLayout, 5);
        TR::placeSimpleRowComponent (harmSlider, horizontalLayout, verticalLayout, 6);
        TR::placeSimpleRowComponent (polaritySlider, horizontalLayout, verticalLayout, 7);
        TR::placeSimpleRowComponent (jitterSlider, horizontalLayout, verticalLayout, 8);
        TR::placeSimpleRowComponent (styleSlider, horizontalLayout, verticalLayout, 9);
    }

    // Button area: 2 rows â€” row1: ALIGN+PDC, row2: SYNC+MIDI
    const int utilityRow1Y = ioSectionExpanded_ ? verticalLayout.btnRow1Y : verticalLayout.btnRow2Y;
    const int utilityRow2Y = ioSectionExpanded_ ? verticalLayout.btnRow2Y : verticalLayout.btnRow3Y;
    TR::placeSimpleToggleAt (alignButton, horizontalLayout, verticalLayout, false, utilityRow1Y);
    TR::placeSimpleToggleAt (pdcButton, horizontalLayout, verticalLayout, true, utilityRow1Y);
    TR::placeSimpleToggleAt (syncButton, horizontalLayout, verticalLayout, false, utilityRow2Y);
    TR::placeSimpleToggleAt (midiButton, horizontalLayout, verticalLayout, true, utilityRow2Y);
    TR::placeSimpleToggleAt (sidechainButton, horizontalLayout, verticalLayout, false, verticalLayout.btnRow3Y);

// Retrig tooltip overlay
    {
        const auto syncLabelRect = getSyncLabelArea();
        retrigDisplay.setBounds (syncLabelRect);
    }

    // MIDI tooltip overlay
    {
        const auto midiLabelRect = getMidiLabelArea();
        midiChannelDisplay.setBounds (midiLabelRect);
    }

    TR::placeSimpleDisplayLabel (pdcDisplay, getPdcLabelArea());
    TR::placeSimpleDisplayLabel (sidechainDisplay, getSidechainLabelArea());
    updateSidechainDependentControls();

    if (resizerCorner != nullptr)
        resizerCorner->setBounds (W - kResizerCornerPx, H - kResizerCornerPx, kResizerCornerPx, kResizerCornerPx);

    promptOverlay.setBounds (getLocalBounds());
    if (promptOverlayActive)
        promptOverlay.toFront (false);

    updateCachedLayout();
    updateInfoIconCache();
    crtEffect.setResolution (static_cast<float> (W), static_cast<float> (H));
}






