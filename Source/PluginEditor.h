#pragma once

#include <cstdint>
#include <atomic>
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CrtEffect.h"
#include "TRSharedUI.h"

class FREQTRAudioProcessorEditor : public juce::AudioProcessorEditor,
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

    void openNumericEntryPopupForSlider (juce::Slider& s);
    void openMidiChannelPrompt();
    void openRetrigPrompt();
    void scheduleRetrigTipAutoHide();
    void openInfoPopup();
    void openGraphicsPopup();
    void setPromptOverlayActive (bool shouldBeActive);

    FREQTRAudioProcessor& audioProcessor;

    class BarSlider : public juce::Slider
    {
    public:
        using juce::Slider::Slider;

        void setOwner (FREQTRAudioProcessorEditor* o) { owner = o; }
        void setAllowNumericPopup (bool allow) { allowNumericPopup = allow; }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && allowNumericPopup)
            {
                if (owner != nullptr)
                    owner->openNumericEntryPopupForSlider (*this);
                return;
            }
            juce::Slider::mouseDown (e);
        }

        juce::String getTextFromValue (double v) override
        {
            if (owner != nullptr && (this == &owner->inputSlider || this == &owner->outputSlider))
            {
                juce::String t (v, 1);
                if (t.containsChar ('.'))
                {
                    while (t.endsWithChar ('0')) t = t.dropLastCharacters (1);
                    if (t.endsWithChar ('.')) t = t.dropLastCharacters (1);
                }
                return t;
            }

            if (owner != nullptr && (this == &owner->mixSlider))
            {
                double percent = v * 100.0;
                juce::String t (percent, 4);
                if (t.containsChar ('.'))
                {
                    while (t.endsWithChar ('0')) t = t.dropLastCharacters (1);
                    if (t.endsWithChar ('.')) t = t.dropLastCharacters (1);
                }
                return t;
            }

            if (owner != nullptr && this == &owner->freqSlider)
            {
                const double rounded1 = std::round (v * 10.0) / 10.0;
                return juce::String (rounded1, 1);
            }

            if (owner != nullptr && this == &owner->engineSlider)
            {
                double percent = v * 100.0;
                juce::String t (percent, 4);
                if (t.containsChar ('.'))
                {
                    while (t.endsWithChar ('0')) t = t.dropLastCharacters (1);
                    if (t.endsWithChar ('.')) t = t.dropLastCharacters (1);
                }
                return t;
            }

            // For polarity (-1 to 1)
            if (owner != nullptr && this == &owner->polaritySlider)
            {
                const double rounded2 = std::round (v * 100.0) / 100.0;
                return juce::String (rounded2, 2);
            }

            juce::String t = juce::Slider::getTextFromValue (v);
            int dot = t.indexOfChar ('.');
            if (dot >= 0)
                t = t.substring (0, dot + 1 + 4);
            return t;
        }

    private:
        FREQTRAudioProcessorEditor* owner = nullptr;
        bool allowNumericPopup = true;
    };

    // 9 bars: INPUT, OUTPUT, MIX, FREQ, MOD, ENGINE, SHAPE, POLARITY, STYLE
    BarSlider inputSlider;
    BarSlider outputSlider;
    BarSlider mixSlider;
    BarSlider freqSlider;
    BarSlider modSlider;
    BarSlider engineSlider;
    BarSlider shapeSlider;
    BarSlider polaritySlider;
    BarSlider styleSlider;

    // 2 checkboxes: SYNC, MIDI
    juce::ToggleButton syncButton;
    juce::ToggleButton midiButton;

    // 2 checkboxes: ALIGN, PDC
    juce::ToggleButton alignButton;
    juce::ToggleButton pdcButton;

    juce::Label midiChannelDisplay;
    juce::Label retrigDisplay;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> freqAttachment;
    std::unique_ptr<SliderAttachment> freqSyncAttachment;
    std::unique_ptr<SliderAttachment> modAttachment;
    std::unique_ptr<SliderAttachment> engineAttachment;
    std::unique_ptr<SliderAttachment> styleAttachment;
    std::unique_ptr<SliderAttachment> shapeAttachment;
    std::unique_ptr<SliderAttachment> polarityAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> inputAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;

    std::unique_ptr<ButtonAttachment> syncAttachment;
    std::unique_ptr<ButtonAttachment> midiAttachment;
    std::unique_ptr<ButtonAttachment> alignAttachment;
    std::unique_ptr<ButtonAttachment> pdcAttachment;

    juce::ComponentBoundsConstrainer resizeConstrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizerCorner;

    using FREQScheme = TR::TRScheme;

    FREQScheme activeScheme;

    struct HorizontalLayoutMetrics
    {
        int barW = 0;
        int valuePad = 0;
        int valueW = 0;
        int contentW = 0;
        int leftX = 0;
    };

    struct VerticalLayoutMetrics
    {
        int rhythm = 0;
        int titleH = 0;
        int titleAreaH = 0;
        int titleTopPad = 0;
        int topMargin = 0;
        int betweenSlidersAndButtons = 0;
        int bottomMargin = 0;
        int box = 0;
        int btnRow1Y = 0;
        int btnRow2Y = 0;
        int btnRowGap = 0;
        int availableForSliders = 0;
        int barH = 0;
        int gapY = 0;
        int topY = 0;
        int toggleBarH = 0;
        int toggleBarY = 0;
    };

    static HorizontalLayoutMetrics buildHorizontalLayout (int editorW, int valueColW);
    static VerticalLayoutMetrics buildVerticalLayout (int editorH, int biasY, bool ioExpanded);
    void updateCachedLayout();

    class MinimalLNF : public juce::LookAndFeel_V4
    {
    public:
        void setScheme (const FREQScheme& s)
        {
            scheme = s;
            TR::applySchemeToLookAndFeel (*this, scheme);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               const juce::Slider::SliderStyle style, juce::Slider& slider) override;

        void drawTickBox (juce::Graphics& g, juce::Component&,
                          float x, float y, float w, float h,
                          bool ticked, bool isEnabled,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

        void drawButtonBackground (juce::Graphics& g,
                       juce::Button& button,
                       const juce::Colour& backgroundColour,
                       bool shouldDrawButtonAsHighlighted,
                       bool shouldDrawButtonAsDown) override;

        void drawAlertBox (juce::Graphics& g,
                   juce::AlertWindow& alert,
                   const juce::Rectangle<int>& textArea,
                   juce::TextLayout& textLayout) override;

        void drawBubble (juce::Graphics&,
                 juce::BubbleComponent&,
                 const juce::Point<float>& tip,
                 const juce::Rectangle<float>& body) override;

        void drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar,
                    int x, int y, int width, int height,
                    bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                    bool isMouseOver, bool isMouseDown) override;

        int getMinimumScrollbarThumbSize (juce::ScrollBar&) override { return 16; }
        int getScrollbarButtonSize (juce::ScrollBar&) override      { return 0; }

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getAlertWindowMessageFont() override;
        juce::Font getLabelFont (juce::Label& label) override;
        juce::Font getSliderPopupFont (juce::Slider&) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                               juce::Point<int> screenPos,
                               juce::Rectangle<int> parentArea) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    private:
        FREQScheme scheme {
            juce::Colours::black,
            juce::Colours::white,
            juce::Colours::white,
            juce::Colours::white
        };
    };

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

    juce::String getEngineText() const;
    juce::String getEngineTextShort() const;

    juce::String getStyleText() const;
    juce::String getStyleTextShort() const;

    juce::String getShapeText() const;
    juce::String getShapeTextShort() const;

    juce::String getPolarityText() const;
    juce::String getPolarityTextShort() const;

    juce::String getMixText() const;
    juce::String getMixTextShort() const;

    juce::String getInputText() const;
    juce::String getInputTextShort() const;

    juce::String getOutputText() const;
    juce::String getOutputTextShort() const;

    int getTargetValueColumnWidth() const;

    void sliderValueChanged (juce::Slider* slider) override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    void applyPersistedUiStateFromProcessor (bool applySize, bool applyPaletteAndFx);
    void applyLabelTextColour (juce::Label& label, juce::Colour colour);

    template <typename T>
    friend void TR::embedAlertWindowInOverlay (T*, juce::AlertWindow*, bool);

    juce::Rectangle<int> getValueAreaFor (const juce::Rectangle<int>& barBounds) const;
    juce::Slider* getSliderForValueAreaPoint (juce::Point<int> p);
    juce::Rectangle<int> getSyncLabelArea() const;
    juce::Rectangle<int> getMidiLabelArea() const;
    juce::Rectangle<int> getAlignLabelArea() const;
    juce::Rectangle<int> getPdcLabelArea() const;
    juce::Rectangle<int> getInfoIconArea() const;
    void updateInfoIconCache();
    bool refreshLegendTextCache();
    juce::Rectangle<int> getRowRepaintBounds (const juce::Slider& s) const;
    void applyActivePalette();
    void applyCrtState (bool enabled);

    juce::Path cachedInfoGearPath;
    juce::Rectangle<float> cachedInfoGearHole;

    juce::String cachedFreqTextFull;
    juce::String cachedFreqTextShort;
    juce::String cachedModTextFull;
    juce::String cachedModTextShort;
    juce::String cachedEngineTextFull;
    juce::String cachedEngineTextShort;
    juce::String cachedStyleTextFull;
    juce::String cachedStyleTextShort;
    juce::String cachedShapeTextFull;
    juce::String cachedShapeTextShort;
    juce::String cachedPolarityTextFull;
    juce::String cachedPolarityTextShort;
    juce::String cachedMixTextFull;
    juce::String cachedMixTextShort;

    juce::String cachedInputTextFull;
    juce::String cachedInputTextShort;
    juce::String cachedInputIntOnly;
    juce::String cachedOutputTextFull;
    juce::String cachedOutputTextShort;
    juce::String cachedOutputIntOnly;

    juce::String cachedMidiDisplay;

    mutable std::uint64_t cachedValueColumnWidthKey = 0;
    mutable int cachedValueColumnWidth = 90;

    HorizontalLayoutMetrics cachedHLayout_;
    VerticalLayoutMetrics cachedVLayout_;
    std::array<juce::Rectangle<int>, 9> cachedValueAreas_;

    // IO collapsible section state
    juce::Rectangle<int> cachedToggleBarArea_;
    bool ioSectionExpanded_ = false;

    static constexpr double kDefaultPolarity = (double) FREQTRAudioProcessor::kPolarityDefault;

    static constexpr int kMinW = 360;
    static constexpr int kMinH = 540;
    static constexpr int kMaxW = 800;
    static constexpr int kMaxH = 540;

    static constexpr int kLayoutVerticalBiasPx = 10;

    bool promptOverlayActive = false;
    bool suppressSizePersistence = false;
    int lastPersistedEditorW = -1;
    int lastPersistedEditorH = -1;
    std::atomic<uint32_t> lastUserInteractionMs { 0 };
    std::atomic<double> dragStartValue { 0.0 };
    static constexpr uint32_t kUserInteractionPersistWindowMs = 5000;
    bool crtEnabled = false;
    bool useCustomPalette = false;

    CrtEffect crtEffect;
    float     crtTime = 0.0f;

    std::array<juce::Colour, 2> defaultPalette {
        juce::Colours::white,
        juce::Colours::black
    };
    std::array<juce::Colour, 2> customPalette {
        juce::Colours::white,
        juce::Colours::black
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FREQTRAudioProcessorEditor)
};
