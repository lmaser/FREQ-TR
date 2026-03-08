// PluginEditor.cpp — FREQ-TR
#include "PluginEditor.h"
#include "InfoContent.h"
#include <functional>

using namespace TR;

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace UiStateKeys
{
    constexpr const char* editorWidth = "uiEditorWidth";
    constexpr const char* editorHeight = "uiEditorHeight";
    constexpr const char* useCustomPalette = "uiUseCustomPalette";
    constexpr const char* crtEnabled = "uiFxTailEnabled";
    constexpr std::array<const char*, 2> customPalette {
        "uiCustomPalette0",
        "uiCustomPalette1"
    };
}

// ── Timer & display constants ──
static constexpr int   kCrtTimerHz   = 10;
static constexpr int   kIdleTimerHz  = 4;
static constexpr float kMultEpsilon  = 0.01f;

// ── Mod slider ↔ multiplier conversion ──
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

static double multiplierToModSlider (double m)
{
    m = juce::jlimit (kModMinMult, kModMaxMult, m);
    if (m <= 1.0)
        return kModCenter * (1.0 - (1.0 / m)) / (kModMaxMult - 1.0) * (kModMaxMult - 1.0) / kModScale;
    return kModCenter + kModCenter * (m - 1.0) / kModScale;
}

// ── MIDI channel tooltip ──
static juce::String formatMidiChannelTooltip (int ch)
{
    return "CHANNEL " + juce::String (ch);
}

// ── Parameter listener IDs ──
static constexpr std::array<const char*, 4> kUiMirrorParamIds {
    FREQTRAudioProcessor::kParamUiPalette,
    FREQTRAudioProcessor::kParamUiCrt,
    FREQTRAudioProcessor::kParamUiColor0,
    FREQTRAudioProcessor::kParamUiColor1
};

//========================== LookAndFeel ==========================

void FREQTRAudioProcessorEditor::MinimalLNF::drawLinearSlider (juce::Graphics& g,
                                                                 int x, int y, int width, int height,
                                                                 float sliderPos, float, float,
                                                                 const juce::Slider::SliderStyle, juce::Slider&)
{
    const juce::Rectangle<float> r ((float) x, (float) y, (float) width, (float) height);

    g.setColour (scheme.outline);
    g.drawRect (r, 4.0f);

    const float pad = 7.0f;
    auto inner = r.reduced (pad);

    g.setColour (scheme.bg);
    g.fillRect (inner);

    // Fill from left
    const float fillW = juce::jlimit (0.0f, inner.getWidth(), sliderPos - inner.getX());
    auto fill = inner.withWidth (fillW);

    g.setColour (scheme.fg);
    g.fillRect (fill);
}

void FREQTRAudioProcessorEditor::MinimalLNF::drawTickBox (juce::Graphics& g, juce::Component& button,
                                                            float x, float y, float w, float h,
                                                            bool ticked, bool, bool, bool)
{
    juce::ignoreUnused (x, y, w, h);

    const auto local = button.getLocalBounds().toFloat().reduced (1.0f);
    const float side = juce::jlimit (14.0f,
                                     juce::jmax (14.0f, local.getHeight() - 2.0f),
                                     std::round (local.getHeight() * 0.65f));

    auto r = juce::Rectangle<float> (local.getX() + 2.0f,
                                     local.getCentreY() - (side * 0.5f),
                                     side,
                                     side).getIntersection (local);

    g.setColour (scheme.outline);
    g.drawRect (r, 4.0f);

    const float innerInset = juce::jlimit (1.0f, side * 0.45f, side * UiMetrics::tickBoxInnerInsetRatio);
    auto inner = r.reduced (innerInset);

    if (ticked)
    {
        g.setColour (scheme.fg);
        g.fillRect (inner);
    }
    else
    {
        g.setColour (scheme.bg);
        g.fillRect (inner);
    }
}

void FREQTRAudioProcessorEditor::MinimalLNF::drawButtonBackground (juce::Graphics& g,
                                                                      juce::Button& button,
                                                                      const juce::Colour& backgroundColour,
                                                                      bool shouldDrawButtonAsHighlighted,
                                                                      bool shouldDrawButtonAsDown)
{
    auto r = button.getLocalBounds();

    auto fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.brighter (0.12f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.06f);

    g.setColour (fill);
    g.fillRect (r);

    g.setColour (scheme.outline);
    g.drawRect (r.reduced (1), 3);
}

void FREQTRAudioProcessorEditor::MinimalLNF::drawAlertBox (juce::Graphics& g,
                                                             juce::AlertWindow& alert,
                                                             const juce::Rectangle<int>& textArea,
                                                             juce::TextLayout& textLayout)
{
    auto bounds = alert.getLocalBounds();

    g.setColour (scheme.bg);
    g.fillRect (bounds);

    g.setColour (scheme.outline);
    g.drawRect (bounds.reduced (1), 3);

    g.setColour (scheme.text);
    textLayout.draw (g, textArea.toFloat());
}

void FREQTRAudioProcessorEditor::MinimalLNF::drawBubble (juce::Graphics& g,
                                                           juce::BubbleComponent&,
                                                           const juce::Point<float>&,
                                                           const juce::Rectangle<float>& body)
{
    drawOverlayPanel (g,
                      body.getSmallestIntegerContainer(),
                      findColour (juce::TooltipWindow::backgroundColourId),
                      findColour (juce::TooltipWindow::outlineColourId));
}

void FREQTRAudioProcessorEditor::MinimalLNF::drawScrollbar (juce::Graphics& g, juce::ScrollBar&,
                                                              int x, int y, int width, int height,
                                                              bool isScrollbarVertical, int thumbStart, int thumbSize,
                                                              bool isMouseOver, bool isMouseDown)
{
    juce::ignoreUnused (x, y, width, height);

    const auto thumbColour = scheme.text.withAlpha (isMouseDown ? 0.7f
                                                     : isMouseOver ? 0.5f
                                                                   : 0.3f);
    constexpr float barThickness = 7.0f;
    constexpr float cornerRadius = 3.5f;

    if (isScrollbarVertical)
    {
        const float bx = (float) (x + width) - barThickness - 1.0f;
        g.setColour (thumbColour);
        g.fillRoundedRectangle (bx, (float) thumbStart,
                                barThickness, (float) thumbSize, cornerRadius);
    }
    else
    {
        const float by = (float) (y + height) - barThickness - 1.0f;
        g.setColour (thumbColour);
        g.fillRoundedRectangle ((float) thumbStart, by,
                                (float) thumbSize, barThickness, cornerRadius);
    }
}

juce::Font FREQTRAudioProcessorEditor::MinimalLNF::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    const float h = juce::jlimit (12.0f, 26.0f, buttonHeight * 0.48f);
    return juce::Font (juce::FontOptions (h).withStyle ("Bold"));
}

juce::Font FREQTRAudioProcessorEditor::MinimalLNF::getAlertWindowMessageFont()
{
    auto f = juce::LookAndFeel_V4::getAlertWindowMessageFont();
    f.setBold (true);
    return f;
}

juce::Font FREQTRAudioProcessorEditor::MinimalLNF::getLabelFont (juce::Label& label)
{
    auto f = label.getFont();
    if (f.getHeight() <= 0.0f)
    {
        const float h = juce::jlimit (12.0f, 40.0f, (float) juce::jmax (12, label.getHeight() - 6));
        f = juce::Font (juce::FontOptions (h).withStyle ("Bold"));
    }
    else
    {
        f.setBold (true);
    }

    return f;
}

juce::Font FREQTRAudioProcessorEditor::MinimalLNF::getSliderPopupFont (juce::Slider&)
{
    return makeOverlayDisplayFont();
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::MinimalLNF::getTooltipBounds (const juce::String& tipText,
                                                                                juce::Point<int> screenPos,
                                                                                juce::Rectangle<int> parentArea)
{
    const auto f = makeOverlayDisplayFont();
    const int h = juce::jmax (UiMetrics::tooltipMinHeight,
                              (int) std::ceil (f.getHeight() * UiMetrics::tooltipHeightScale));

    const int anchorOffsetX = juce::jmax (8, (int) std::round ((double) h * UiMetrics::tooltipAnchorXRatio));
    const int anchorOffsetY = juce::jmax (10, (int) std::round ((double) h * UiMetrics::tooltipAnchorYRatio));
    const int parentMargin = juce::jmax (2, (int) std::round ((double) h * UiMetrics::tooltipParentMarginRatio));
    const int widthPad = juce::jmax (16, (int) std::round (f.getHeight() * UiMetrics::tooltipWidthPadFontRatio));

    const int w = juce::jmax (UiMetrics::tooltipMinWidth, stringWidth (f, tipText) + widthPad);
    auto r = juce::Rectangle<int> (screenPos.x + anchorOffsetX, screenPos.y + anchorOffsetY, w, h);
    return r.constrainedWithin (parentArea.reduced (parentMargin));
}

void FREQTRAudioProcessorEditor::MinimalLNF::drawTooltip (juce::Graphics& g,
                                                            const juce::String& text, int width, int height)
{
    const auto f = makeOverlayDisplayFont();
    const int h = juce::jmax (UiMetrics::tooltipMinHeight,
                              (int) std::ceil (f.getHeight() * UiMetrics::tooltipHeightScale));
    const int textInsetX = juce::jmax (4, (int) std::round ((double) h * UiMetrics::tooltipTextInsetXRatio));
    const int textInsetY = juce::jmax (1, (int) std::round ((double) h * UiMetrics::tooltipTextInsetYRatio));

    drawOverlayPanel (g,
                      { 0, 0, width, height },
                      findColour (juce::TooltipWindow::backgroundColourId),
                      findColour (juce::TooltipWindow::outlineColourId));

    g.setColour (findColour (juce::TooltipWindow::textColourId));
    g.setFont (f);
    g.drawFittedText (text,
                      textInsetX,
                      textInsetY,
                      juce::jmax (1, width - (textInsetX * 2)),
                      juce::jmax (1, height - (textInsetY * 2)),
                      juce::Justification::centred,
                      1);
}

//========================== Editor ==========================

FREQTRAudioProcessorEditor::FREQTRAudioProcessorEditor (FREQTRAudioProcessor& p)
: AudioProcessorEditor (&p), audioProcessor (p)
{
    const std::array<BarSlider*, 7> barSliders { &freqSlider, &modSlider, &engineSlider, &styleSlider, &shapeSlider, &polaritySlider, &mixSlider };

    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    crtEnabled = audioProcessor.getUiCrtEnabled();

    for (int i = 0; i < 2; ++i)
        customPalette[(size_t) i] = audioProcessor.getUiCustomPaletteColour (i);

    setOpaque (true);
    setBufferedToImage (true);

    applyActivePalette();
    setLookAndFeel (&lnf);
    tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 250);
    tooltipWindow->setLookAndFeel (&lnf);
    tooltipWindow->setAlwaysOnTop (true);
    tooltipWindow->setInterceptsMouseClicks (false, false);

    setResizable (true, true);
    setResizeLimits (kMinW, kMinH, kMaxW, kMaxH);

    resizeConstrainer.setMinimumSize (kMinW, kMinH);
    resizeConstrainer.setMaximumSize (kMaxW, kMaxH);

    resizerCorner = std::make_unique<juce::ResizableCornerComponent> (this, &resizeConstrainer);
    addAndMakeVisible (*resizerCorner);
    resizerCorner->addMouseListener (this, true);

    addAndMakeVisible (promptOverlay);
    promptOverlay.setInterceptsMouseClicks (true, true);
    promptOverlay.setVisible (false);

    const int restoredW = juce::jlimit (kMinW, kMaxW, audioProcessor.getUiEditorWidth());
    const int restoredH = juce::jlimit (kMinH, kMaxH, audioProcessor.getUiEditorHeight());
    suppressSizePersistence = true;
    setSize (restoredW, restoredH);
    suppressSizePersistence = false;
    lastPersistedEditorW = restoredW;
    lastPersistedEditorH = restoredH;

    for (auto* slider : barSliders)
    {
        slider->setOwner (this);
        setupBar (*slider);
        addAndMakeVisible (*slider);
        slider->addListener (this);
    }

    freqSlider.setNumDecimalPlacesToDisplay (1);
    modSlider.setNumDecimalPlacesToDisplay (2);
    engineSlider.setNumDecimalPlacesToDisplay (1);
    styleSlider.setNumDecimalPlacesToDisplay (0);
    shapeSlider.setNumDecimalPlacesToDisplay (2);
    polaritySlider.setNumDecimalPlacesToDisplay (2);
    mixSlider.setNumDecimalPlacesToDisplay (1);

    syncButton.setButtonText ("");
    midiButton.setButtonText ("");

    addAndMakeVisible (syncButton);
    addAndMakeVisible (midiButton);

    // MIDI channel tooltip overlay
    const int savedChannel = audioProcessor.getMidiChannel();
    midiChannelDisplay.setText ("", juce::dontSendNotification);
    midiChannelDisplay.setInterceptsMouseClicks (true, false);
    midiChannelDisplay.addMouseListener (this, false);
    midiChannelDisplay.setTooltip (formatMidiChannelTooltip (savedChannel));
    midiChannelDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    midiChannelDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    midiChannelDisplay.setOpaque (false);
    addAndMakeVisible (midiChannelDisplay);

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
    bindSlider (engineAttachment,  FREQTRAudioProcessor::kParamEngine,  engineSlider,  (double) FREQTRAudioProcessor::kEngineDefault);
    bindSlider (styleAttachment,   FREQTRAudioProcessor::kParamStyle,   styleSlider,   (double) FREQTRAudioProcessor::kStyleDefault);
    bindSlider (shapeAttachment,   FREQTRAudioProcessor::kParamShape,   shapeSlider,   (double) FREQTRAudioProcessor::kShapeDefault);
    bindSlider (polarityAttachment, FREQTRAudioProcessor::kParamPolarity, polaritySlider, kDefaultPolarity);
    bindSlider (mixAttachment,      FREQTRAudioProcessor::kParamMix,     mixSlider,     (double) FREQTRAudioProcessor::kMixDefault);

    // Disable numeric popup for STYLE (slider-only)
    styleSlider.setAllowNumericPopup (false);

    auto bindButton = [&] (std::unique_ptr<ButtonAttachment>& attachment,
                           const char* paramId,
                           juce::Button& button)
    {
        attachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, paramId, button);
    };

    bindButton (syncAttachment, FREQTRAudioProcessor::kParamSync, syncButton);
    bindButton (midiAttachment, FREQTRAudioProcessor::kParamMidi, midiButton);

    for (auto* paramId : kUiMirrorParamIds)
        audioProcessor.apvts.addParameterListener (paramId, this);

    audioProcessor.apvts.addParameterListener (FREQTRAudioProcessor::kParamSync, this);

    // Apply initial SYNC state
    if (syncButton.getToggleState())
        updateFreqSliderForSyncMode();

    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]()
    {
        if (safeThis == nullptr) return;
        safeThis->applyPersistedUiStateFromProcessor (true, true);
    });

    juce::Timer::callAfterDelay (250, [safeThis]()
    {
        if (safeThis == nullptr) return;
        safeThis->applyPersistedUiStateFromProcessor (true, true);
    });

    juce::Timer::callAfterDelay (750, [safeThis]()
    {
        if (safeThis == nullptr) return;
        safeThis->applyPersistedUiStateFromProcessor (true, true);
    });

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

    audioProcessor.setUiUseCustomPalette (useCustomPalette);
    audioProcessor.setUiCrtEnabled (crtEnabled);

    dismissEditorOwnedModalPrompts (lnf);
    setPromptOverlayActive (false);

    const std::array<BarSlider*, 7> barSliders { &freqSlider, &modSlider, &engineSlider, &styleSlider, &shapeSlider, &polaritySlider, &mixSlider };
    for (auto* slider : barSliders)
        slider->removeListener (this);

    if (tooltipWindow != nullptr)
        tooltipWindow->setLookAndFeel (nullptr);

    setLookAndFeel (nullptr);
}

void FREQTRAudioProcessorEditor::applyActivePalette()
{
    const auto& palette = useCustomPalette ? customPalette : defaultPalette;

    FREQScheme scheme;
    scheme.bg = palette[1];
    scheme.fg = palette[0];
    scheme.outline = palette[0];
    scheme.text = palette[0];

    activeScheme = scheme;
    lnf.setScheme (activeScheme);
}

void FREQTRAudioProcessorEditor::applyCrtState (bool enabled)
{
    crtEnabled = enabled;
    crtEffect.setEnabled (crtEnabled);
    setComponentEffect (crtEnabled ? &crtEffect : nullptr);
    stopTimer();
    startTimerHz (crtEnabled ? kCrtTimerHz : kIdleTimerHz);
}

void FREQTRAudioProcessorEditor::applyLabelTextColour (juce::Label& label, juce::Colour colour)
{
    label.setColour (juce::Label::textColourId, colour);
}

void FREQTRAudioProcessorEditor::sliderValueChanged (juce::Slider* slider)
{
    auto isBarSlider = [&] (const juce::Slider* s)
    {
        return s == &freqSlider || s == &modSlider || s == &engineSlider
            || s == &styleSlider || s == &shapeSlider || s == &polaritySlider || s == &mixSlider;
    };

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

    promptOverlayActive = shouldBeActive;

    promptOverlay.setBounds (getLocalBounds());
    promptOverlay.setVisible (shouldBeActive);
    if (shouldBeActive)
        promptOverlay.toFront (false);

    const bool enableControls = ! shouldBeActive;
    const std::array<juce::Component*, 9> interactiveControls {
        &freqSlider, &modSlider, &engineSlider, &styleSlider,
        &shapeSlider, &polaritySlider, &mixSlider,
        &syncButton, &midiButton
    };
    for (auto* control : interactiveControls)
        control->setEnabled (enableControls);

    if (resizerCorner != nullptr)
        resizerCorner->setEnabled (enableControls);

    repaint();

    if (promptOverlayActive)
        promptOverlay.toFront (false);

    anchorEditorOwnedPromptWindows (*this, lnf);
}

void FREQTRAudioProcessorEditor::moved()
{
    if (promptOverlayActive)
        promptOverlay.toFront (false);

    anchorEditorOwnedPromptWindows (*this, lnf);
}

void FREQTRAudioProcessorEditor::parentHierarchyChanged()
{
   #if JUCE_WINDOWS
    if (auto* peer = getPeer())
    {
        if (auto nativeHandle = peer->getNativeHandle())
        {
            static HBRUSH blackBrush = CreateSolidBrush (RGB (0, 0, 0));
            SetClassLongPtr (static_cast<HWND> (nativeHandle),
                             GCLP_HBRBACKGROUND,
                             reinterpret_cast<LONG_PTR> (blackBrush));
        }
    }
   #endif
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

    const bool isSizeParam = parameterID == FREQTRAudioProcessor::kParamUiWidth
                         || parameterID == FREQTRAudioProcessor::kParamUiHeight;

    const bool isUiVisualParam = parameterID == FREQTRAudioProcessor::kParamUiPalette
                             || parameterID == FREQTRAudioProcessor::kParamUiCrt
                             || parameterID == FREQTRAudioProcessor::kParamUiColor0
                             || parameterID == FREQTRAudioProcessor::kParamUiColor1;

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
        refreshLegendTextCache();
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
                                    || engineSlider.isMouseButtonDown()
                                    || styleSlider.isMouseButtonDown()
                                    || shapeSlider.isMouseButtonDown()
                                    || polaritySlider.isMouseButtonDown()
                                    || mixSlider.isMouseButtonDown();
        if (! anySliderDragging)
            repaint();
    }
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
        for (int i = 0; i < 2; ++i)
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

        const bool paletteSwitchChanged = (useCustomPalette != targetUseCustomPalette);
        const bool fxChanged = (crtEnabled != targetCrtEnabled);

        if (paletteSwitchChanged)
            useCustomPalette = targetUseCustomPalette;

        if (fxChanged)
            applyCrtState (targetCrtEnabled);

        if (paletteChanged || paletteSwitchChanged)
            applyActivePalette();

        if (paletteChanged || paletteSwitchChanged || fxChanged)
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
    if (hz < 0.05f)
        return "0 Hz FREQ";
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, 2) + " kHz FREQ";
    return juce::String (hz, 1) + " Hz FREQ";
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
    if (hz < 0.05f)
        return "0 Hz";
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, 2) + "kHz";
    return juce::String (hz, 1) + "Hz";
}

juce::String FREQTRAudioProcessorEditor::getModText() const
{
    const float mult = (float) modSliderToMultiplier (modSlider.getValue());
    if (std::abs (mult - 1.0f) < kMultEpsilon)
        return "X1 MOD";
    return "X" + juce::String (mult, 2) + " MOD";
}

juce::String FREQTRAudioProcessorEditor::getModTextShort() const
{
    const float mult = (float) modSliderToMultiplier (modSlider.getValue());
    if (std::abs (mult - 1.0f) < kMultEpsilon)
        return "X1";
    return "X" + juce::String (mult, 2);
}

juce::String FREQTRAudioProcessorEditor::getEngineText() const
{
    const float val = (float) engineSlider.getValue();
    if (val < 0.01f)
        return "AM ENGINE";
    if (val > 0.99f)
        return "FREQ SHIFT ENGINE";
    const int pct = (int) std::lround (val * 100.0);
    return juce::String (pct) + "% ENGINE";
}

juce::String FREQTRAudioProcessorEditor::getEngineTextShort() const
{
    const float val = (float) engineSlider.getValue();
    if (val < 0.01f)
        return "AM";
    if (val > 0.99f)
        return "FREQ SHIFT";
    const int pct = (int) std::lround (val * 100.0);
    return juce::String (pct) + "%";
}

juce::String FREQTRAudioProcessorEditor::getStyleText() const
{
    const int style = (int) styleSlider.getValue();
    return (style == 0) ? "MONO STYLE" : "STEREO STYLE";
}

juce::String FREQTRAudioProcessorEditor::getStyleTextShort() const
{
    const int style = (int) styleSlider.getValue();
    return (style == 0) ? "MONO" : "STEREO";
}

juce::String FREQTRAudioProcessorEditor::getShapeText() const
{
    const float val = (float) shapeSlider.getValue();
    const float scaled = val * 3.0f;
    juce::String name;
    if (scaled < 0.5f)        name = "SINE";
    else if (scaled < 1.5f)   name = "TRI";
    else if (scaled < 2.5f)   name = "SQR";
    else                      name = "SAW";
    return name + " SHAPE";
}

juce::String FREQTRAudioProcessorEditor::getShapeTextShort() const
{
    const float val = (float) shapeSlider.getValue();
    const float scaled = val * 3.0f;
    if (scaled < 0.5f)        return "SINE";
    if (scaled < 1.5f)        return "TRI";
    if (scaled < 2.5f)        return "SQR";
    return "SAW";
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
        return "-1";
    if (val >= 0.995f)
        return "+1";
    if (std::abs (val) < 0.005f)
        return "0";
    return juce::String (val, 2);
}

juce::String FREQTRAudioProcessorEditor::getMixText() const
{
    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);
    return juce::String (pct) + "% MIX";
}

juce::String FREQTRAudioProcessorEditor::getMixTextShort() const
{
    const int pct = (int) std::lround (mixSlider.getValue() * 100.0);
    return juce::String (pct) + "%";
}

bool FREQTRAudioProcessorEditor::refreshLegendTextCache()
{
    const auto oldFreqFull     = cachedFreqTextFull;
    const auto oldFreqShort    = cachedFreqTextShort;
    const auto oldModFull      = cachedModTextFull;
    const auto oldModShort     = cachedModTextShort;
    const auto oldEngineFull   = cachedEngineTextFull;
    const auto oldEngineShort  = cachedEngineTextShort;
    const auto oldStyleFull    = cachedStyleTextFull;
    const auto oldStyleShort   = cachedStyleTextShort;
    const auto oldShapeFull    = cachedShapeTextFull;
    const auto oldShapeShort   = cachedShapeTextShort;
    const auto oldPolarityFull = cachedPolarityTextFull;
    const auto oldPolarityShort = cachedPolarityTextShort;
    const auto oldMixFull      = cachedMixTextFull;
    const auto oldMixShort     = cachedMixTextShort;

    cachedFreqTextFull      = getFreqText();
    cachedFreqTextShort     = getFreqTextShort();
    cachedModTextFull       = getModText();
    cachedModTextShort      = getModTextShort();
    cachedEngineTextFull    = getEngineText();
    cachedEngineTextShort   = getEngineTextShort();
    cachedStyleTextFull     = getStyleText();
    cachedStyleTextShort    = getStyleTextShort();
    cachedShapeTextFull     = getShapeText();
    cachedShapeTextShort    = getShapeTextShort();
    cachedPolarityTextFull  = getPolarityText();
    cachedPolarityTextShort = getPolarityTextShort();
    cachedMixTextFull       = getMixText();
    cachedMixTextShort      = getMixTextShort();

    return oldFreqFull      != cachedFreqTextFull
        || oldFreqShort     != cachedFreqTextShort
        || oldModFull       != cachedModTextFull
        || oldModShort      != cachedModTextShort
        || oldEngineFull    != cachedEngineTextFull
        || oldEngineShort   != cachedEngineTextShort
        || oldStyleFull     != cachedStyleTextFull
        || oldStyleShort    != cachedStyleTextShort
        || oldShapeFull     != cachedShapeTextFull
        || oldShapeShort    != cachedShapeTextShort
        || oldPolarityFull  != cachedPolarityTextFull
        || oldPolarityShort != cachedPolarityTextShort
        || oldMixFull       != cachedMixTextFull
        || oldMixShort      != cachedMixTextShort;
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

    s.setColour (juce::Slider::trackColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::thumbColourId, juce::Colours::transparentBlack);
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
        freqSlider.setRange (0.0, 29.0, 1.0);
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
        freqSlider.setSkewFactor (0.35);
    }

    refreshLegendTextCache();
    repaint();
}

//========================== Popup helper classes ==========================

struct PopupSwatchButton final : public juce::TextButton
{
    std::function<void()> onLeftClick;
    std::function<void()> onRightClick;

    void clicked() override
    {
        if (onLeftClick)
            onLeftClick();
        else
            juce::TextButton::clicked();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onRightClick)
                onRightClick();
            return;
        }

        juce::TextButton::mouseUp (e);
    }
};

struct PopupClickableLabel final : public juce::Label
{
    using juce::Label::Label;
    std::function<void()> onClick;

    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::Label::mouseUp (e);
        if (! e.mods.isPopupMenu() && onClick)
            onClick();
    }
};

//========================== Popup static layout helpers ==========================

static void syncGraphicsPopupState (juce::AlertWindow& aw,
                                    const std::array<juce::Colour, 2>& defaultPalette,
                                    const std::array<juce::Colour, 2>& customPalette,
                                    bool useCustomPalette)
{
    if (auto* t = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteDefaultToggle")))
        t->setToggleState (! useCustomPalette, juce::dontSendNotification);
    if (auto* t = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteCustomToggle")))
        t->setToggleState (useCustomPalette, juce::dontSendNotification);

    for (int i = 0; i < 2; ++i)
    {
        if (auto* dflt = dynamic_cast<juce::TextButton*> (aw.findChildWithID ("defaultSwatch" + juce::String (i))))
            setPaletteSwatchColour (*dflt, defaultPalette[(size_t) i]);
        if (auto* custom = dynamic_cast<juce::TextButton*> (aw.findChildWithID ("customSwatch" + juce::String (i))))
        {
            setPaletteSwatchColour (*custom, customPalette[(size_t) i]);
            custom->setTooltip (colourToHexRgb (customPalette[(size_t) i]));
        }
    }

    auto applyLabelTextColourTo = [] (juce::Label* lbl, juce::Colour col)
    {
        if (lbl != nullptr)
            lbl->setColour (juce::Label::textColourId, col);
    };

    const juce::Colour activeText = useCustomPalette ? customPalette[0] : defaultPalette[0];
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteDefaultLabel")), activeText);
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteCustomLabel")), activeText);
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteTitle")), activeText);
    applyLabelTextColourTo (dynamic_cast<juce::Label*> (aw.findChildWithID ("fxLabel")), activeText);
}

static void layoutGraphicsPopupContent (juce::AlertWindow& aw)
{
    layoutAlertWindowButtons (aw);

    auto snapEven = [] (int v) { return v & ~1; };

    const int contentLeft = kPromptInnerMargin;
    const int contentRight = aw.getWidth() - kPromptInnerMargin;
    const int contentW = juce::jmax (0, contentRight - contentLeft);

    auto* dfltToggle = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteDefaultToggle"));
    auto* dfltLabel  = dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteDefaultLabel"));
    auto* customToggle = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("paletteCustomToggle"));
    auto* customLabel  = dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteCustomLabel"));
    auto* paletteTitle = dynamic_cast<juce::Label*> (aw.findChildWithID ("paletteTitle"));
    auto* fxToggle = dynamic_cast<juce::ToggleButton*> (aw.findChildWithID ("fxToggle"));
    auto* fxLabel  = dynamic_cast<juce::Label*> (aw.findChildWithID ("fxLabel"));
    auto* okBtn = aw.getNumButtons() > 0 ? aw.getButton (0) : nullptr;

    constexpr int toggleBox = GraphicsPromptLayout::toggleBox;
    constexpr int toggleGap = 4;
    constexpr int toggleVisualInsetLeft = 2;
    constexpr int swatchSize = GraphicsPromptLayout::swatchSize;
    constexpr int swatchGap = GraphicsPromptLayout::swatchGap;
    constexpr int columnGap = GraphicsPromptLayout::columnGap;
    constexpr int titleH = GraphicsPromptLayout::titleHeight;

    const int toggleVisualSide = juce::jlimit (14,
                                               juce::jmax (14, toggleBox - 2),
                                               (int) std::lround ((double) toggleBox * 0.65));

    const int swatchW = swatchSize;
    const int swatchH = (2 * swatchSize) + swatchGap;
    const int swatchGroupSize = (2 * swatchW) + swatchGap;
    const int swatchesH = swatchH;
    const int modeH = toggleBox;

    const int baseGap1 = GraphicsPromptLayout::titleToModeGap;
    const int baseGap2 = GraphicsPromptLayout::modeToSwatchesGap;

    const int titleY = snapEven (kPromptFooterBottomPad);
    const int footerY = getAlertButtonsTop (aw);

    const int bodyH = modeH + baseGap2 + swatchesH;
    const int bodyZoneTop = titleY + titleH + baseGap1;
    const int bodyZoneBottom = footerY - baseGap1;
    const int bodyZoneH = juce::jmax (0, bodyZoneBottom - bodyZoneTop);
    const int bodyY = snapEven (bodyZoneTop + juce::jmax (0, (bodyZoneH - bodyH) / 2));

    const int modeY = bodyY;
    const int blocksY = snapEven (modeY + modeH + baseGap2);

    const int dfltLabelW = (dfltLabel != nullptr) ? juce::jmax (38, stringWidth (dfltLabel->getFont(), "DFLT") + 2) : 40;
    const int customLabelW = (customLabel != nullptr) ? juce::jmax (38, stringWidth (customLabel->getFont(), "CSTM") + 2) : 40;
    const int fxLabelW = (fxLabel != nullptr)
                       ? juce::jmax (90, stringWidth (fxLabel->getFont(), fxLabel->getText().toUpperCase()) + 2)
                       : 96;

    const int toggleLabelStartOffset = toggleVisualInsetLeft + toggleVisualSide + toggleGap;
    const int dfltRowW = toggleLabelStartOffset + dfltLabelW;
    const int customRowW = toggleLabelStartOffset + customLabelW;
    const int fxRowW = toggleLabelStartOffset + fxLabelW;
    const int okBtnW = (okBtn != nullptr) ? okBtn->getWidth() : 96;

    const int leftColumnW = juce::jmax (swatchGroupSize, juce::jmax (dfltRowW, fxRowW));
    const int rightColumnW = juce::jmax (swatchGroupSize, juce::jmax (customRowW, okBtnW));
    const int columnsRowW = leftColumnW + columnGap + rightColumnW;
    const int columnsX = snapEven (contentLeft + juce::jmax (0, (contentW - columnsRowW) / 2));
    const int col0X = columnsX;
    const int col1X = columnsX + leftColumnW + columnGap;

    const int dfltX = col0X;
    const int customX = col1X;

    const int defaultSwatchStartX = col0X;
    const int customSwatchStartX = col1X;

    if (paletteTitle != nullptr)
    {
        const int paletteW = juce::jmax (100, juce::jmin (leftColumnW, contentRight - col0X));
        paletteTitle->setBounds (col0X, titleY, paletteW, titleH);
    }

    if (dfltToggle != nullptr)   dfltToggle->setBounds (dfltX, modeY, toggleBox, toggleBox);
    if (dfltLabel != nullptr)    dfltLabel->setBounds (dfltX + toggleLabelStartOffset, modeY, dfltLabelW, toggleBox);
    if (customToggle != nullptr) customToggle->setBounds (customX, modeY, toggleBox, toggleBox);
    if (customLabel != nullptr)  customLabel->setBounds (customX + toggleLabelStartOffset, modeY, customLabelW, toggleBox);

    auto placeSwatchGroup = [&] (const juce::String& prefix, int startX)
    {
        const int startY = blocksY;
        for (int i = 0; i < 2; ++i)
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (aw.findChildWithID (prefix + juce::String (i))))
            {
                b->setBounds (startX + i * (swatchW + swatchGap), startY, swatchW, swatchH);
            }
        }
    };

    placeSwatchGroup ("defaultSwatch", defaultSwatchStartX);
    placeSwatchGroup ("customSwatch", customSwatchStartX);

    if (okBtn != nullptr)
    {
        auto okR = okBtn->getBounds();
        okR.setX (col1X);
        okR.setY (footerY);
        okBtn->setBounds (okR);

        const int fxY = snapEven (footerY + juce::jmax (0, (okR.getHeight() - toggleBox) / 2));
        const int fxX = col0X;
        if (fxToggle != nullptr) fxToggle->setBounds (fxX, fxY, toggleBox, toggleBox);
        if (fxLabel != nullptr)  fxLabel->setBounds (fxX + toggleLabelStartOffset, fxY, fxLabelW, toggleBox);
    }

    auto updateVisualBounds = [] (juce::Component* c, int& minX, int& maxR)
    {
        if (c == nullptr)
            return;
        const auto r = c->getBounds();
        minX = juce::jmin (minX, r.getX());
        maxR = juce::jmax (maxR, r.getRight());
    };

    int visualMinX = aw.getWidth();
    int visualMaxR = 0;

    updateVisualBounds (paletteTitle, visualMinX, visualMaxR);
    updateVisualBounds (dfltToggle, visualMinX, visualMaxR);
    updateVisualBounds (dfltLabel, visualMinX, visualMaxR);
    updateVisualBounds (customToggle, visualMinX, visualMaxR);
    updateVisualBounds (customLabel, visualMinX, visualMaxR);
    updateVisualBounds (fxToggle, visualMinX, visualMaxR);
    updateVisualBounds (fxLabel, visualMinX, visualMaxR);
    updateVisualBounds (okBtn, visualMinX, visualMaxR);

    for (int i = 0; i < 2; ++i)
    {
        updateVisualBounds (aw.findChildWithID ("defaultSwatch" + juce::String (i)), visualMinX, visualMaxR);
        updateVisualBounds (aw.findChildWithID ("customSwatch" + juce::String (i)), visualMinX, visualMaxR);
    }

    if (visualMaxR > visualMinX)
    {
        const int leftMarginToPrompt = visualMinX;
        const int rightMarginToPrompt = aw.getWidth() - visualMaxR;

        int dx = (rightMarginToPrompt - leftMarginToPrompt) / 2;

        const int minDx = contentLeft - visualMinX;
        const int maxDx = contentRight - visualMaxR;
        dx = juce::jlimit (minDx, maxDx, dx);

        if (dx != 0)
        {
            auto shiftX = [dx] (juce::Component* c)
            {
                if (c == nullptr)
                    return;
                auto r = c->getBounds();
                r.setX (r.getX() + dx);
                c->setBounds (r);
            };

            shiftX (paletteTitle);
            shiftX (dfltToggle);
            shiftX (dfltLabel);
            shiftX (customToggle);
            shiftX (customLabel);
            shiftX (fxToggle);
            shiftX (fxLabel);
            shiftX (okBtn);

            for (int i = 0; i < 2; ++i)
            {
                shiftX (aw.findChildWithID ("defaultSwatch" + juce::String (i)));
                shiftX (aw.findChildWithID ("customSwatch" + juce::String (i)));
            }
        }
    }
}

static void layoutInfoPopupContent (juce::AlertWindow& aw)
{
    layoutAlertWindowButtons (aw);

    const int contentTop = kPromptBodyTopPad;
    const int contentBottom = getAlertButtonsTop (aw) - kPromptBodyBottomPad;
    const int contentH = juce::jmax (0, contentBottom - contentTop);
    const int bodyW = aw.getWidth() - (2 * kPromptInnerMargin);

    auto* viewport = dynamic_cast<juce::Viewport*> (aw.findChildWithID ("bodyViewport"));
    if (viewport == nullptr)
        return;

    viewport->setBounds (kPromptInnerMargin, contentTop, bodyW, contentH);

    auto* content = viewport->getViewedComponent();
    if (content == nullptr)
        return;

    constexpr int kItemGap = 10;
    int y = 0;
    const int innerW = bodyW - 10;

    for (int i = 0; i < content->getNumChildComponents(); ++i)
    {
        auto* child = content->getChildComponent (i);
        if (child == nullptr || ! child->isVisible())
            continue;

        int itemH = 30;
        if (auto* label = dynamic_cast<juce::Label*> (child))
        {
            auto font = label->getFont();
            const auto text = label->getText();
            const auto border = label->getBorderSize();

            if (! text.containsChar ('\n'))
            {
                itemH = (int) std::ceil (font.getHeight()) + border.getTopAndBottom();
            }
            else
            {
                juce::AttributedString as;
                as.append (text, font, label->findColour (juce::Label::textColourId));
                as.setJustification (label->getJustificationType());
                juce::TextLayout layout;
                const int textAreaW = innerW - border.getLeftAndRight();
                layout.createLayout (as, (float) juce::jmax (1, textAreaW));
                itemH = juce::jmax (20, (int) std::ceil (layout.getHeight()
                                                         + font.getDescent())
                                        + border.getTopAndBottom() + 4);
            }
        }
        else if (dynamic_cast<juce::HyperlinkButton*> (child) != nullptr)
        {
            itemH = 28;
        }

        child->setBounds (0, y, innerW, itemH);

        if (auto* label = dynamic_cast<juce::Label*> (child))
        {
            const auto& props = label->getProperties();
            if (props.contains ("poemPadFraction"))
            {
                const float padFrac = (float) props["poemPadFraction"];
                const int padPx = juce::jmax (4, (int) std::round (innerW * padFrac));
                label->setBorderSize (juce::BorderSize<int> (0, padPx, 0, padPx));

                auto font = label->getFont();
                const int textAreaW = innerW - 2 * padPx;
                for (float scale = 1.0f; scale >= 0.65f; scale -= 0.025f)
                {
                    font.setHorizontalScale (scale);
                    juce::GlyphArrangement glyphs;
                    glyphs.addLineOfText (font, label->getText(), 0.0f, 0.0f);
                    if (static_cast<int> (std::ceil (glyphs.getBoundingBox (0, -1, false).getWidth())) <= textAreaW)
                        break;
                }
                label->setFont (font);
            }
        }

        y += itemH + kItemGap;
    }

    if (y > kItemGap)
        y -= kItemGap;

    content->setSize (innerW, juce::jmax (contentH, y));
}

//========================== Right-click numeric popup (stub) ==========================

void FREQTRAudioProcessorEditor::openNumericEntryPopupForSlider (juce::Slider& s)
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    juce::String suffix;
    juce::String suffixShort;
    const bool isFreqSyncMode = (&s == &freqSlider && syncButton.getToggleState());
    if (&s == &freqSlider)
    {
        if (isFreqSyncMode)
        {
            suffix = "";
            suffixShort = "";
        }
        else
        {
            suffix = " HZ FREQ";
            suffixShort = " HZ";
        }
    }
    else if (&s == &modSlider)       { suffix = " X MOD";      suffixShort = " X"; }
    else if (&s == &engineSlider)    { suffix = " % ENGINE";   suffixShort = " %"; }
    else if (&s == &styleSlider)     { suffix = " STYLE";      suffixShort = " STYLE"; }
    else if (&s == &shapeSlider)     { suffix = " SHAPE";      suffixShort = " SHP"; }
    else if (&s == &polaritySlider)  { suffix = " POLARITY";   suffixShort = " POL"; }
    else if (&s == &mixSlider)       { suffix = " % MIX";      suffixShort = " % MIX"; }
    const juce::String suffixText = suffix.trimStart();
    const juce::String suffixTextShort = suffixShort.trimStart();
    const bool isPercentPrompt = (&s == &engineSlider || &s == &mixSlider);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);

    juce::String currentDisplay;
    if (&s == &modSlider)
        currentDisplay = juce::String (modSliderToMultiplier (s.getValue()), 2);
    else if (&s == &engineSlider)
        currentDisplay = juce::String ((int) std::lround (s.getValue() * 100.0));
    else if (&s == &mixSlider)
        currentDisplay = juce::String ((int) std::lround (s.getValue() * 100.0));
    else
        currentDisplay = s.getTextFromValue (s.getValue());

    aw->addTextEditor ("val", currentDisplay, juce::String());

    juce::Label* suffixLabel = nullptr;

    struct SyncDivisionInputFilter : juce::TextEditor::InputFilter
    {
        int maxLen;
        SyncDivisionInputFilter (int maxLength) : maxLen (maxLength) {}
        juce::String filterNewText (juce::TextEditor& editor,
                                    const juce::String& newText) override
        {
            juce::ignoreUnused (editor);
            juce::String result;
            for (auto c : newText)
            {
                if (juce::CharacterFunctions::isDigit (c) || c == '/' ||
                    c == 'T' || c == 't' || c == '.')
                    result += c;
                if (maxLen > 0 && result.length() >= maxLen)
                    break;
            }
            return result;
        }
    };

    juce::Rectangle<int> editorBaseBounds;
    std::function<void()> layoutValueAndSuffix;

    if (auto* te = aw->getTextEditor ("val"))
    {
        const auto& f = kBoldFont40();
        te->setFont (f);
        te->applyFontToAllText (f);

        auto r = te->getBounds();
        r.setHeight ((int) (f.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
        r.setY (juce::jmax (kPromptEditorMinTopPx, r.getY() - kPromptEditorRaiseYPx));
        editorBaseBounds = r;

        suffixLabel = new juce::Label ("suffix", suffixText);
        suffixLabel->setComponentID (kPromptSuffixLabelId);
        suffixLabel->setJustificationType (juce::Justification::centredLeft);
        applyLabelTextColour (*suffixLabel, scheme.text);
        suffixLabel->setBorderSize (juce::BorderSize<int> (0));
        suffixLabel->setFont (f);
        aw->addAndMakeVisible (suffixLabel);

        juce::String worstCaseText;
        if (&s == &freqSlider)
            worstCaseText = isFreqSyncMode ? "1/64T." : "5000.0";
        else if (&s == &modSlider)
            worstCaseText = "4.00";
        else if (&s == &engineSlider)
            worstCaseText = "100";
        else if (&s == &styleSlider)
            worstCaseText = "1";
        else if (&s == &shapeSlider)
            worstCaseText = "1.00";
        else if (&s == &polaritySlider)
            worstCaseText = "-1.00";
        else if (&s == &mixSlider)
            worstCaseText = "100";
        else
            worstCaseText = "999.99";

        const int maxInputTextW = juce::jmax (1, stringWidth (f, worstCaseText));

        layoutValueAndSuffix = [aw, te, suffixLabel, editorBaseBounds, isPercentPrompt, suffixText, suffixTextShort, maxInputTextW]()
        {
            juce::ignoreUnused (isPercentPrompt);
            const int contentPad = kPromptInlineContentPadPx;
            const int contentLeft = contentPad;
            const int contentRight = (aw != nullptr ? aw->getWidth() - contentPad : editorBaseBounds.getRight());
            const int availableW = contentRight - contentLeft;
            const int contentCenter = (contentLeft + contentRight) / 2;

            const int fullLabelW = stringWidth (suffixLabel->getFont(), suffixText) + 2;
            const bool stickPercentFull = suffixText.containsChar ('%');
            const int spaceWFull = stickPercentFull ? 0 : juce::jmax (2, stringWidth (suffixLabel->getFont(), " "));
            const int worstCaseFullW = maxInputTextW + spaceWFull + fullLabelW;

            const bool useShort = (worstCaseFullW > availableW) && suffixTextShort != suffixText;
            const juce::String& activeSuffix = useShort ? suffixTextShort : suffixText;
            suffixLabel->setText (activeSuffix, juce::dontSendNotification);

            const auto txt = te->getText();
            const int textW = juce::jmax (1, stringWidth (te->getFont(), txt));
            int labelW = stringWidth (suffixLabel->getFont(), activeSuffix) + 2;
            auto er = te->getBounds();

            const bool stickPercentToValue = activeSuffix.containsChar ('%');
            const int spaceW = stickPercentToValue ? 0 : juce::jmax (2, stringWidth (te->getFont(), " "));
            const int minGapPx = juce::jmax (1, spaceW);

            constexpr int kEditorTextPadPx = 12;
            constexpr int kMinEditorWidthPx = 24;
            const int editorW = juce::jlimit (kMinEditorWidthPx,
                                              editorBaseBounds.getWidth(),
                                              textW + (kEditorTextPadPx * 2));
            er.setWidth (editorW);

            const int combinedW = textW + minGapPx + labelW;

            int blockLeft = contentCenter - (combinedW / 2);
            const int minBlockLeft = contentLeft;
            const int maxBlockLeft = juce::jmax (minBlockLeft, contentRight - combinedW);
            blockLeft = juce::jlimit (minBlockLeft, maxBlockLeft, blockLeft);

            int teX = blockLeft - ((editorW - textW) / 2);
            const int minTeX = contentLeft;
            const int maxTeX = juce::jmax (minTeX, contentRight - editorW);
            teX = juce::jlimit (minTeX, maxTeX, teX);

            er.setX (teX);
            te->setBounds (er);

            const int textLeftActual = er.getX() + (er.getWidth() - textW) / 2;
            int labelX = textLeftActual + textW + minGapPx;
            const int minLabelX = contentLeft;
            const int maxLabelX = juce::jmax (minLabelX, contentRight - labelW);
            labelX = juce::jlimit (minLabelX, maxLabelX, labelX);

            const int labelY = er.getY();
            const int labelH = juce::jmax (1, er.getHeight());
            suffixLabel->setBounds (labelX, labelY, labelW, labelH);
        };

        te->setBounds (editorBaseBounds);
        int labelW0 = stringWidth (suffixLabel->getFont(), suffixText) + 2;
        suffixLabel->setBounds (r.getRight() + 2, r.getY() + 1, labelW0, juce::jmax (1, r.getHeight() - 2));

        if (layoutValueAndSuffix)
            layoutValueAndSuffix();

        double minVal = 0.0, maxVal = 1.0;
        int maxLen = 0, maxDecs = 4;

        if (&s == &freqSlider)
        {
            if (isFreqSyncMode)
            {
                minVal = 0.0;
                maxVal = 29.0;
                maxDecs = 0;
                maxLen = 6;
            }
            else
            {
                minVal = 0.0;
                maxVal = 5000.0;
                maxDecs = 1;
                maxLen = 6;
            }
        }
        else if (&s == &modSlider)
        {
            minVal = 0.0;
            maxVal = 4.0;
            maxDecs = 2;
            maxLen = 4;
        }
        else if (&s == &engineSlider)
        {
            minVal = 0.0;
            maxVal = 100.0;
            maxDecs = 0;
            maxLen = 3;
        }
        else if (&s == &styleSlider)
        {
            minVal = 0.0;
            maxVal = 1.0;
            maxDecs = 0;
            maxLen = 1;
        }
        else if (&s == &shapeSlider)
        {
            minVal = 0.0;
            maxVal = 1.0;
            maxDecs = 2;
            maxLen = 4;
        }
        else if (&s == &polaritySlider)
        {
            minVal = -1.0;
            maxVal = 1.0;
            maxDecs = 2;
            maxLen = 5;
        }
        else if (&s == &mixSlider)
        {
            minVal = 0.0;
            maxVal = 100.0;
            maxDecs = 0;
            maxLen = 3;
        }

        if (&s == &freqSlider && isFreqSyncMode)
            te->setInputFilter (new SyncDivisionInputFilter (maxLen), true);
        else
            te->setInputFilter (new NumericInputFilter (minVal, maxVal, maxLen, maxDecs), true);

        te->onTextChange = [te, layoutValueAndSuffix, maxDecs]() mutable
        {
            auto txt = te->getText();
            int dot = txt.indexOfChar ('.');
            if (dot >= 0)
            {
                int decimals = txt.length() - dot - 1;
                if (decimals > maxDecs)
                    te->setText (txt.substring (0, dot + 1 + maxDecs), juce::dontSendNotification);
            }
            if (layoutValueAndSuffix)
                layoutValueAndSuffix();
        };
    }

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);

    const juce::Font& kPromptFont = kBoldFont40();

    preparePromptTextEditor (*aw, "val", scheme.bg, scheme.text, scheme.fg, kPromptFont, false);

    if (suffixLabel != nullptr && ! editorBaseBounds.isEmpty())
    {
        if (auto* te = aw->getTextEditor ("val"))
            suffixLabel->setFont (te->getFont());
        if (layoutValueAndSuffix)
            layoutValueAndSuffix();
    }

    styleAlertButtons (*aw, lnf);

    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
    juce::Slider* sliderPtr = &s;

    setPromptOverlayActive (true);

    aw->setLookAndFeel (&lnf);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [&] (juce::AlertWindow& a)
        {
            if (layoutValueAndSuffix)
                layoutValueAndSuffix();
            layoutAlertWindowButtons (a);
            preparePromptTextEditor (a, "val", scheme.bg, scheme.text, scheme.fg, kPromptFont, false);
        });

        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
        aw->repaint();
    }

    {
        preparePromptTextEditor (*aw, "val", scheme.bg, scheme.text, scheme.fg, kPromptFont, false);
        if (auto* suffixLbl = dynamic_cast<juce::Label*> (aw->findChildWithID (kPromptSuffixLabelId)))
        {
            if (auto* te = aw->getTextEditor ("val"))
                suffixLbl->setFont (te->getFont());
        }

        if (layoutValueAndSuffix)
            layoutValueAndSuffix();

        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::MessageManager::callAsync ([safeAw]()
        {
            if (safeAw == nullptr)
                return;
            bringPromptWindowToFront (*safeAw);
            safeAw->repaint();
        });
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, sliderPtr, aw] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);

            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);

            if (safeThis == nullptr || sliderPtr == nullptr)
                return;

            if (result != 1)
                return;

            const auto txt = aw->getTextEditorContents ("val").trim();
            auto normalised = txt.replaceCharacter (',', '.');

            double v = 0.0;

            // FREQ in sync mode: parse division name or index
            if (safeThis != nullptr && sliderPtr == &safeThis->freqSlider
                && safeThis->syncButton.getToggleState())
            {
                int foundIndex = -1;
                auto choices = FREQTRAudioProcessor::getFreqSyncChoices();
                for (int i = 0; i < choices.size(); ++i)
                {
                    if (txt.equalsIgnoreCase (choices[i]) ||
                        txt.equalsIgnoreCase (choices[i].replace ("/", "")))
                    {
                        foundIndex = i;
                        break;
                    }
                }

                if (foundIndex < 0)
                {
                    juce::String t = normalised.trimStart();
                    while (t.startsWithChar ('+'))
                        t = t.substring (1).trimStart();
                    const juce::String numericToken = t.initialSectionContainingOnly ("0123456789");
                    foundIndex = numericToken.getIntValue();
                }

                v = (double) juce::jlimit (0, 29, foundIndex);
            }
            else
            {
                juce::String t = normalised.trimStart();
                while (t.startsWithChar ('+'))
                    t = t.substring (1).trimStart();
                const juce::String numericToken = t.initialSectionContainingOnly ("0123456789.,-");
                v = numericToken.getDoubleValue();

                // Percent → 0..1 for engine and mix
                if (safeThis != nullptr && (sliderPtr == &safeThis->engineSlider
                                         || sliderPtr == &safeThis->mixSlider))
                    v *= 0.01;

                // Multiplier → slider for MOD
                if (safeThis != nullptr && sliderPtr == &safeThis->modSlider)
                    v = multiplierToModSlider (v);
            }

            const auto range = sliderPtr->getRange();
            double clamped = juce::jlimit (range.getStart(), range.getEnd(), v);

            if (safeThis != nullptr && sliderPtr == &safeThis->freqSlider
                && ! safeThis->syncButton.getToggleState())
            {
                clamped = roundToDecimals (clamped, 1);
            }

            sliderPtr->setValue (clamped, juce::sendNotificationSync);
        }));
}

void FREQTRAudioProcessorEditor::openMidiChannelPrompt()
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    const juce::String suffixText = "CHANNEL";
    const bool legendFirst = true;
    const int channel = audioProcessor.getMidiChannel();
    const juce::String currentValue = juce::String (channel);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("val", currentValue, juce::String());

    juce::Label* suffixLabel = nullptr;

    struct MidiChannelInputFilter : juce::TextEditor::InputFilter
    {
        juce::String filterNewText (juce::TextEditor& editor, const juce::String& newText) override
        {
            juce::String result;
            for (auto c : newText)
            {
                if (juce::CharacterFunctions::isDigit (c))
                    result += c;
                if (result.length() >= 2)
                    break;
            }

            juce::String proposed = editor.getText();
            int insertPos = editor.getCaretPosition();
            proposed = proposed.substring (0, insertPos) + result
                     + proposed.substring (insertPos + editor.getHighlightedText().length());
            if (proposed.length() > 2 || proposed.getIntValue() > 16)
                return juce::String();

            if (proposed.length() > 1 && proposed[0] == '0')
                return juce::String();

            return result;
        }
    };

    juce::Rectangle<int> editorBaseBounds;
    std::function<void()> layoutValueAndSuffix;

    if (auto* te = aw->getTextEditor ("val"))
    {
        const auto& f = kBoldFont40();
        te->setFont (f);
        te->applyFontToAllText (f);

        auto r = te->getBounds();
        r.setHeight ((int) (f.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
        r.setY (juce::jmax (kPromptEditorMinTopPx, r.getY() - kPromptEditorRaiseYPx));
        editorBaseBounds = r;

        suffixLabel = new juce::Label ("suffix", suffixText);
        suffixLabel->setComponentID (kPromptSuffixLabelId);
        suffixLabel->setJustificationType (juce::Justification::centredLeft);
        applyLabelTextColour (*suffixLabel, scheme.text);
        suffixLabel->setBorderSize (juce::BorderSize<int> (0));
        suffixLabel->setFont (f);
        aw->addAndMakeVisible (suffixLabel);

        layoutValueAndSuffix = [aw, te, suffixLabel, editorBaseBounds, legendFirst]()
        {
            int labelW = stringWidth (suffixLabel->getFont(), suffixLabel->getText()) + 2;
            auto er = te->getBounds();

            const auto txt = te->getText();
            const int textW = juce::jmax (1, stringWidth (te->getFont(), txt));
            const int spaceW = juce::jmax (2, stringWidth (te->getFont(), " "));
            const int minGapPx = juce::jmax (1, spaceW);

            constexpr int kEditorTextPadPx = 12;
            constexpr int kMinEditorWidthPx = 24;
            const int editorW = juce::jlimit (kMinEditorWidthPx,
                                              editorBaseBounds.getWidth(),
                                              textW + (kEditorTextPadPx * 2));
            er.setWidth (editorW);

            const int combinedW = labelW + minGapPx + textW;

            const int contentPad = kPromptInlineContentPadPx;
            const int contentLeft = contentPad;
            const int contentRight = (aw != nullptr ? aw->getWidth() - contentPad : editorBaseBounds.getRight());
            const int contentCenter = (contentLeft + contentRight) / 2;

            int blockLeft = contentCenter - (combinedW / 2);
            const int minBlockLeft = contentLeft;
            const int maxBlockLeft = juce::jmax (minBlockLeft, contentRight - combinedW);
            blockLeft = juce::jlimit (minBlockLeft, maxBlockLeft, blockLeft);

            if (legendFirst)
            {
                int labelX = blockLeft;
                const int minLabelX = contentLeft;
                const int maxLabelX = juce::jmax (minLabelX, contentRight - combinedW);
                labelX = juce::jlimit (minLabelX, maxLabelX, labelX);

                const int labelY = er.getY();
                const int labelH = juce::jmax (1, er.getHeight());
                suffixLabel->setBounds (labelX, labelY, labelW, labelH);

                int teX = labelX + labelW + minGapPx - ((editorW - textW) / 2);
                const int minTeX = contentLeft;
                const int maxTeX = juce::jmax (minTeX, contentRight - editorW);
                teX = juce::jlimit (minTeX, maxTeX, teX);
                er.setX (teX);
                te->setBounds (er);
            }
            else
            {
                int teX = blockLeft - ((editorW - textW) / 2);
                const int minTeX = contentLeft;
                const int maxTeX = juce::jmax (minTeX, contentRight - editorW);
                teX = juce::jlimit (minTeX, maxTeX, teX);
                er.setX (teX);
                te->setBounds (er);

                const int textLeftActual = er.getX() + (er.getWidth() - textW) / 2;
                int labelX = textLeftActual + textW + minGapPx;
                const int minLabelX = contentLeft;
                const int maxLabelX = juce::jmax (minLabelX, contentRight - labelW);
                labelX = juce::jlimit (minLabelX, maxLabelX, labelX);

                const int labelY = er.getY();
                const int labelH = juce::jmax (1, er.getHeight());
                suffixLabel->setBounds (labelX, labelY, labelW, labelH);
            }
        };

        te->setBounds (editorBaseBounds);
        int labelW0 = stringWidth (suffixLabel->getFont(), suffixText) + 2;
        suffixLabel->setBounds (r.getRight() + 2, r.getY() + 1, labelW0, juce::jmax (1, r.getHeight() - 2));

        if (layoutValueAndSuffix)
            layoutValueAndSuffix();

        te->setInputFilter (new MidiChannelInputFilter(), true);
        te->onTextChange = [te, layoutValueAndSuffix]() mutable
        {
            if (layoutValueAndSuffix)
                layoutValueAndSuffix();
        };
    }

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);

    const juce::Font& kMidiPromptFont = kBoldFont40();

    preparePromptTextEditor (*aw, "val", scheme.bg, scheme.text, scheme.fg, kMidiPromptFont, false);

    if (suffixLabel != nullptr && ! editorBaseBounds.isEmpty())
    {
        if (auto* te = aw->getTextEditor ("val"))
            suffixLabel->setFont (te->getFont());
        if (layoutValueAndSuffix)
            layoutValueAndSuffix();
    }

    styleAlertButtons (*aw, lnf);

    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);

    setPromptOverlayActive (true);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutValueAndSuffix] (juce::AlertWindow& a)
        {
            layoutAlertWindowButtons (a);
            if (layoutValueAndSuffix)
                layoutValueAndSuffix();
        });

        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
    }

    {
        preparePromptTextEditor (*aw, "val", scheme.bg, scheme.text, scheme.fg, kMidiPromptFont, false);
        if (auto* suffixLbl = dynamic_cast<juce::Label*> (aw->findChildWithID (kPromptSuffixLabelId)))
        {
            if (auto* te = aw->getTextEditor ("val"))
                suffixLbl->setFont (te->getFont());
        }

        if (layoutValueAndSuffix)
            layoutValueAndSuffix();

        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::MessageManager::callAsync ([safeAw]()
        {
            if (safeAw == nullptr)
                return;
            bringPromptWindowToFront (*safeAw);
            safeAw->repaint();
        });
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, aw] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);

            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);

            if (safeThis == nullptr || result != 1)
                return;

            const auto txt = aw->getTextEditorContents ("val").trim();
            const int ch = juce::jlimit (0, 16, txt.isEmpty() ? 0 : txt.getIntValue());
            safeThis->audioProcessor.setMidiChannel (ch);
            safeThis->midiChannelDisplay.setTooltip (formatMidiChannelTooltip (ch));
        }),
        false);
}

void FREQTRAudioProcessorEditor::openInfoPopup()
{
    lnf.setScheme (activeScheme);

    setPromptOverlayActive (true);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
    aw->setLookAndFeel (&lnf);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("GRAPHICS", 2);

    applyPromptShellSize (*aw);

    auto* bodyContent = new juce::Component();
    bodyContent->setComponentID ("bodyContent");

    auto infoFont = lnf.getAlertWindowMessageFont();
    infoFont.setHeight (infoFont.getHeight() * 1.45f);

    auto headingFont = infoFont;
    headingFont.setBold (true);
    headingFont.setHeight (infoFont.getHeight() * 1.25f);

    auto linkFont = infoFont;
    linkFont.setHeight (infoFont.getHeight() * 1.08f);

    auto poemFont = infoFont;
    poemFont.setItalic (true);

    auto xmlDoc = juce::XmlDocument::parse (InfoContent::xml);
    auto* contentNode = xmlDoc != nullptr ? xmlDoc->getChildByName ("content") : nullptr;

    if (contentNode != nullptr)
    {
        int elemIdx = 0;
        for (auto* node : contentNode->getChildIterator())
        {
            const auto tag  = node->getTagName();
            const auto text = node->getAllSubText().trim();
            const auto id   = tag + juce::String (elemIdx++);

            if (tag == "heading")
            {
                auto* l = new juce::Label (id, text);
                l->setComponentID (id);
                l->setJustificationType (juce::Justification::centred);
                applyLabelTextColour (*l, activeScheme.text);
                l->setFont (headingFont);
                bodyContent->addAndMakeVisible (l);
            }
            else if (tag == "text" || tag == "separator")
            {
                auto* l = new juce::Label (id, text);
                l->setComponentID (id);
                l->setJustificationType (juce::Justification::centred);
                applyLabelTextColour (*l, activeScheme.text);
                l->setFont (infoFont);
                l->setBorderSize (juce::BorderSize<int> (0));
                bodyContent->addAndMakeVisible (l);
            }
            else if (tag == "link")
            {
                const auto url = node->getStringAttribute ("url");
                auto* lnk = new juce::HyperlinkButton (text, juce::URL (url));
                lnk->setComponentID (id);
                lnk->setJustificationType (juce::Justification::centred);
                lnk->setColour (juce::HyperlinkButton::textColourId, activeScheme.text);
                lnk->setFont (linkFont, false, juce::Justification::centred);
                lnk->setTooltip ("");
                bodyContent->addAndMakeVisible (lnk);
            }
            else if (tag == "poem")
            {
                auto* l = new juce::Label (id, text);
                l->setComponentID (id);
                l->setJustificationType (juce::Justification::centred);
                applyLabelTextColour (*l, activeScheme.text);
                l->setFont (poemFont);
                l->setBorderSize (juce::BorderSize<int> (0, 0, 0, 0));
                l->getProperties().set ("poemPadFraction", 0.12f);
                bodyContent->addAndMakeVisible (l);
            }
            else if (tag == "spacer")
            {
                auto* l = new juce::Label (id, "");
                l->setComponentID (id);
                l->setFont (infoFont);
                l->setBorderSize (juce::BorderSize<int> (0));
                bodyContent->addAndMakeVisible (l);
            }
        }
    }

    auto* viewport = new juce::Viewport();
    viewport->setComponentID ("bodyViewport");
    viewport->setViewedComponent (bodyContent, true);
    viewport->setScrollBarsShown (true, false);
    viewport->setScrollBarThickness (8);
    viewport->setLookAndFeel (&lnf);
    aw->addAndMakeVisible (viewport);

    layoutInfoPopupContent (*aw);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [] (juce::AlertWindow& a)
        {
            layoutInfoPopupContent (a);
        });

        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
        aw->repaint();
    }

    juce::MessageManager::callAsync ([safeAw, safeThis]()
    {
        if (safeAw == nullptr || safeThis == nullptr)
            return;

        bringPromptWindowToFront (*safeAw);
        safeAw->repaint();
    });

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis = juce::Component::SafePointer<FREQTRAudioProcessorEditor> (this), aw] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);

            if (safeThis == nullptr)
                return;

            if (result == 2)
            {
                safeThis->openGraphicsPopup();
                return;
            }

            safeThis->setPromptOverlayActive (false);
        }));
}

void FREQTRAudioProcessorEditor::openGraphicsPopup()
{
    lnf.setScheme (activeScheme);

    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    crtEnabled = audioProcessor.getUiCrtEnabled();
    crtEffect.setEnabled (crtEnabled);
    applyActivePalette();

    setPromptOverlayActive (true);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
    juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
    aw->setLookAndFeel (&lnf);
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));

    auto labelFont = lnf.getAlertWindowMessageFont();
    labelFont.setHeight (labelFont.getHeight() * 1.20f);

    auto addPopupLabel = [this, aw] (const juce::String& id,
                                     const juce::String& text,
                                     juce::Font font,
                                     juce::Justification justification = juce::Justification::centredLeft)
    {
        auto* label = new PopupClickableLabel (id, text);
        label->setComponentID (id);
        label->setJustificationType (justification);
        applyLabelTextColour (*label, activeScheme.text);
        label->setBorderSize (juce::BorderSize<int> (0));
        label->setFont (font);
        label->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        aw->addAndMakeVisible (label);
        return label;
    };

    auto* defaultToggle = new juce::ToggleButton ("");
    defaultToggle->setComponentID ("paletteDefaultToggle");
    aw->addAndMakeVisible (defaultToggle);

    auto* defaultLabel = addPopupLabel ("paletteDefaultLabel", "DFLT", labelFont);

    auto* customToggle = new juce::ToggleButton ("");
    customToggle->setComponentID ("paletteCustomToggle");
    aw->addAndMakeVisible (customToggle);

    auto* customLabel = addPopupLabel ("paletteCustomLabel", "CSTM", labelFont);

    auto paletteTitleFont = labelFont;
    paletteTitleFont.setHeight (paletteTitleFont.getHeight() * 1.30f);
    addPopupLabel ("paletteTitle", "PALETTE", paletteTitleFont, juce::Justification::centredLeft);

    for (int i = 0; i < 2; ++i)
    {
        auto* dflt = new juce::TextButton();
        dflt->setComponentID ("defaultSwatch" + juce::String (i));
        dflt->setTooltip ("Default palette colour " + juce::String (i + 1));
        aw->addAndMakeVisible (dflt);

        auto* custom = new PopupSwatchButton();
        custom->setComponentID ("customSwatch" + juce::String (i));
        custom->setTooltip (colourToHexRgb (customPalette[(size_t) i]));
        aw->addAndMakeVisible (custom);
    }

    auto* fxToggle = new juce::ToggleButton ("");
    fxToggle->setComponentID ("fxToggle");
    fxToggle->setToggleState (crtEnabled, juce::dontSendNotification);
    fxToggle->onClick = [safeThis, fxToggle]()
    {
        if (safeThis == nullptr || fxToggle == nullptr)
            return;

        safeThis->applyCrtState (fxToggle->getToggleState());
        safeThis->audioProcessor.setUiCrtEnabled (safeThis->crtEnabled);
        safeThis->repaint();
    };
    aw->addAndMakeVisible (fxToggle);

    auto* fxLabel = addPopupLabel ("fxLabel", "GRAPHIC FX", labelFont);

    auto syncAndRepaintPopup = [safeThis, safeAw]()
    {
        if (safeThis == nullptr || safeAw == nullptr)
            return;

        syncGraphicsPopupState (*safeAw, safeThis->defaultPalette, safeThis->customPalette, safeThis->useCustomPalette);
        layoutGraphicsPopupContent (*safeAw);
        safeAw->repaint();
    };

    auto applyPaletteAndRepaint = [safeThis]()
    {
        if (safeThis == nullptr)
            return;

        safeThis->applyActivePalette();
        safeThis->repaint();
    };

    defaultToggle->onClick = [safeThis, defaultToggle, customToggle, applyPaletteAndRepaint, syncAndRepaintPopup]() mutable
    {
        if (safeThis == nullptr || defaultToggle == nullptr || customToggle == nullptr)
            return;

        safeThis->useCustomPalette = false;
        safeThis->audioProcessor.setUiUseCustomPalette (safeThis->useCustomPalette);
        defaultToggle->setToggleState (true, juce::dontSendNotification);
        customToggle->setToggleState (false, juce::dontSendNotification);
        applyPaletteAndRepaint();
        syncAndRepaintPopup();
    };

    customToggle->onClick = [safeThis, defaultToggle, customToggle, applyPaletteAndRepaint, syncAndRepaintPopup]() mutable
    {
        if (safeThis == nullptr || defaultToggle == nullptr || customToggle == nullptr)
            return;

        safeThis->useCustomPalette = true;
        safeThis->audioProcessor.setUiUseCustomPalette (safeThis->useCustomPalette);
        defaultToggle->setToggleState (false, juce::dontSendNotification);
        customToggle->setToggleState (true, juce::dontSendNotification);
        applyPaletteAndRepaint();
        syncAndRepaintPopup();
    };

    if (defaultLabel != nullptr && defaultToggle != nullptr)
        defaultLabel->onClick = [defaultToggle]() { defaultToggle->triggerClick(); };

    if (customLabel != nullptr && customToggle != nullptr)
        customLabel->onClick = [customToggle]() { customToggle->triggerClick(); };

    if (fxLabel != nullptr && fxToggle != nullptr)
        fxLabel->onClick = [fxToggle]() { fxToggle->triggerClick(); };

    for (int i = 0; i < 2; ++i)
    {
        if (auto* customSwatch = dynamic_cast<PopupSwatchButton*> (aw->findChildWithID ("customSwatch" + juce::String (i))))
        {
            customSwatch->onLeftClick = [safeThis, safeAw, i]()
            {
                if (safeThis == nullptr)
                    return;

                auto& rng = juce::Random::getSystemRandom();
                const auto randomColour = juce::Colour::fromRGB ((juce::uint8) rng.nextInt (256),
                                                                 (juce::uint8) rng.nextInt (256),
                                                                 (juce::uint8) rng.nextInt (256));

                safeThis->customPalette[(size_t) i] = randomColour;
                safeThis->audioProcessor.setUiCustomPaletteColour (i, randomColour);
                if (safeThis->useCustomPalette)
                {
                    safeThis->applyActivePalette();
                    safeThis->repaint();
                }

                if (safeAw != nullptr)
                {
                    syncGraphicsPopupState (*safeAw, safeThis->defaultPalette, safeThis->customPalette, safeThis->useCustomPalette);
                    layoutGraphicsPopupContent (*safeAw);
                    safeAw->repaint();
                }
            };

            customSwatch->onRightClick = [safeThis, safeAw, i]()
            {
                if (safeThis == nullptr)
                    return;

                const auto scheme = safeThis->activeScheme;

                auto* colorAw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
                colorAw->setLookAndFeel (&safeThis->lnf);
                colorAw->addTextEditor ("hex", colourToHexRgb (safeThis->customPalette[(size_t) i]), juce::String());

                if (auto* te = colorAw->getTextEditor ("hex"))
                    te->setInputFilter (new HexInputFilter(), true);

                colorAw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                colorAw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

                styleAlertButtons (*colorAw, safeThis->lnf);

                applyPromptShellSize (*colorAw);
                layoutAlertWindowButtons (*colorAw);

                const juce::Font& kHexPromptFont = kBoldFont40();

                preparePromptTextEditor (*colorAw, "hex", scheme.bg, scheme.text, scheme.fg, kHexPromptFont, true, 6);

                if (safeThis != nullptr)
                {
                    fitAlertWindowToEditor (*colorAw, safeThis.getComponent(), [&] (juce::AlertWindow& a)
                    {
                        layoutAlertWindowButtons (a);
                        preparePromptTextEditor (a, "hex", scheme.bg, scheme.text, scheme.fg, kHexPromptFont, true, 6);
                    });

                    embedAlertWindowInOverlay (safeThis.getComponent(), colorAw, true);
                }
                else
                {
                    colorAw->centreAroundComponent (safeThis.getComponent(), colorAw->getWidth(), colorAw->getHeight());
                    bringPromptWindowToFront (*colorAw);
                    if (safeThis != nullptr && safeThis->tooltipWindow)
                        safeThis->tooltipWindow->toFront (true);
                    colorAw->repaint();
                }

                preparePromptTextEditor (*colorAw, "hex", scheme.bg, scheme.text, scheme.fg, kHexPromptFont, true, 6);

                juce::Component::SafePointer<juce::AlertWindow> safeColorAw (colorAw);
                juce::MessageManager::callAsync ([safeColorAw]()
                {
                    if (safeColorAw == nullptr)
                        return;
                    bringPromptWindowToFront (*safeColorAw);
                    safeColorAw->repaint();
                });

                colorAw->enterModalState (true,
                    juce::ModalCallbackFunction::create ([safeThis, safeAw, colorAw, i] (int result) mutable
                    {
                        std::unique_ptr<juce::AlertWindow> killer (colorAw);
                        if (safeThis == nullptr)
                            return;

                        if (result != 1)
                            return;

                        juce::Colour parsed;
                        if (! tryParseHexColour (killer->getTextEditorContents ("hex"), parsed))
                            return;

                        safeThis->customPalette[(size_t) i] = parsed;
                        safeThis->audioProcessor.setUiCustomPaletteColour (i, parsed);
                        if (safeThis->useCustomPalette)
                        {
                            safeThis->applyActivePalette();
                            safeThis->repaint();
                        }

                        if (safeAw != nullptr)
                        {
                            syncGraphicsPopupState (*safeAw, safeThis->defaultPalette, safeThis->customPalette, safeThis->useCustomPalette);
                            layoutGraphicsPopupContent (*safeAw);
                            safeAw->repaint();
                        }
                    }));
            };
        }
    }

    applyPromptShellSize (*aw);
    syncGraphicsPopupState (*aw, defaultPalette, customPalette, useCustomPalette);
    layoutGraphicsPopupContent (*aw);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [&] (juce::AlertWindow& a)
        {
            syncGraphicsPopupState (a, defaultPalette, customPalette, useCustomPalette);
            layoutGraphicsPopupContent (a);
        });
    }
    if (safeThis != nullptr)
    {
        embedAlertWindowInOverlay (safeThis.getComponent(), aw);

        juce::MessageManager::callAsync ([safeAw, safeThis]()
        {
            if (safeAw == nullptr || safeThis == nullptr)
                return;

            safeAw->toFront (false);
            safeAw->repaint();
        });
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
        aw->repaint();
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, aw] (int) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);
            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);
        }));
}

//========================== Mouse interactions ==========================

void FREQTRAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

    const auto pt = e.getEventRelativeTo (this).getPosition();

    // Right-click on value area → numeric entry
    if (e.mods.isPopupMenu())
    {
        if (auto* slider = getSliderForValueAreaPoint (pt))
        {
            openNumericEntryPopupForSlider (*slider);
            return;
        }
    }

    // Gear icon: left-click → info popup, right-click → graphics popup
    {
        auto infoArea = getInfoIconArea();
        if (crtEnabled)
            infoArea = infoArea.expanded (4, 0);
        if (infoArea.contains (pt))
        {
            if (e.mods.isPopupMenu())
                openGraphicsPopup();
            else
                openInfoPopup();
            return;
        }
    }

    // SYNC label click → toggle
    if (getSyncLabelArea().contains (pt))
    {
        syncButton.setToggleState (! syncButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    // MIDI label click → toggle (left), channel prompt (right)
    if (getMidiLabelArea().contains (pt) || midiChannelDisplay.getBounds().contains (pt))
    {
        if (e.mods.isPopupMenu())
            openMidiChannelPrompt();
        else
            midiButton.setToggleState (! midiButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    // Left-click on value area → remember value for drag
    if (auto* slider = getSliderForValueAreaPoint (pt))
    {
        dragStartValue.store (slider->getValue(), std::memory_order_relaxed);
    }
}

void FREQTRAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

    if (auto* slider = getSliderForValueAreaPoint (e.getPosition()))
    {
        slider->setValue (slider->getDoubleClickReturnValue(), juce::sendNotificationSync);
        return;
    }
}

void FREQTRAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);

    if (auto* slider = getSliderForValueAreaPoint (e.getMouseDownPosition()))
    {
        const double delta = (double) -e.getDistanceFromDragStartY() * 0.002;
        const double range = slider->getMaximum() - slider->getMinimum();
        const double baseValue = dragStartValue.load (std::memory_order_relaxed);
        slider->setValue (baseValue + delta * range, juce::sendNotificationSync);
    }
}

//========================== Layout builders ==========================

namespace
{
    constexpr int kValueAreaHeightPx = 44;
    constexpr int kValueAreaRightMarginPx = 24;
    constexpr int kToggleLabelGapPx = 4;
    constexpr int kResizerCornerPx = 22;
    constexpr int kToggleBoxPx = 72;
    constexpr int kTitleAreaExtraHeightPx = 4;
    constexpr int kTitleRightGapToInfoPx = 8;
    constexpr int kVersionGapPx = 8;
    constexpr int kToggleLegendCollisionPadPx = 6;
    constexpr int kNumBars = 7;

    int getToggleVisualBoxSidePx (const juce::Component& button)
    {
        const int h = button.getHeight();
        return juce::jlimit (14, juce::jmax (14, h - 2), (int) std::lround ((double) h * 0.65));
    }

    int getToggleVisualBoxLeftPx (const juce::Component& button)
    {
        return button.getX() + 2;
    }

    juce::Rectangle<int> makeToggleLabelArea (const juce::Component& button,
                                              int collisionRight,
                                              const juce::String& fullLabel,
                                              const juce::String& shortLabel)
    {
        const auto b = button.getBounds();
        const int visualRight = getToggleVisualBoxLeftPx (button) + getToggleVisualBoxSidePx (button);
        const int x = visualRight + kToggleLabelGapPx;

        const auto& labelFont = kBoldFont40();
        const int fullW  = stringWidth (labelFont, fullLabel) + 2;
        const int shortW = stringWidth (labelFont, shortLabel) + 2;
        const int maxW   = juce::jmax (0, collisionRight - x);

        const int w = (fullW <= maxW) ? fullW : juce::jmin (shortW, maxW);
        return { x, b.getY(), w, b.getHeight() };
    }

    juce::String chooseToggleLabel (const juce::Component& button,
                                   int collisionRight,
                                   const juce::String& fullLabel,
                                   const juce::String& shortLabel)
    {
        const int visualRight = getToggleVisualBoxLeftPx (button) + getToggleVisualBoxSidePx (button);
        const int x = visualRight + kToggleLabelGapPx;
        const auto& labelFont = kBoldFont40();
        const int fullW = stringWidth (labelFont, fullLabel) + 2;
        return (fullW <= juce::jmax (0, collisionRight - x)) ? fullLabel : shortLabel;
    }
}

FREQTRAudioProcessorEditor::HorizontalLayoutMetrics
FREQTRAudioProcessorEditor::buildHorizontalLayout (int editorW, int valueColW)
{
    HorizontalLayoutMetrics m;
    m.barW = (int) std::round (editorW * 0.455);
    m.valuePad = (int) std::round (editorW * 0.02);
    m.valueW = valueColW;
    m.contentW = m.barW + m.valuePad + m.valueW;
    m.leftX = juce::jmax (6, (editorW - m.contentW) / 2);
    return m;
}

FREQTRAudioProcessorEditor::VerticalLayoutMetrics
FREQTRAudioProcessorEditor::buildVerticalLayout (int editorH, int biasY)
{
    VerticalLayoutMetrics m;
    m.rhythm = juce::jlimit (6, 16, (int) std::round (editorH * 0.018));
    const int nominalBarH = juce::jlimit (14, 120, m.rhythm * 6);
    const int nominalGapY = juce::jmax (4, m.rhythm * 4);

    m.titleH = juce::jlimit (24, 56, m.rhythm * 4);
    m.titleAreaH = m.titleH + 4;
    const int computedTitleTopPad = 6 + biasY;
    m.titleTopPad = (computedTitleTopPad > 8) ? computedTitleTopPad : 8;
    const int titleGap = m.titleTopPad;
    m.topMargin = m.titleTopPad + m.titleAreaH + titleGap;
    m.betweenSlidersAndButtons = juce::jmax (8, m.rhythm * 2);
    m.bottomMargin = m.titleTopPad;

    m.box = juce::jlimit (40, kToggleBoxPx, (int) std::round (editorH * 0.085));
    m.btnRowGap = juce::jlimit (4, 14, (int) std::round (editorH * 0.008));
    // Only 1 button row for FREQ-TR (SYNC + MIDI)
    m.btnRow1Y = editorH - m.bottomMargin - m.box;
    m.availableForSliders = juce::jmax (40, m.btnRow1Y - m.betweenSlidersAndButtons - m.topMargin);

    const int nominalStack = kNumBars * nominalBarH + (kNumBars - 1) * nominalGapY;
    const double stackScale = nominalStack > 0 ? juce::jmin (1.0, (double) m.availableForSliders / (double) nominalStack)
                                               : 1.0;

    m.barH = juce::jmax (14, (int) std::round (nominalBarH * stackScale));
    m.gapY = juce::jmax (4,  (int) std::round (nominalGapY * stackScale));

    auto stackHeight = [&]() { return kNumBars * m.barH + (kNumBars - 1) * m.gapY; };

    while (stackHeight() > m.availableForSliders && m.gapY > 4)
        --m.gapY;

    while (stackHeight() > m.availableForSliders && m.barH > 14)
        --m.barH;

    m.topY = m.topMargin;
    return m;
}

void FREQTRAudioProcessorEditor::updateCachedLayout()
{
    cachedHLayout_ = buildHorizontalLayout (getWidth(), getTargetValueColumnWidth());
    cachedVLayout_ = buildVerticalLayout (getHeight(), kLayoutVerticalBiasPx);

    const juce::Slider* sliders[kNumBars] = { &freqSlider, &modSlider, &engineSlider,
                                               &styleSlider, &shapeSlider, &polaritySlider, &mixSlider };

    for (int i = 0; i < kNumBars; ++i)
    {
        const auto& bb = sliders[i]->getBounds();
        const int valueX = bb.getRight() + cachedHLayout_.valuePad;
        const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
        const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
        const int y    = bb.getCentreY() - (kValueAreaHeightPx / 2);
        cachedValueAreas_[(size_t) i] = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
    }
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
        "5000.0 Hz FREQ", "5.00 kHz FREQ",
        "1/64. FREQ",
        "X4.00 MOD",
        "FREQ SHIFT ENGINE",
        "STEREO STYLE",
        "SQR SHAPE",
        "-1.00 POLARITY",
        "100% MIX"
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
    const int valueX = barBounds.getRight() + cachedHLayout_.valuePad;
    const int valueW = juce::jmin (cachedHLayout_.valueW, juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx));
    const int y = barBounds.getCentreY() - (kValueAreaHeightPx / 2);
    return { valueX, y, juce::jmax (0, valueW), kValueAreaHeightPx };
}

juce::Slider* FREQTRAudioProcessorEditor::getSliderForValueAreaPoint (juce::Point<int> p)
{
    juce::Slider* sliders[kNumBars] = { &freqSlider, &modSlider, &engineSlider,
                                         &styleSlider, &shapeSlider, &polaritySlider, &mixSlider };

    for (int i = 0; i < kNumBars; ++i)
        if (cachedValueAreas_[(size_t) i].contains (p))
            return sliders[i];

    return nullptr;
}

//========================== Label areas ==========================

juce::Rectangle<int> FREQTRAudioProcessorEditor::getSyncLabelArea() const
{
    return makeToggleLabelArea (syncButton, midiButton.getX() - kToggleLegendCollisionPadPx, "SYNC", "SYN");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getMidiLabelArea() const
{
    return makeToggleLabelArea (midiButton, getWidth() - kToggleLegendCollisionPadPx, "MIDI", "MD");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getInfoIconArea() const
{
    const int contentRight = cachedValueAreas_[0].getRight();
    const int titleH = cachedVLayout_.titleH;
    const int titleY = cachedVLayout_.titleTopPad;
    const int titleAreaH = cachedVLayout_.titleAreaH;
    const int size = juce::jlimit (20, 36, titleH);

    const int x = contentRight - size;
    const int y = titleY + juce::jmax (0, (titleAreaH - size) / 2);
    return { x, y, size, size };
}

void FREQTRAudioProcessorEditor::updateInfoIconCache()
{
    const auto area = getInfoIconArea();
    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return;

    const float cx = area.getCentreX() - (float) area.getX();
    const float cy = area.getCentreY() - (float) area.getY();
    const float radius = (float) area.getWidth() * 0.38f;

    cachedInfoGearPath.clear();
    const int teeth = 8;
    const float outerR = radius;
    const float innerR = radius * 0.68f;
    for (int i = 0; i < teeth * 2; ++i)
    {
        const float angle = (float) i * juce::MathConstants<float>::pi / (float) teeth
                          - juce::MathConstants<float>::halfPi;
        const float r = (i % 2 == 0) ? outerR : innerR;
        const float px = cx + r * std::cos (angle);
        const float py = cy + r * std::sin (angle);
        if (i == 0)
            cachedInfoGearPath.startNewSubPath (px, py);
        else
            cachedInfoGearPath.lineTo (px, py);
    }
    cachedInfoGearPath.closeSubPath();

    const float holeR = radius * 0.28f;
    cachedInfoGearHole = { cx - holeR, cy - holeR, holeR * 2.0f, holeR * 2.0f };
}

//========================== Paint ==========================

void FREQTRAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int W = getWidth();
    const auto& horizontalLayout = cachedHLayout_;
    const auto& verticalLayout   = cachedVLayout_;

    const auto scheme = activeScheme;

    g.fillAll (scheme.bg);
    g.setColour (scheme.text);

    constexpr float baseFontPx = 40.0f;
    constexpr float minFontPx  = 18.0f;
    constexpr float fullShrinkFloor = baseFontPx * 0.75f;
    g.setFont (kBoldFont40());

    auto tryDrawLegend = [&] (const juce::Rectangle<int>& area,
                              const juce::String& text,
                              float shrinkFloor) -> bool
    {
        auto t = text.trim();
        if (t.isEmpty() || area.getWidth() <= 2 || area.getHeight() <= 2)
            return false;

        const int split = t.lastIndexOfChar (' ');
        if (split <= 0 || split >= t.length() - 1)
        {
            g.setFont (kBoldFont40());
            return drawIfFitsWithOptionalShrink (g, area, t, baseFontPx, shrinkFloor);
        }

        const auto value  = t.substring (0, split).trimEnd();
        const auto suffix = t.substring (split + 1).trimStart();

        g.setFont (kBoldFont40());
        if (drawValueWithRightAlignedSuffix (g, area, value, suffix, false,
                                              baseFontPx, shrinkFloor))
        {
            g.setColour (scheme.text);
            return true;
        }
        return false;
    };

    auto drawLegendForMode = [&] (const juce::Rectangle<int>& area,
                                  const juce::String& fullLegend,
                                  const juce::String& shortLegend,
                                  const juce::String& intOnlyLegend)
    {
        if (tryDrawLegend (area, fullLegend, fullShrinkFloor))
            return;

        if (tryDrawLegend (area, shortLegend, minFontPx))
            return;

        g.setFont (kBoldFont40());
        drawValueNoEllipsis (g, area, intOnlyLegend, juce::String(), intOnlyLegend, baseFontPx, minFontPx);
        g.setColour (scheme.text);
    };

    // ── Title ──
    {
        const int titleH = verticalLayout.titleH;
        const int contentW = horizontalLayout.contentW;
        const int leftX = horizontalLayout.leftX;

        const int titleX = juce::jlimit (0, juce::jmax (0, W - 1), leftX);
        const int titleW = juce::jmax (0, juce::jmin (contentW, W - titleX));
        const int titleY = verticalLayout.titleTopPad;

        auto titleFont = g.getCurrentFont();
        titleFont.setHeight ((float) titleH);
        g.setFont (titleFont);

        const auto titleArea = juce::Rectangle<int> (titleX, titleY, titleW, titleH + kTitleAreaExtraHeightPx);
        const juce::String titleText ("FREQ-TR");

        g.drawText (titleText, titleArea.getX(), titleArea.getY(), titleArea.getWidth(), titleArea.getHeight(), juce::Justification::left, false);

        const auto infoIconArea = getInfoIconArea();
        const int titleRightLimit = infoIconArea.getX() - kTitleRightGapToInfoPx;
        const int titleMaxW = juce::jmax (0, titleRightLimit - titleArea.getX());
        const int titleBaseW = stringWidth (titleFont, titleText);
        const int originalTitleLimitW = juce::jmax (0, juce::jmin (titleW, horizontalLayout.barW));
        const bool originalWouldClipTitle = titleBaseW > originalTitleLimitW;

        if (titleMaxW > 0 && (originalWouldClipTitle || titleBaseW > titleMaxW))
        {
            auto fittedTitleFont = titleFont;
            fittedTitleFont.setHorizontalScale (1.0f);
            const float titleMinScale = juce::jlimit (0.4f, 1.0f, 12.0f / (float) titleH);
            for (float s = 1.0f; s >= titleMinScale; s -= 0.025f)
            {
                fittedTitleFont.setHorizontalScale (s);
                if (stringWidth (fittedTitleFont, titleText) <= titleMaxW)
                    break;
            }

            g.setColour (scheme.text);
            g.setFont (fittedTitleFont);
            g.drawText (titleText, titleArea.getX(), titleArea.getY(), titleMaxW, titleArea.getHeight(), juce::Justification::left, false);
        }

        g.setColour (scheme.text);

        auto versionFont = juce::Font (juce::FontOptions (juce::jmax (10.0f, (float) titleH * UiMetrics::versionFontRatio)).withStyle ("Bold"));
        g.setFont (versionFont);

        const int versionH = juce::jlimit (10, infoIconArea.getHeight(), (int) std::round ((double) infoIconArea.getHeight() * UiMetrics::versionHeightRatio));
        const int versionY = infoIconArea.getBottom() - versionH;

        const int desiredVersionW = juce::jlimit (28, 64, (int) std::round ((double) infoIconArea.getWidth() * UiMetrics::versionDesiredWidthRatio));
        const int versionRight = infoIconArea.getX() - kVersionGapPx;
        const int versionLeftLimit = titleArea.getX();
        const int versionX = juce::jmax (versionLeftLimit, versionRight - desiredVersionW);
        const int versionW = juce::jmax (0, versionRight - versionX);

        if (versionW > 0)
            g.drawText (juce::String ("v") + InfoContent::version,
                        versionX, versionY, versionW, versionH,
                        juce::Justification::bottomRight, false);

        g.setFont (kBoldFont40());
    }

    // ── Bar legends ──
    {
        const juce::String* fullTexts[kNumBars]  = { &cachedFreqTextFull, &cachedModTextFull, &cachedEngineTextFull,
                                                      &cachedStyleTextFull, &cachedShapeTextFull, &cachedPolarityTextFull, &cachedMixTextFull };
        const juce::String* shortTexts[kNumBars] = { &cachedFreqTextShort, &cachedModTextShort, &cachedEngineTextShort,
                                                      &cachedStyleTextShort, &cachedShapeTextShort, &cachedPolarityTextShort, &cachedMixTextShort };
        const juce::String intTexts[kNumBars] = {
            juce::String ((int) freqSlider.getValue()) + "Hz",
            juce::String ((int) modSlider.getValue()),
            juce::String ((int) std::lround (engineSlider.getValue() * 100.0)) + "%",
            juce::String ((int) styleSlider.getValue()),
            getShapeTextShort(),
            juce::String (polaritySlider.getValue(), 1),
            juce::String ((int) std::lround (mixSlider.getValue() * 100.0)) + "%"
        };

        for (int i = 0; i < kNumBars; ++i)
            drawLegendForMode (cachedValueAreas_[(size_t) i], *fullTexts[i], *shortTexts[i], intTexts[i]);
    }

    // ── Button labels ──
    {
        const auto& labelFont = kBoldFont40();
        g.setFont (labelFont);

        const int syncCR  = midiButton.getX() - kToggleLegendCollisionPadPx;
        const int midiCR  = W - kToggleLegendCollisionPadPx;

        auto drawToggleLabel = [&] (const juce::ToggleButton& btn,
                                     const juce::String& text,
                                     int clipRight)
        {
            const int boxSide = juce::jlimit (14, juce::jmax (14, cachedVLayout_.box - 2),
                                              (int) std::lround ((double) cachedVLayout_.box * 0.65));
            const int labelX  = btn.getX() + boxSide + kToggleLabelGapPx + 2;
            const int labelW  = juce::jmax (0, clipRight - labelX);
            if (labelW > 0)
            {
                const auto area = juce::Rectangle<int> (labelX, btn.getY(), labelW, cachedVLayout_.box);
                drawIfFitsWithOptionalShrink (g, area, text, baseFontPx, minFontPx);
                g.setColour (scheme.text);
            }
        };

        drawToggleLabel (syncButton, "SYNC", syncCR);
        drawToggleLabel (midiButton, "MIDI", midiCR);
    }

    // ── Info gear icon ──
    {
        const auto infoArea = getInfoIconArea();
        g.saveState();
        g.setOrigin (infoArea.getX(), infoArea.getY());
        g.setColour (scheme.text);
        g.fillPath (cachedInfoGearPath);
        g.setColour (scheme.bg);
        g.fillEllipse (cachedInfoGearHole);
        g.restoreState();
    }
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
    const auto verticalLayout = buildVerticalLayout (H, kLayoutVerticalBiasPx);

    // Position 7 sliders
    freqSlider.setBounds     (horizontalLayout.leftX, verticalLayout.topY + 0 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    modSlider.setBounds      (horizontalLayout.leftX, verticalLayout.topY + 1 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    engineSlider.setBounds   (horizontalLayout.leftX, verticalLayout.topY + 2 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    styleSlider.setBounds    (horizontalLayout.leftX, verticalLayout.topY + 3 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    shapeSlider.setBounds    (horizontalLayout.leftX, verticalLayout.topY + 4 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    polaritySlider.setBounds (horizontalLayout.leftX, verticalLayout.topY + 5 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);
    mixSlider.setBounds      (horizontalLayout.leftX, verticalLayout.topY + 6 * (verticalLayout.barH + verticalLayout.gapY), horizontalLayout.barW, verticalLayout.barH);

    // Button area: 1 row — SYNC (left) + MIDI (right)
    const int buttonAreaX = horizontalLayout.leftX;

    const int toggleVisualSide = juce::jlimit (14,
                                               juce::jmax (14, verticalLayout.box - 2),
                                               (int) std::lround ((double) verticalLayout.box * 0.65));
    const int toggleHitW = toggleVisualSide + 6;

    const int leftBlockX = buttonAreaX;
    const int rightBlockX = horizontalLayout.leftX + horizontalLayout.barW + horizontalLayout.valuePad;

    syncButton.setBounds (leftBlockX,  verticalLayout.btnRow1Y, toggleHitW, verticalLayout.box);
    midiButton.setBounds (rightBlockX, verticalLayout.btnRow1Y, toggleHitW, verticalLayout.box);

    // MIDI tooltip overlay
    {
        const auto midiLabelRect = getMidiLabelArea();
        midiChannelDisplay.setBounds (midiLabelRect);
    }

    if (resizerCorner != nullptr)
        resizerCorner->setBounds (W - kResizerCornerPx, H - kResizerCornerPx, kResizerCornerPx, kResizerCornerPx);

    promptOverlay.setBounds (getLocalBounds());
    if (promptOverlayActive)
        promptOverlay.toFront (false);

    updateCachedLayout();
    updateInfoIconCache();
    crtEffect.setResolution (static_cast<float> (W), static_cast<float> (H));
}
