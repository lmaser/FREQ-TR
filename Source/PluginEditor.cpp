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

static bool isGainFaderFloor (float dB) noexcept
{
    return dB <= FREQTRAudioProcessor::kGainFloorDb + 0.001f;
}

static juce::String formatGainFaderDb (float dB)
{
    if (isGainFaderFloor (dB))
        return "-INF dB";
    if (std::abs (dB) < 0.05f)
        return "0.0 dB";
    return juce::String (dB, 1) + " dB";
}

static juce::String formatGainFaderDbCompact (float dB)
{
    if (isGainFaderFloor (dB))
        return "-INFdB";
    if (std::abs (dB) < 0.05f)
        return "0.0dB";
    return juce::String (dB, 1) + "dB";
}

static juce::String formatFilterPromptFrequency (float hz)
{
    return juce::String (juce::roundToInt (juce::jlimit (20.0f, 20000.0f, hz)));
}

static juce::String formatInlineFrequency (float hz)
{
    if (hz < 0.05f)
        return "0Hz";

    const float displayHz = std::round (hz * 100.0f) / 100.0f;
    if (displayHz >= 1000.0f)
        return juce::String (displayHz / 1000.0f, 2) + "kHz";
    return juce::String (displayHz, 2) + "Hz";
}

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
    if (ch <= 0)
        return "OMNI";

    return "CHANNEL " + juce::String (ch);
}

// ── Retrig tooltip ──
static juce::String formatRetrigTooltip (bool on)
{
    return juce::String ("RETRIG ") + (on ? "ON" : "OFF");
}

static juce::String formatChaosTooltip (float amountPercent, float speedHz)
{
    return "AMT " + juce::String (juce::roundToInt (juce::jlimit (0.0f, 100.0f, amountPercent))) + "%"
         + " | SPD " + juce::String (juce::jlimit (FREQTRAudioProcessor::kChaosSpdMin,
                                                   FREQTRAudioProcessor::kChaosSpdMax,
                                                   speedHz), 1)
         + " Hz";
}

static juce::String formatPdcTooltip (bool on, int maxWindow)
{
    return juce::String ("PDC ") + (on ? "ON" : "OFF")
         + " | MAX WIN " + juce::String (FREQTRAudioProcessor::getCanonicalHilbertWindow (maxWindow));
}

static juce::String formatSidechainToneText (float hz)
{
    const float clamped = juce::jlimit (FREQTRAudioProcessor::kSidechainToneMin,
                                        FREQTRAudioProcessor::kSidechainToneMax, hz);
    if (clamped >= 10000.0f)
        return juce::String (juce::roundToInt (clamped / 1000.0f)) + "kHz";
    if (clamped >= 1000.0f)
        return juce::String (clamped / 1000.0f, 1) + "kHz";
    return juce::String (juce::roundToInt (clamped)) + "Hz";
}

static juce::String formatSidechainTooltip (float time, float tone)
{
    return "TIME x" + juce::String (juce::jlimit (0.0f, 1.0f, time), 2)
         + " | TONE " + formatSidechainToneText (tone);
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

    if (ticked)
    {
        g.setColour (scheme.outline);
        g.fillRect (r);
    }
    else
    {
        g.setColour (scheme.outline);
        g.drawRect (r, 4.0f);

        const float innerInset = juce::jlimit (1.0f, side * 0.45f, side * UiMetrics::tickBoxInnerInsetRatio);
        auto inner = r.reduced (innerInset);
        g.setColour (scheme.bg);
        g.fillRect (inner);
    }
}

void FREQTRAudioProcessorEditor::MinimalLNF::drawToggleButton (
	juce::Graphics& g, juce::ToggleButton& button,
	bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
	const auto local = button.getLocalBounds().toFloat().reduced (1.0f);
	const float side = juce::jlimit (14.0f,
	                                 juce::jmax (14.0f, local.getHeight() - 2.0f),
	                                 std::round (local.getHeight() * 0.65f));

	drawTickBox (g, button, 0, 0, 0, 0,
	             button.getToggleState(), button.isEnabled(),
	             shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

	const float textX = local.getX() + 2.0f + side + 2.0f;
	auto textArea = button.getLocalBounds().toFloat();
	textArea.removeFromLeft (textX);

	g.setColour (button.findColour (juce::ToggleButton::textColourId));

	float fontSize = juce::jlimit (12.0f, 40.0f, (float) button.getHeight() - 6.0f);

	const auto text = button.getButtonText();
	const float availW = textArea.getWidth();
	if (availW > 0)
	{
		juce::Font testFont (juce::FontOptions (fontSize).withStyle ("Bold"));
		juce::GlyphArrangement ga;
		ga.addLineOfText (testFont, text, 0.0f, 0.0f);
		const float neededW = ga.getBoundingBox (0, -1, false).getWidth();
		if (neededW > availW)
			fontSize = juce::jmax (8.0f, fontSize * (availW / neededW));
	}

	g.setFont (juce::Font (juce::FontOptions (fontSize).withStyle ("Bold")));
	g.drawText (text, textArea, juce::Justification::centredLeft, false);
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

void FREQTRAudioProcessorEditor::MinimalLNF::drawComboBox (
    juce::Graphics& g, int width, int height,
    bool /*isButtonDown*/, int /*buttonX*/, int /*buttonY*/,
    int /*buttonW*/, int /*buttonH*/, juce::ComboBox& /*box*/)
{
    const juce::Rectangle<int> r (0, 0, width, height);
    g.setColour (scheme.bg);
    g.fillRect (r);
    g.setColour (scheme.outline);
    g.drawRect (r, 3);
}

void FREQTRAudioProcessorEditor::MinimalLNF::drawPopupMenuBackground (
    juce::Graphics& g, int width, int height)
{
    g.fillAll (scheme.bg);
    g.setColour (scheme.outline);
    g.drawRect (0, 0, width, height, 2);
}

juce::Font FREQTRAudioProcessorEditor::MinimalLNF::getComboBoxFont (juce::ComboBox& box)
{
    const float h = juce::jlimit (12.0f, 24.0f, box.getHeight() * 0.59f);
    return juce::Font (juce::FontOptions (h).withStyle ("Bold"));
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

//========================== FilterBarComponent ==========================

juce::Rectangle<float> FREQTRAudioProcessorEditor::FilterBarComponent::getInnerArea() const
{
    return getLocalBounds().toFloat().reduced (kPad);
}

float FREQTRAudioProcessorEditor::FilterBarComponent::freqToNormX (float freq) const
{
    const float clamped = juce::jlimit (kMinFreq, kMaxFreq, freq);
    return std::log2 (clamped / kMinFreq) / std::log2 (kMaxFreq / kMinFreq);
}

float FREQTRAudioProcessorEditor::FilterBarComponent::normXToFreq (float normX) const
{
    const float n = juce::jlimit (0.0f, 1.0f, normX);
    return kMinFreq * std::pow (2.0f, n * std::log2 (kMaxFreq / kMinFreq));
}

float FREQTRAudioProcessorEditor::FilterBarComponent::getMarkerScreenX (float freq) const
{
    const auto inner = getInnerArea();
    return inner.getX() + freqToNormX (freq) * inner.getWidth();
}

FREQTRAudioProcessorEditor::FilterBarComponent::DragTarget
FREQTRAudioProcessorEditor::FilterBarComponent::hitTestMarker (juce::Point<float> p) const
{
    const float hpX = getMarkerScreenX (hpFreq_);
    const float lpX = getMarkerScreenX (lpFreq_);
    const float distHp = std::abs (p.x - hpX);
    const float distLp = std::abs (p.x - lpX);

    if (distHp <= kMarkerHitPx && distHp <= distLp)
        return HP;
    if (distLp <= kMarkerHitPx)
        return LP;
    if (distHp <= kMarkerHitPx)
        return HP;

    return None;
}

void FREQTRAudioProcessorEditor::FilterBarComponent::setFreqFromMouseX (float mouseX, DragTarget target)
{
    if (owner == nullptr || target == None)
        return;

    const auto inner = getInnerArea();
    const float normX = (inner.getWidth() > 0.0f) ? (mouseX - inner.getX()) / inner.getWidth() : 0.0f;
    float freq = normXToFreq (normX);

    // Clamp so HP never exceeds LP and vice-versa
    auto& proc = owner->audioProcessor;
    if (target == HP)
    {
        const float otherFreq = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterLpFreq)->load();
        freq = juce::jmin (freq, otherFreq);
    }
    else
    {
        const float otherFreq = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterHpFreq)->load();
        freq = juce::jmax (freq, otherFreq);
    }

    const char* paramId = (target == HP) ? FREQTRAudioProcessor::kParamFilterHpFreq
                                         : FREQTRAudioProcessor::kParamFilterLpFreq;
    if (auto* param = proc.apvts.getParameter (paramId))
        param->setValueNotifyingHost (param->convertTo0to1 (freq));
}

void FREQTRAudioProcessorEditor::FilterBarComponent::updateTooltipForTarget (DragTarget target)
{
    if (target == HP)
    {
        const int hz = juce::roundToInt (hpFreq_);
        setTooltip ("HP " + juce::String (hz) + " Hz");
    }
    else if (target == LP)
    {
        const int hz = juce::roundToInt (lpFreq_);
        setTooltip ("LP " + juce::String (hz) + " Hz");
    }
    else
    {
        setTooltip ({});
    }
}

void FREQTRAudioProcessorEditor::FilterBarComponent::updateFromProcessor()
{
    if (owner == nullptr) return;
    auto& proc = owner->audioProcessor;
    const float newHpFreq = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterHpFreq)->load();
    const float newLpFreq = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterLpFreq)->load();
    const bool  newHpOn   = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterHpOn)->load() > 0.5f;
    const bool  newLpOn   = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterLpOn)->load() > 0.5f;

    if (newHpFreq == hpFreq_ && newLpFreq == lpFreq_ && newHpOn == hpOn_ && newLpOn == lpOn_)
        return;

    hpFreq_ = newHpFreq;
    lpFreq_ = newLpFreq;
    hpOn_   = newHpOn;
    lpOn_   = newLpOn;
    repaint();
}

void FREQTRAudioProcessorEditor::FilterBarComponent::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    // Outline
    g.setColour (scheme.outline);
    g.drawRect (r, 4.0f);

    // Background
    const auto inner = getInnerArea();
    g.setColour (scheme.bg);
    g.fillRect (inner);

    // Pass-band fill (between HP and LP) — subtle
    if (hpOn_ || lpOn_)
    {
        const float hpX = hpOn_ ? getMarkerScreenX (hpFreq_) : inner.getX();
        const float lpX = lpOn_ ? getMarkerScreenX (lpFreq_) : inner.getRight();

        if (lpX > hpX)
        {
            const auto band = juce::Rectangle<float> (hpX, inner.getY(), lpX - hpX, inner.getHeight());
            g.setColour (scheme.fg.withAlpha (0.12f));
            g.fillRect (band.getIntersection (inner));
        }
    }

    // HP marker
    {
        const float mx = getMarkerScreenX (hpFreq_);
        if (mx >= inner.getX() && mx <= inner.getRight())
        {
            const float alpha = hpOn_ ? 1.0f : 0.25f;
            g.setColour (scheme.fg.withAlpha (alpha));
            const float hw = 2.5f;   // half-width (5 px total)
            const float overshoot = 3.0f;
            g.fillRoundedRectangle (mx - hw, inner.getY() - overshoot, hw * 2.0f,
                                    inner.getHeight() + overshoot * 2.0f, 2.0f);
        }
    }

    // LP marker
    {
        const float mx = getMarkerScreenX (lpFreq_);
        if (mx >= inner.getX() && mx <= inner.getRight())
        {
            const float alpha = lpOn_ ? 1.0f : 0.25f;
            g.setColour (scheme.fg.withAlpha (alpha));
            const float hw = 2.5f;
            const float overshoot = 3.0f;
            g.fillRoundedRectangle (mx - hw, inner.getY() - overshoot, hw * 2.0f,
                                    inner.getHeight() + overshoot * 2.0f, 2.0f);
        }
    }
}

void FREQTRAudioProcessorEditor::FilterBarComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (owner != nullptr)
            owner->openFilterPrompt();
        return;
    }

    currentDrag_ = hitTestMarker (e.position);
    if (currentDrag_ != None)
    {
        setFreqFromMouseX (e.position.x, currentDrag_);
        updateFromProcessor();
        updateTooltipForTarget (currentDrag_);
    }
}

void FREQTRAudioProcessorEditor::FilterBarComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (currentDrag_ != None)
    {
        setFreqFromMouseX (e.position.x, currentDrag_);
        updateFromProcessor();
        updateTooltipForTarget (currentDrag_);
    }
}

void FREQTRAudioProcessorEditor::FilterBarComponent::mouseUp (const juce::MouseEvent&)
{
    currentDrag_ = None;
}

void FREQTRAudioProcessorEditor::FilterBarComponent::mouseMove (const juce::MouseEvent& e)
{
    updateTooltipForTarget (hitTestMarker (e.position));
}

void FREQTRAudioProcessorEditor::FilterBarComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (owner == nullptr) return;
    auto& proc = owner->audioProcessor;

    const auto target = hitTestMarker (e.position);
    if (target == HP)
    {
        if (auto* param = proc.apvts.getParameter (FREQTRAudioProcessor::kParamFilterHpOn))
        {
            const bool current = param->getValue() > 0.5f;
            param->setValueNotifyingHost (current ? 0.0f : 1.0f);
        }
    }
    else if (target == LP)
    {
        if (auto* param = proc.apvts.getParameter (FREQTRAudioProcessor::kParamFilterLpOn))
        {
            const bool current = param->getValue() > 0.5f;
            param->setValueNotifyingHost (current ? 0.0f : 1.0f);
        }
    }
    else
    {
        // Double-click on empty area — open prompt
        owner->openFilterPrompt();
    }
}

// ========================== DualMixBarComponent ==========================

juce::Rectangle<float> FREQTRAudioProcessorEditor::DualMixBarComponent::getInnerArea() const
{
    return getLocalBounds().toFloat().reduced (kPad);
}

FREQTRAudioProcessorEditor::DualMixBarComponent::DragTarget
FREQTRAudioProcessorEditor::DualMixBarComponent::hitTestMarker (juce::Point<float> p) const
{
    const auto inner = getInnerArea();
    const float halfW = inner.getWidth() * 0.5f;
    const float midX  = inner.getX() + halfW;
    return (p.x < midX) ? DRY : WET;
}

void FREQTRAudioProcessorEditor::DualMixBarComponent::setLevelFromMouseX (float mouseX, DragTarget target)
{
    if (owner == nullptr || target == None) return;
    const auto inner = getInnerArea();
    const float halfW = inner.getWidth() * 0.5f;
    float level;
    if (target == DRY)
        level = (halfW > 0.0f) ? juce::jlimit (0.0f, 1.0f, (mouseX - inner.getX()) / halfW) : 0.0f;
    else
        level = (halfW > 0.0f) ? juce::jlimit (0.0f, 1.0f, (mouseX - (inner.getX() + halfW)) / halfW) : 0.0f;
    const char* paramId = (target == DRY) ? FREQTRAudioProcessor::kParamDryLevel
                                          : FREQTRAudioProcessor::kParamWetLevel;
    if (auto* param = owner->audioProcessor.apvts.getParameter (paramId))
        param->setValueNotifyingHost (level);
}

void FREQTRAudioProcessorEditor::DualMixBarComponent::updateTooltipForTarget (DragTarget target)
{
    if (target == None)
    {
        setTooltip ({});
        return;
    }

    const float level = (target == DRY) ? dryLevel_ : wetLevel_;
    const float dB = (level <= 0.0001f) ? -100.0f : 20.0f * std::log10 (level);
    const juce::String label = (target == DRY) ? "DRY" : "WET";
    setTooltip (dB <= -100.0f ? (label + " -INF dB") : (label + " " + juce::String (dB, 1) + " dB"));
}

void FREQTRAudioProcessorEditor::DualMixBarComponent::updateFromProcessor()
{
    if (owner == nullptr) return;
    auto& proc = owner->audioProcessor;
    const float newDry = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamDryLevel)->load();
    const float newWet = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamWetLevel)->load();
    if (newDry == dryLevel_ && newWet == wetLevel_) return;
    dryLevel_ = newDry;
    wetLevel_ = newWet;
    repaint();
}

void FREQTRAudioProcessorEditor::DualMixBarComponent::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (scheme.outline);
    g.drawRect (r, 4.0f);
    const auto inner = getInnerArea();
    g.setColour (scheme.bg);
    g.fillRect (inner);
    const float halfW = inner.getWidth() * 0.5f;
    const float divX  = inner.getX() + halfW;
    g.setColour (scheme.fg.withAlpha (0.25f));
    g.drawVerticalLine ((int) divX, inner.getY(), inner.getBottom());
    {
        const float fillW = dryLevel_ * halfW;
        g.setColour (scheme.fg.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (inner.getX(), inner.getY(), fillW, inner.getHeight()).getIntersection (inner));
    }
    {
        const float fillW = wetLevel_ * halfW;
        g.setColour (scheme.fg.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (divX, inner.getY(), fillW, inner.getHeight()).getIntersection (inner));
    }
    {
        const float mx = inner.getX() + dryLevel_ * halfW;
        if (mx >= inner.getX() && mx <= divX)
        {
            const float hw = 2.5f; const float overshoot = 3.0f;
            g.setColour (scheme.fg.withAlpha (0.7f));
            g.fillRoundedRectangle (mx - hw, inner.getY() - overshoot, hw * 2.0f, inner.getHeight() + overshoot * 2.0f, 2.0f);
        }
    }
    {
        const float mx = divX + wetLevel_ * halfW;
        if (mx >= divX && mx <= inner.getRight())
        {
            const float hw = 2.5f; const float overshoot = 3.0f;
            g.setColour (scheme.fg);
            g.fillRoundedRectangle (mx - hw, inner.getY() - overshoot, hw * 2.0f, inner.getHeight() + overshoot * 2.0f, 2.0f);
        }
    }
}

void FREQTRAudioProcessorEditor::DualMixBarComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (owner) owner->openMixSendPrompt(); return; }
    currentDrag_ = hitTestMarker (e.position);
    if (currentDrag_ != None)
    {
        lastTouched_ = currentDrag_;
        setLevelFromMouseX (e.position.x, currentDrag_);
        updateFromProcessor();
        updateTooltipForTarget (currentDrag_);
        if (owner) { if (owner->refreshLegendTextCache()) owner->updateCachedLayout(); owner->repaint(); }
    }
}

void FREQTRAudioProcessorEditor::DualMixBarComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (currentDrag_ != None)
    {
        setLevelFromMouseX (e.position.x, currentDrag_);
        updateFromProcessor();
        updateTooltipForTarget (currentDrag_);
        if (owner) { if (owner->refreshLegendTextCache()) owner->updateCachedLayout(); owner->repaint(); }
    }
}

void FREQTRAudioProcessorEditor::DualMixBarComponent::mouseUp (const juce::MouseEvent&)
{
    currentDrag_ = None;
}

void FREQTRAudioProcessorEditor::DualMixBarComponent::mouseMove (const juce::MouseEvent& e)
{
    updateTooltipForTarget (hitTestMarker (e.position));
}

//========================== Editor ==========================

FREQTRAudioProcessorEditor::FREQTRAudioProcessorEditor (FREQTRAudioProcessor& p)
: AudioProcessorEditor (&p), audioProcessor (p)
{
    const std::array<BarSlider*, 16> barSliders { &inputSlider, &outputSlider, &mixSlider, &freqSlider, &modSlider, &combSlider, &feedbackSlider, &engineSlider, &windowSlider, &harmSlider, &polaritySlider, &jitterSlider, &styleSlider, &tiltSlider, &panSlider, &limThresholdSlider };

    useCustomPalette = audioProcessor.getUiUseCustomPalette();
    crtEnabled = audioProcessor.getUiCrtEnabled();
    ioSectionExpanded_ = audioProcessor.getUiIoExpanded();

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
    feedbackSlider.setNumDecimalPlacesToDisplay (1);
    jitterSlider.setNumDecimalPlacesToDisplay (1);
    combSlider.setNumDecimalPlacesToDisplay (2);
    engineSlider.setNumDecimalPlacesToDisplay (1);
    windowSlider.setNumDecimalPlacesToDisplay (0);
    windowSlider.setRange ((double) FREQTRAudioProcessor::kHilbertWindowMin,
                           (double) FREQTRAudioProcessor::kHilbertWindowMax,
                           1.0);
    styleSlider.setNumDecimalPlacesToDisplay (0);
    harmSlider.setNumDecimalPlacesToDisplay (1);
    polaritySlider.setNumDecimalPlacesToDisplay (2);
    mixSlider.setNumDecimalPlacesToDisplay (1);
    inputSlider.setNumDecimalPlacesToDisplay (1);
    outputSlider.setNumDecimalPlacesToDisplay (1);
    inputSlider.setSkewFactor (FREQTRAudioProcessor::kGainSkew);
    outputSlider.setSkewFactor (FREQTRAudioProcessor::kGainSkew);

    // IO sliders start hidden (collapsible section, collapsed by default)
    inputSlider.setVisible (false);
    outputSlider.setVisible (false);
    mixSlider.setVisible (false);
    tiltSlider.setVisible (false);
    tiltSlider.setNumDecimalPlacesToDisplay (1);
    panSlider.setVisible (false);
    panSlider.setNumDecimalPlacesToDisplay (1);

    // Filter bar — hidden along with IO sliders in collapsed state
    filterBar_.setOwner (this);
    filterBar_.setScheme (activeScheme);
    addAndMakeVisible (filterBar_);
    filterBar_.setVisible (false);
    filterBar_.updateFromProcessor();

    // Chaos buttons (visible only when IO expanded)
    chaosFilterButton.setButtonText ("");
    addAndMakeVisible (chaosFilterButton);
    chaosFilterButton.setVisible (false);
    {
        const float savedAmt = audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosAmtFilter)->load();
        const float savedSpd = audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosSpdFilter)->load();
        chaosFilterDisplay.setText ("", juce::dontSendNotification);
        chaosFilterDisplay.setInterceptsMouseClicks (true, false);
        chaosFilterDisplay.addMouseListener (this, false);
        chaosFilterDisplay.setTooltip (formatChaosTooltip (savedAmt, savedSpd));
        chaosFilterDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        chaosFilterDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        chaosFilterDisplay.setOpaque (false);
        addAndMakeVisible (chaosFilterDisplay);
        chaosFilterDisplay.setVisible (false);
    }
    chaosDelayButton.setButtonText ("");
    addAndMakeVisible (chaosDelayButton);
    chaosDelayButton.setVisible (false);
    {
        const float savedAmt = audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosAmt)->load();
        const float savedSpd = audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamChaosSpd)->load();
        chaosDelayDisplay.setText ("", juce::dontSendNotification);
        chaosDelayDisplay.setInterceptsMouseClicks (true, false);
        chaosDelayDisplay.addMouseListener (this, false);
        chaosDelayDisplay.setTooltip (formatChaosTooltip (savedAmt, savedSpd));
        chaosDelayDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        chaosDelayDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        chaosDelayDisplay.setOpaque (false);
        addAndMakeVisible (chaosDelayDisplay);
        chaosDelayDisplay.setVisible (false);
    }

    // Mode In / Mode Out / Sum Bus combos
    {
        auto setupModeCombo = [this] (juce::ComboBox& combo)
        {
            addAndMakeVisible (combo);
            combo.addItem ("L+R",  1);
            combo.addItem ("MID",  2);
            combo.addItem ("SIDE", 3);
            combo.setJustificationType (juce::Justification::centred);
            combo.setLookAndFeel (&lnf);
            combo.setVisible (false);
        };
        setupModeCombo (modeInCombo);
        setupModeCombo (modeOutCombo);

        addAndMakeVisible (sumBusCombo);
        sumBusCombo.addItem ("ST",              1);
        sumBusCombo.addItem (juce::String::fromUTF8 (u8"\u2192M"), 2);
        sumBusCombo.addItem (juce::String::fromUTF8 (u8"\u2192S"), 3);
        sumBusCombo.setJustificationType (juce::Justification::centred);
        sumBusCombo.setLookAndFeel (&lnf);
        sumBusCombo.setVisible (false);

        addAndMakeVisible (limModeCombo);
        limModeCombo.addItem ("NONE",   1);
        limModeCombo.addItem ("WET",    2);
        limModeCombo.addItem ("GLOBAL", 3);
        limModeCombo.setJustificationType (juce::Justification::centred);
        limModeCombo.setLookAndFeel (&lnf);
        limModeCombo.setVisible (false);

        addAndMakeVisible (invPolCombo);
        invPolCombo.addItem ("NONE",   1);
        invPolCombo.addItem ("WET",    2);
        invPolCombo.addItem ("GLOBAL", 3);
        invPolCombo.setJustificationType (juce::Justification::centred);
        invPolCombo.setLookAndFeel (&lnf);
        invPolCombo.setVisible (false);

        addAndMakeVisible (invStrCombo);
        invStrCombo.addItem ("NONE",   1);
        invStrCombo.addItem ("WET",    2);
        invStrCombo.addItem ("GLOBAL", 3);
        invStrCombo.setJustificationType (juce::Justification::centred);
        invStrCombo.setLookAndFeel (&lnf);
        invStrCombo.setVisible (false);

        addAndMakeVisible (mixModeCombo);
        mixModeCombo.addItem ("INSERT", 1);
        mixModeCombo.addItem ("SEND",   2);
        mixModeCombo.setJustificationType (juce::Justification::centred);
        mixModeCombo.setLookAndFeel (&lnf);
        mixModeCombo.setVisible (false);

        addAndMakeVisible (filterPosCombo);
        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25bc T\u25bc"), 1);
        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25b2 T\u25b2"), 2);
        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25b2 T\u25bc"), 3);
        filterPosCombo.addItem (juce::String::fromUTF8 (u8"F\u25bc T\u25b2"), 4);
        filterPosCombo.setJustificationType (juce::Justification::centred);
        filterPosCombo.setLookAndFeel (&lnf);
        filterPosCombo.setVisible (false);

        addAndMakeVisible (dualMixBar_);
        dualMixBar_.setOwner (this);
        dualMixBar_.setVisible (false);
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

    // Retrig tooltip overlay (over SYNC label)
    const bool savedRetrig = audioProcessor.apvts.getParameterAsValue (
        FREQTRAudioProcessor::kParamRetrig).getValue();
    retrigDisplay.setText ("", juce::dontSendNotification);
    retrigDisplay.setInterceptsMouseClicks (true, false);
    retrigDisplay.addMouseListener (this, false);
    retrigDisplay.setTooltip (formatRetrigTooltip (savedRetrig));
    retrigDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    retrigDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    retrigDisplay.setOpaque (false);
    addAndMakeVisible (retrigDisplay);

    pdcDisplay.setText ("", juce::dontSendNotification);
    pdcDisplay.setInterceptsMouseClicks (true, false);
    pdcDisplay.addMouseListener (this, false);
    pdcDisplay.setTooltip (formatPdcTooltip (
        pdcButton.getToggleState(),
        (int) std::lround (audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamMaxWindow)->load())));
    pdcDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    pdcDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    pdcDisplay.setOpaque (false);
    addAndMakeVisible (pdcDisplay);

    sidechainDisplay.setText ("", juce::dontSendNotification);
    sidechainDisplay.setInterceptsMouseClicks (true, false);
    sidechainDisplay.addMouseListener (this, false);
    sidechainDisplay.setTooltip (formatSidechainTooltip (
        audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainTime)->load(),
        audioProcessor.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamSidechainTone)->load()));
    sidechainDisplay.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    sidechainDisplay.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    sidechainDisplay.setOpaque (false);
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
    bindSlider (polarityAttachment, FREQTRAudioProcessor::kParamPolarity, polaritySlider, kDefaultPolarity);
    bindSlider (mixAttachment,      FREQTRAudioProcessor::kParamMix,     mixSlider,     (double) FREQTRAudioProcessor::kMixDefault);
    bindSlider (inputAttachment,    FREQTRAudioProcessor::kParamInput,   inputSlider,   (double) FREQTRAudioProcessor::kInputDefault);
    bindSlider (outputAttachment,   FREQTRAudioProcessor::kParamOutput,  outputSlider,  (double) FREQTRAudioProcessor::kOutputDefault);
    bindSlider (tiltAttachment,     FREQTRAudioProcessor::kParamTilt,    tiltSlider,    (double) FREQTRAudioProcessor::kTiltDefault);
    bindSlider (panAttachment,      FREQTRAudioProcessor::kParamPan,     panSlider,     (double) FREQTRAudioProcessor::kPanDefault);
    bindSlider (limThresholdAttachment, FREQTRAudioProcessor::kParamLimThreshold, limThresholdSlider, kDefaultLimThreshold);

    // Disable numeric popup for discrete/slider-only controls.
    windowSlider.setAllowNumericPopup (false);
    styleSlider.setAllowNumericPopup (false);

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
    audioProcessor.apvts.removeParameterListener (FREQTRAudioProcessor::kParamSidechain, this);
    audioProcessor.apvts.removeParameterListener (FREQTRAudioProcessor::kParamPdc, this);
    audioProcessor.apvts.removeParameterListener (FREQTRAudioProcessor::kParamWindow, this);
    audioProcessor.apvts.removeParameterListener (FREQTRAudioProcessor::kParamMaxWindow, this);

    audioProcessor.setUiUseCustomPalette (useCustomPalette);
    audioProcessor.setUiCrtEnabled (crtEnabled);

    dismissEditorOwnedModalPrompts (lnf);
    setPromptOverlayActive (false);

    const std::array<BarSlider*, 16> barSliders { &inputSlider, &outputSlider, &mixSlider, &freqSlider, &modSlider, &combSlider, &feedbackSlider, &engineSlider, &windowSlider, &harmSlider, &polaritySlider, &jitterSlider, &styleSlider, &tiltSlider, &panSlider, &limThresholdSlider };
    for (auto* slider : barSliders)
        slider->removeListener (this);

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

    FREQScheme scheme;
    scheme.bg = palette[1];
    scheme.fg = palette[0];
    scheme.outline = palette[0];
    scheme.text = palette[0];

    activeScheme = scheme;
    lnf.setScheme (activeScheme);
    filterBar_.setScheme (activeScheme);

    for (auto* combo : { &modeInCombo, &modeOutCombo, &sumBusCombo, &limModeCombo, &invPolCombo, &invStrCombo, &mixModeCombo, &filterPosCombo })
    {
        combo->setColour (juce::ComboBox::backgroundColourId, scheme.bg);
        combo->setColour (juce::ComboBox::textColourId,       scheme.text);
        combo->setColour (juce::ComboBox::outlineColourId,    scheme.outline);
        combo->setColour (juce::ComboBox::arrowColourId,      scheme.text);
    }

    dualMixBar_.setScheme (scheme);
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
        return s == &freqSlider || s == &modSlider || s == &feedbackSlider || s == &jitterSlider || s == &combSlider
            || s == &engineSlider || s == &windowSlider || s == &styleSlider || s == &harmSlider || s == &polaritySlider
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

    promptOverlayActive = shouldBeActive;

    promptOverlay.setBounds (getLocalBounds());
    promptOverlay.setVisible (shouldBeActive);
    if (shouldBeActive)
        promptOverlay.toFront (false);

    // promptOverlay intercepts mouse input while the modal prompt is open. Do not disable
    // the underlying controls here, otherwise overlay dimming stacks with disabled alpha.

    if (! shouldBeActive)
    {
        updateCombEnabled();
        updateWindowEnabled();
        updateSidechainDependentControls();
    }

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

    if (parameterID == FREQTRAudioProcessor::kParamSidechain)
    {
        juce::ignoreUnused (newValue);
        juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]()
        {
            if (safeThis == nullptr) return;
            safeThis->updateSidechainDependentControls();
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
            mixSlider.setVisible (! isSendMode);
            dualMixBar_.setVisible (isSendMode);
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
            mixSlider.setVisible (false);
            dualMixBar_.setVisible (true);
        }
        else if (! isSend && dualMixBar_.isVisible())
        {
            dualMixBar_.setVisible (false);
            mixSlider.setVisible (true);
        }
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

        const bool targetIoExpanded = audioProcessor.getUiIoExpanded();
        const bool ioChanged = (ioSectionExpanded_ != targetIoExpanded);
        if (ioChanged)
        {
            ioSectionExpanded_ = targetIoExpanded;
            resized();
        }

        if (paletteChanged || paletteSwitchChanged)
            applyActivePalette();

        if (paletteChanged || paletteSwitchChanged || fxChanged || ioChanged)
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
    const float mult = (float) modSliderToMultiplier (modSlider.getValue());
    if (std::abs (mult - 1.0f) < kMultEpsilon)
        return "X1 MOD";
    return "X" + juce::String (mult, 2) + " MOD";
}

juce::String FREQTRAudioProcessorEditor::getModTextShort() const
{
    const float mult = (float) modSliderToMultiplier (modSlider.getValue());
    if (std::abs (mult - 1.0f) < kMultEpsilon)
        return "X1 MOD";
    return "X" + juce::String (mult, 2)  + " MOD";
}

juce::String FREQTRAudioProcessorEditor::getFeedbackText() const
{
    const int pct = (int) std::lround (juce::jlimit (-1.0, 1.0, feedbackSlider.getValue()) * 100.0);
    return juce::String (pct) + "% FBK";
}

juce::String FREQTRAudioProcessorEditor::getFeedbackTextShort() const
{
    const int pct = (int) std::lround (juce::jlimit (-1.0, 1.0, feedbackSlider.getValue()) * 100.0);
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
    pdcButton.setTooltip (tooltip);
    pdcDisplay.setTooltip (tooltip);
}

void FREQTRAudioProcessorEditor::updateSidechainDependentControls()
{
    if (promptOverlayActive)
        return;

    const bool sidechainOn = sidechainButton.getToggleState();
    const float carrierAlpha = sidechainOn ? 0.35f : 1.0f;

    freqSlider.setAlpha (carrierAlpha);
    modSlider.setAlpha (carrierAlpha);
    harmSlider.setAlpha (carrierAlpha);
    syncButton.setAlpha (carrierAlpha);
    midiButton.setAlpha (carrierAlpha);
    retrigDisplay.setAlpha (carrierAlpha);
    midiChannelDisplay.setAlpha (carrierAlpha);

    freqSlider.setEnabled (! sidechainOn);
    modSlider.setEnabled (! sidechainOn);
    harmSlider.setEnabled (! sidechainOn);
    syncButton.setEnabled (! sidechainOn);
    midiButton.setEnabled (! sidechainOn);

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
    cachedModIntOnly      = juce::String ((int) modSlider.getValue());
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
        setLabelOnly (cachedFreqTextFull, cachedFreqTextShort, cachedFreqIntOnly, "FREQ");
        setLabelOnly (cachedModTextFull, cachedModTextShort, cachedModIntOnly, "MOD");
    }

    if (sidechainOn)
    {
        setLabelOnly (cachedHarmTextFull, cachedHarmTextShort, cachedHarmIntOnly, "HARM");
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
    if (&s == &windowSlider || &s == &styleSlider)
        return;

    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    juce::String prefix;
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
            suffix = " Hz";
            suffixShort = " Hz";
        }
    }
    else if (&s == &modSlider)       { prefix = "X";           suffix = " MOD";      suffixShort = " MOD"; }
    else if (&s == &feedbackSlider)  { suffix = " % FBK";      suffixShort = " % FBK"; }
    else if (&s == &jitterSlider)    { suffix = " % JITTER";      suffixShort = " % JIT"; }
    else if (&s == &combSlider)      { suffix = " Hz";         suffixShort = " Hz"; }
    else if (&s == &engineSlider)    { suffix = " % ENGINE";   suffixShort = " % ENG"; }
    else if (&s == &harmSlider)      { suffix = " % HARM";     suffixShort = " % HARM"; }
    else if (&s == &polaritySlider)  { suffix = " POLARITY";   suffixShort = " POL"; }
    else if (&s == &mixSlider)       { suffix = " % MIX";      suffixShort = " % MIX"; }
    else if (&s == &panSlider)       { suffix = " % PAN";      suffixShort = " % PAN"; }
    else if (&s == &inputSlider)     { suffix = " dB INPUT";   suffixShort = " dB IN"; }
    else if (&s == &outputSlider)    { suffix = " dB OUTPUT";  suffixShort = " dB OUT"; }
    else if (&s == &tiltSlider)      { suffix = " dB TILT";    suffixShort = " dB TILT"; }
    else if (&s == &limThresholdSlider) { suffix = " dB LIM";  suffixShort = " dB LIM"; }
    const juce::String prefixText = prefix.trim();
    const juce::String suffixText = suffix.trimStart();
    const juce::String suffixTextShort = suffixShort.trimStart();
    const bool isPercentPrompt = (&s == &engineSlider || &s == &harmSlider || &s == &mixSlider || &s == &feedbackSlider || &s == &jitterSlider || &s == &panSlider);

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);

    juce::String currentDisplay;
    if (&s == &modSlider)
        currentDisplay = juce::String (modSliderToMultiplier (s.getValue()), 2);
    else if (&s == &engineSlider)
        currentDisplay = juce::String (s.getValue() * 100.0, 2);
    else if (&s == &feedbackSlider)
        currentDisplay = juce::String (s.getValue() * 100.0, 2);
    else if (&s == &jitterSlider)
        currentDisplay = juce::String (juce::jlimit (0.0, 100.0, s.getValue() * 100.0), 2);
    else if (&s == &mixSlider)
        currentDisplay = juce::String (s.getValue() * 100.0, 2);
    else if (&s == &panSlider)
        currentDisplay = juce::String (juce::jlimit (0.0, 100.0, s.getValue() * 100.0), 0);
    else if (&s == &freqSlider && ! isFreqSyncMode)
        currentDisplay = juce::String (s.getValue(), 3);
    else if (&s == &combSlider)
        currentDisplay = juce::String (s.getValue(), 3);
    else
        currentDisplay = s.getTextFromValue (s.getValue());

    aw->addTextEditor ("val", currentDisplay, juce::String());

    juce::Label* prefixLabel = nullptr;
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

        prefixLabel = new juce::Label ("prefix", prefixText);
        prefixLabel->setJustificationType (juce::Justification::centredRight);
        applyLabelTextColour (*prefixLabel, scheme.text);
        prefixLabel->setBorderSize (juce::BorderSize<int> (0));
        prefixLabel->setFont (f);
        aw->addAndMakeVisible (prefixLabel);

        suffixLabel = new juce::Label ("suffix", suffixText);
        suffixLabel->setComponentID (kPromptSuffixLabelId);
        suffixLabel->setJustificationType (juce::Justification::centredLeft);
        applyLabelTextColour (*suffixLabel, scheme.text);
        suffixLabel->setBorderSize (juce::BorderSize<int> (0));
        suffixLabel->setFont (f);
        aw->addAndMakeVisible (suffixLabel);

        juce::String worstCaseText;
        if (&s == &freqSlider)
            worstCaseText = isFreqSyncMode ? "1/64T." : "5000.000";
        else if (&s == &modSlider)
            worstCaseText = "4.00";
        else if (&s == &feedbackSlider)
            worstCaseText = "-100.00";
        else if (&s == &jitterSlider)
            worstCaseText = "100.00";
        else if (&s == &engineSlider)
            worstCaseText = "100.00";
        else if (&s == &harmSlider)
            worstCaseText = "100.00";
        else if (&s == &polaritySlider)
            worstCaseText = "-1.00";
        else if (&s == &mixSlider)
            worstCaseText = "100.00";
        else if (&s == &panSlider)
            worstCaseText = "100";
        else if (&s == &inputSlider)
            worstCaseText = "-144.0";
        else if (&s == &outputSlider)
            worstCaseText = "-144.0";
        else if (&s == &tiltSlider)
            worstCaseText = "-6.0";
        else if (&s == &limThresholdSlider)
            worstCaseText = "-36.0";
        else
            worstCaseText = "999.99";

        const int maxInputTextW = juce::jmax (1, stringWidth (f, worstCaseText));

        layoutValueAndSuffix = [aw, te, prefixLabel, suffixLabel, editorBaseBounds, isPercentPrompt, prefixText, suffixText, suffixTextShort, maxInputTextW]()
        {
            juce::ignoreUnused (isPercentPrompt);
            const int contentPad = kPromptInlineContentPadPx;
            const int contentLeft = contentPad;
            const int contentRight = (aw != nullptr ? aw->getWidth() - contentPad : editorBaseBounds.getRight());
            const int availableW = contentRight - contentLeft;
            const int contentCenter = (contentLeft + contentRight) / 2;

            const int prefixW = prefixText.isNotEmpty() ? stringWidth (prefixLabel->getFont(), prefixText) : 0;
            const int fullLabelW = stringWidth (suffixLabel->getFont(), suffixText) + 2;
            const bool stickPercentFull = suffixText.containsChar ('%');
            const int spaceWFull = stickPercentFull ? 0 : juce::jmax (2, stringWidth (suffixLabel->getFont(), " "));
            const int worstCaseFullW = prefixW + maxInputTextW + spaceWFull + fullLabelW;

            constexpr int kPromptShortLabelComfortPx = 8;
            const bool useShort = (worstCaseFullW > (availableW - kPromptShortLabelComfortPx))
                               && suffixTextShort != suffixText;
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

            const int combinedW = prefixW + textW + minGapPx + labelW;

            int blockLeft = contentCenter - (combinedW / 2);
            const int minBlockLeft = contentLeft;
            const int maxBlockLeft = juce::jmax (minBlockLeft, contentRight - combinedW);
            blockLeft = juce::jlimit (minBlockLeft, maxBlockLeft, blockLeft);

            int teX = blockLeft + prefixW - ((editorW - textW) / 2);
            const int minTeX = contentLeft;
            const int maxTeX = juce::jmax (minTeX, contentRight - editorW);
            teX = juce::jlimit (minTeX, maxTeX, teX);

            er.setX (teX);
            te->setBounds (er);

            const int textLeftActual = er.getX() + (er.getWidth() - textW) / 2;
            if (prefixLabel != nullptr)
                prefixLabel->setBounds (textLeftActual - prefixW, er.getY(), prefixW, juce::jmax (1, er.getHeight()));

            int labelX = textLeftActual + textW + minGapPx;
            const int minLabelX = contentLeft;
            const int maxLabelX = juce::jmax (minLabelX, contentRight - labelW);
            labelX = juce::jlimit (minLabelX, maxLabelX, labelX);

            const int labelY = er.getY();
            const int labelH = juce::jmax (1, er.getHeight());
            suffixLabel->setBounds (labelX, labelY, labelW, labelH);
        };

        te->setBounds (editorBaseBounds);
        if (prefixLabel != nullptr)
        {
            const int prefixW0 = prefixText.isNotEmpty() ? stringWidth (prefixLabel->getFont(), prefixText) : 0;
            prefixLabel->setBounds (r.getX() - prefixW0, r.getY() + 1, prefixW0, juce::jmax (1, r.getHeight() - 2));
        }
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
                maxVal = (double) FREQTRAudioProcessor::kFreqSyncMax;
                maxDecs = 0;
                maxLen = 6;
            }
            else
            {
                minVal = 0.0;
                maxVal = 5000.0;
                maxDecs = 3;
                maxLen = 8; // "5000.000"
            }
        }
        else if (&s == &modSlider)
        {
            minVal = 0.0;
            maxVal = 4.0;
            maxDecs = 2;
            maxLen = 4;
        }
        else if (&s == &combSlider)
        {
            minVal = FREQTRAudioProcessor::kCombMin;
            maxVal = FREQTRAudioProcessor::kCombMax;
            maxDecs = 3;
            maxLen = 8; // "5000.000"
        }
        else if (&s == &feedbackSlider)
        {
            minVal = -100.0;
            maxVal = 100.0;
            maxDecs = 2;
            maxLen = 7;
        }
        else if (&s == &jitterSlider)
        {
            minVal = 0.0;
            maxVal = 100.0;
            maxDecs = 2;
            maxLen = 6;
        }
        else if (&s == &engineSlider)
        {
            minVal = 0.0;
            maxVal = 100.0;
            maxDecs = 2;
            maxLen = 6;
        }
        else if (&s == &harmSlider)
        {
            minVal = 0.0;
            maxVal = 100.0;
            maxDecs = 2;
            maxLen = 6;
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
            maxDecs = 2;
            maxLen = 6;
        }
        else if (&s == &inputSlider)
        {
            minVal = FREQTRAudioProcessor::kGainFloorDb;
            maxVal = FREQTRAudioProcessor::kGainMaxDb;
            maxDecs = 1;
            maxLen = 6;
        }
        else if (&s == &outputSlider)
        {
            minVal = FREQTRAudioProcessor::kGainFloorDb;
            maxVal = FREQTRAudioProcessor::kGainMaxDb;
            maxDecs = 1;
            maxLen = 6;
        }
        else if (&s == &tiltSlider)
        {
            minVal = FREQTRAudioProcessor::kTiltMin;
            maxVal = FREQTRAudioProcessor::kTiltMax;
            maxDecs = 1;
            maxLen = 4;
        }
        else if (&s == &limThresholdSlider)
        {
            minVal = FREQTRAudioProcessor::kLimThresholdMin;
            maxVal = FREQTRAudioProcessor::kLimThresholdMax;
            maxDecs = 1;
            maxLen = 5;
        }
        else if (&s == &panSlider)
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
        {
            if (prefixLabel != nullptr)
                prefixLabel->setFont (te->getFont());
            suffixLabel->setFont (te->getFont());
        }
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
            {
                if (prefixLabel != nullptr)
                    prefixLabel->setFont (te->getFont());
                suffixLbl->setFont (te->getFont());
            }
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

                v = (double) juce::jlimit (FREQTRAudioProcessor::kFreqSyncMin,
                                           FREQTRAudioProcessor::kFreqSyncMax,
                                           foundIndex);
            }
            else
            {
                juce::String t = normalised.trimStart();
                while (t.startsWithChar ('+'))
                    t = t.substring (1).trimStart();
                const juce::String numericToken = t.initialSectionContainingOnly ("0123456789.,-");
                v = numericToken.getDoubleValue();

                // Percent input maps to normalized slider units; FBK intentionally supports -100..+100.
                if (safeThis != nullptr && (sliderPtr == &safeThis->engineSlider
                                         || sliderPtr == &safeThis->harmSlider
                                         || sliderPtr == &safeThis->mixSlider
                                         || sliderPtr == &safeThis->feedbackSlider
                                         || sliderPtr == &safeThis->jitterSlider
                                         || sliderPtr == &safeThis->panSlider))
                    v *= 0.01;

                // Multiplier input maps to slider position for MOD.
                if (safeThis != nullptr && sliderPtr == &safeThis->modSlider)
                    v = multiplierToModSlider (v);
            }

            const auto range = sliderPtr->getRange();
            double clamped = juce::jlimit (range.getStart(), range.getEnd(), v);

            if (safeThis != nullptr && sliderPtr == &safeThis->freqSlider
                && ! safeThis->syncButton.getToggleState())
            {
                clamped = roundToDecimals (clamped, 3);
            }

            sliderPtr->setValue (clamped, juce::sendNotificationSync);
        }));
}

// ── Filter Prompt (HP/LP frequency + slope) ───────────────────────
void FREQTRAudioProcessorEditor::openFilterPrompt()
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    auto& proc = audioProcessor;
    const float hpFreq = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterHpFreq)->load();
    const float lpFreq = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterLpFreq)->load();
    const int hpSlope  = juce::roundToInt (proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterHpSlope)->load());
    const int lpSlope  = juce::roundToInt (proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterLpSlope)->load());
    const bool hpOn    = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterHpOn)->load() > 0.5f;
    const bool lpOn    = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamFilterLpOn)->load() > 0.5f;

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);

    // ── Inline PromptBar for freq dragging ──
    struct PromptBar : public juce::Component
    {
        FREQScheme colours;
        float  value01    = 0.5f;
        float  default01  = 0.5f;
        std::function<void (float)> onValueChanged;

        PromptBar (const FREQScheme& s, float initial01, float def01)
            : colours (s), value01 (initial01), default01 (def01) {}

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (colours.outline);
            g.drawRect (r, 4.0f);
            const float pad = 7.0f;
            auto inner = r.reduced (pad);
            g.setColour (colours.bg);
            g.fillRect (inner);
            const float fillW = juce::jlimit (0.0f, inner.getWidth(), inner.getWidth() * value01);
            g.setColour (colours.fg);
            g.fillRect (inner.withWidth (fillW));
        }

        void mouseDown (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDrag (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDoubleClick (const juce::MouseEvent&) override { setValue (default01); }

        void setValue (float v)
        {
            value01 = juce::jlimit (0.0f, 1.0f, v);
            repaint();
            if (onValueChanged)
                onValueChanged (value01);
        }

    private:
        void updateFromMouse (const juce::MouseEvent& e)
        {
            const float pad = 7.0f;
            const float innerW = (float) getWidth() - pad * 2.0f;
            setValue (innerW > 0.0f ? ((float) e.x - pad) / innerW : 0.0f);
        }
    };

    // Freq normalisation helpers (log scale 20..20000)
    auto freqToNorm = [] (float freq) -> float
    {
        constexpr float minF = 20.0f, maxF = 20000.0f;
        return std::log2 (juce::jlimit (minF, maxF, freq) / minF) / std::log2 (maxF / minF);
    };
    auto normToFreq = [] (float n) -> float
    {
        constexpr float minF = 20.0f, maxF = 20000.0f;
        return minF * std::pow (2.0f, juce::jlimit (0.0f, 1.0f, n) * std::log2 (maxF / minF));
    };

    // HP section
    aw->addTextEditor ("hpFreq", formatFilterPromptFrequency (hpFreq), juce::String());
    auto* hpBar = new PromptBar (scheme, freqToNorm (hpFreq), freqToNorm (FREQTRAudioProcessor::kFilterHpFreqDefault));
    aw->addAndMakeVisible (hpBar);

    // LP section
    aw->addTextEditor ("lpFreq", formatFilterPromptFrequency (lpFreq), juce::String());
    auto* lpBar = new PromptBar (scheme, freqToNorm (lpFreq), freqToNorm (FREQTRAudioProcessor::kFilterLpFreqDefault));
    aw->addAndMakeVisible (lpBar);

    // HP on/off toggle
    auto* hpToggle = new juce::ToggleButton ("");
    hpToggle->setToggleState (hpOn, juce::dontSendNotification);
    hpToggle->setLookAndFeel (&lnf);
    aw->addAndMakeVisible (hpToggle);

    // LP on/off toggle
    auto* lpToggle = new juce::ToggleButton ("");
    lpToggle->setToggleState (lpOn, juce::dontSendNotification);
    lpToggle->setLookAndFeel (&lnf);
    aw->addAndMakeVisible (lpToggle);

    // Clickable slope labels (cycle 6→12→24→6 on click)
    auto slopeToText = [] (int s) -> juce::String
    {
        if (s == 0) return "6dB";
        if (s == 1) return "12dB";
        return "24dB";
    };

    auto* hpSlopeLabel = new juce::Label ("", slopeToText (hpSlope));
    hpSlopeLabel->setJustificationType (juce::Justification::centredRight);
    hpSlopeLabel->setColour (juce::Label::textColourId, scheme.text);
    aw->addAndMakeVisible (hpSlopeLabel);

    auto* lpSlopeLabel = new juce::Label ("", slopeToText (lpSlope));
    lpSlopeLabel->setJustificationType (juce::Justification::centredRight);
    lpSlopeLabel->setColour (juce::Label::textColourId, scheme.text);
    aw->addAndMakeVisible (lpSlopeLabel);

    // Shared state
    auto hpSlopeVal  = std::make_shared<int> (hpSlope);
    auto lpSlopeVal  = std::make_shared<int> (lpSlope);
    auto syncing     = std::make_shared<bool> (false);
    auto layoutFn    = std::make_shared<std::function<void()>> ([] {});

    // ── Real-time parameter setter ──
    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);

    auto pushParams = [safeThis, hpToggle, lpToggle, hpSlopeVal, lpSlopeVal, normToFreq, aw] ()
    {
        if (safeThis == nullptr) return;
        auto& p = safeThis->audioProcessor;
        auto setP = [&p] (const char* id, float plain)
        {
            if (auto* param = p.apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (plain));
        };

        auto* hpTe = aw->getTextEditor ("hpFreq");
        auto* lpTe = aw->getTextEditor ("lpFreq");
        float hpF = hpTe ? juce::jlimit (20.0f, 20000.0f, (float) hpTe->getText().getFloatValue()) : 20.0f;
        float lpF = lpTe ? juce::jlimit (20.0f, 20000.0f, (float) lpTe->getText().getFloatValue()) : 20000.0f;
        // Clamp so HP never exceeds LP
        if (hpF > lpF) { const float mid = (hpF + lpF) * 0.5f; hpF = mid; lpF = mid; }
        if (hpTe) setP (FREQTRAudioProcessor::kParamFilterHpFreq, hpF);
        if (lpTe) setP (FREQTRAudioProcessor::kParamFilterLpFreq, lpF);
        setP (FREQTRAudioProcessor::kParamFilterHpSlope, (float) *hpSlopeVal);
        setP (FREQTRAudioProcessor::kParamFilterLpSlope, (float) *lpSlopeVal);

        if (auto* hpOnParam = p.apvts.getParameter (FREQTRAudioProcessor::kParamFilterHpOn))
            hpOnParam->setValueNotifyingHost (hpToggle->getToggleState() ? 1.0f : 0.0f);
        if (auto* lpOnParam = p.apvts.getParameter (FREQTRAudioProcessor::kParamFilterLpOn))
            lpOnParam->setValueNotifyingHost (lpToggle->getToggleState() ? 1.0f : 0.0f);

        safeThis->filterBar_.updateFromProcessor();
    };

    // Slope label click → cycle value and push
    hpSlopeLabel->setInterceptsMouseClicks (true, false);
    struct SlopeCycler : public juce::MouseListener
    {
        std::shared_ptr<int> val;
        juce::Label* label;
        std::function<juce::String (int)> toText;
        std::function<void()> push;
        std::shared_ptr<std::function<void()>> layout;
        void mouseDown (const juce::MouseEvent&) override
        {
            *val = (*val + 1) % 3;
            label->setText (toText (*val), juce::dontSendNotification);
            push();
            if (layout && *layout) (*layout)();
        }
    };
    auto* hpCycler = new SlopeCycler();
    hpCycler->val = hpSlopeVal;
    hpCycler->label = hpSlopeLabel;
    hpCycler->toText = slopeToText;
    hpCycler->push = pushParams;
    hpCycler->layout = layoutFn;
    hpSlopeLabel->addMouseListener (hpCycler, false);

    lpSlopeLabel->setInterceptsMouseClicks (true, false);
    auto* lpCycler = new SlopeCycler();
    lpCycler->val = lpSlopeVal;
    lpCycler->label = lpSlopeLabel;
    lpCycler->toText = slopeToText;
    lpCycler->push = pushParams;
    lpCycler->layout = layoutFn;
    lpSlopeLabel->addMouseListener (lpCycler, false);

    // prevent cyclers from leaking — tie lifetime to shared_ptrs captured in modal callback
    auto hpCyclerGuard = std::shared_ptr<SlopeCycler> (hpCycler);
    auto lpCyclerGuard = std::shared_ptr<SlopeCycler> (lpCycler);

    // Wire toggle real-time
    hpToggle->onClick = pushParams;
    lpToggle->onClick = pushParams;

    // Wire bar ↔ text sync — real-time
    auto* hpTe = aw->getTextEditor ("hpFreq");
    auto* lpTe = aw->getTextEditor ("lpFreq");

    auto barToText = [aw, syncing, normToFreq, freqToNorm, pushParams, hpBar, lpBar] (const char* editorId, float v01, bool isHp)
    {
        if (*syncing) return;
        *syncing = true;
        if (isHp)
            v01 = juce::jmin (v01, lpBar->value01);
        else
            v01 = juce::jmax (v01, hpBar->value01);

        if (isHp) { hpBar->value01 = v01; hpBar->repaint(); }
        else      { lpBar->value01 = v01; lpBar->repaint(); }

        if (auto* te = aw->getTextEditor (editorId))
        {
            te->setText (formatFilterPromptFrequency (normToFreq (v01)), juce::sendNotification);
            te->selectAll();
        }
        *syncing = false;
        pushParams();
    };

    hpBar->onValueChanged = [barToText] (float v) { barToText ("hpFreq", v, true); };
    lpBar->onValueChanged = [barToText] (float v) { barToText ("lpFreq", v, false); };

    auto textToBar = [syncing, freqToNorm, normToFreq, pushParams, aw, hpBar, lpBar] (juce::TextEditor* te, PromptBar* bar, bool isHp)
    {
        if (*syncing || te == nullptr || bar == nullptr) return;
        *syncing = true;
        float freq = juce::jlimit (20.0f, 20000.0f, (float) te->getText().getFloatValue());
        auto* otherTe = aw->getTextEditor (isHp ? "lpFreq" : "hpFreq");
        const float otherFreq = otherTe ? juce::jlimit (20.0f, 20000.0f, (float) otherTe->getText().getFloatValue()) : (isHp ? 20000.0f : 20.0f);
        if (isHp)
            freq = juce::jmin (freq, otherFreq);
        else
            freq = juce::jmax (freq, otherFreq);
        te->setText (formatFilterPromptFrequency (freq), juce::dontSendNotification);
        bar->value01 = freqToNorm (freq);
        bar->repaint();
        *syncing = false;
        pushParams();
    };

    if (hpTe != nullptr)
        hpTe->onTextChange = [syncing, textToBar, hpTe, hpBar, layoutFn] () { textToBar (hpTe, hpBar, true); if (*layoutFn) (*layoutFn)(); };
    if (lpTe != nullptr)
        lpTe->onTextChange = [syncing, textToBar, lpTe, lpBar, layoutFn] () { textToBar (lpTe, lpBar, false); if (*layoutFn) (*layoutFn)(); };

    // Buttons: OK / CANCEL
    aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // Layout using standard prompt sizing
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);

    const int margin     = kPromptInnerMargin;
    const int toggleSide = 26;

    const juce::Font  promptFont (juce::FontOptions (34.0f).withStyle ("Bold"));
    const juce::Font  slopeFont  (juce::FontOptions (24.0f).withStyle ("Bold"));

    // ── Create persistent labels (repositioned by layoutRows) ──
    auto* hpNameLabel = new juce::Label ("", "HP");
    hpNameLabel->setJustificationType (juce::Justification::centredLeft);
    hpNameLabel->setColour (juce::Label::textColourId, scheme.text);
    hpNameLabel->setBorderSize (juce::BorderSize<int> (0));
    hpNameLabel->setFont (promptFont);
    aw->addAndMakeVisible (hpNameLabel);

    auto* lpNameLabel = new juce::Label ("", "LP");
    lpNameLabel->setJustificationType (juce::Justification::centredLeft);
    lpNameLabel->setColour (juce::Label::textColourId, scheme.text);
    lpNameLabel->setBorderSize (juce::BorderSize<int> (0));
    lpNameLabel->setFont (promptFont);
    aw->addAndMakeVisible (lpNameLabel);

    auto* hpHzLabel = new juce::Label ("", "Hz");
    hpHzLabel->setJustificationType (juce::Justification::centredLeft);
    hpHzLabel->setColour (juce::Label::textColourId, scheme.text);
    hpHzLabel->setBorderSize (juce::BorderSize<int> (0));
    hpHzLabel->setFont (promptFont);
    aw->addAndMakeVisible (hpHzLabel);

    auto* lpHzLabel = new juce::Label ("", "Hz");
    lpHzLabel->setJustificationType (juce::Justification::centredLeft);
    lpHzLabel->setColour (juce::Label::textColourId, scheme.text);
    lpHzLabel->setBorderSize (juce::BorderSize<int> (0));
    lpHzLabel->setFont (promptFont);
    aw->addAndMakeVisible (lpHzLabel);

    // ── Prepare TextEditors via shared helper ──
    preparePromptTextEditor (*aw, "hpFreq", scheme.bg, scheme.text, scheme.fg, promptFont, false);
    preparePromptTextEditor (*aw, "lpFreq", scheme.bg, scheme.text, scheme.fg, promptFont, false);

    // Clicking the HP / LP name label toggles its checkbox
    struct ToggleForwarder : public juce::MouseListener
    {
        juce::ToggleButton* toggle = nullptr;
        void mouseDown (const juce::MouseEvent&) override
        {
            if (toggle != nullptr)
                toggle->setToggleState (! toggle->getToggleState(), juce::sendNotification);
        }
    };
    hpNameLabel->setInterceptsMouseClicks (true, false);
    auto* hpFwd = new ToggleForwarder();
    hpFwd->toggle = hpToggle;
    hpNameLabel->addMouseListener (hpFwd, false);

    lpNameLabel->setInterceptsMouseClicks (true, false);
    auto* lpFwd = new ToggleForwarder();
    lpFwd->toggle = lpToggle;
    lpNameLabel->addMouseListener (lpFwd, false);

    auto hpFwdGuard = std::shared_ptr<ToggleForwarder> (hpFwd);
    auto lpFwdGuard = std::shared_ptr<ToggleForwarder> (lpFwd);

    // ── Re-callable layout (visual-centering approach) ──
    auto layoutRows = [aw, hpToggle, lpToggle,
                        hpNameLabel, lpNameLabel, hpHzLabel, lpHzLabel,
                        hpSlopeLabel, lpSlopeLabel, hpBar, lpBar,
                        promptFont, slopeFont, toggleSide, margin] ()
    {
        auto* hpTe = aw->getTextEditor ("hpFreq");
        auto* lpTe = aw->getTextEditor ("lpFreq");
        if (hpTe == nullptr || lpTe == nullptr) return;

        const int buttonsTop = getAlertButtonsTop (*aw);
        const int rowH       = hpTe->getHeight();
        const int barH       = juce::jmax (10, rowH / 2);
        const int barGap     = juce::jmax (2, rowH / 6);
        const int gap        = juce::jmax (4, rowH / 3);
        const int rowTotal   = rowH + barGap + barH;
        const int totalH     = rowTotal * 2 + gap;
        const int startY     = juce::jmax (kPromptEditorMinTopPx, (buttonsTop - totalH) / 2);

        const int barX = margin;
        const int barR = aw->getWidth() - margin;

        constexpr int toggleVisualInsetLeft = 2;
        constexpr int tglGap = 4;
        const int toggleVisualSide = juce::jlimit (14,
                                                   juce::jmax (14, toggleSide - 2),
                                                   (int) std::lround ((double) toggleSide * 0.65));
        const int labelOffset = toggleVisualInsetLeft + toggleVisualSide + tglGap;

        const int nameW  = stringWidth (slopeFont, "LP") + 2;
        const int slopeW = stringWidth (slopeFont, "24dB") + 4;
        const int hzGap  = 2;
        const int hzW    = stringWidth (promptFont, "Hz") + 2;

        auto placeRow = [&] (juce::ToggleButton* toggle, juce::Label* nameLabel,
                             juce::TextEditor* te, juce::Label* hzLabel,
                             juce::Label* slopeLabel, PromptBar* bar, int y)
        {
            nameLabel->setFont (slopeFont);
            hzLabel->setFont (promptFont);
            slopeLabel->setFont (slopeFont);

            toggle->setBounds (barX, y + (rowH - toggleSide) / 2, toggleSide, toggleSide);

            const int nameX = barX + labelOffset;
            nameLabel->setBounds (nameX, y, nameW, rowH);

            const int slopeX = barR - slopeW;
            slopeLabel->setBounds (slopeX, y, slopeW, rowH);

            const int midL = nameX + nameW;
            const int midR = slopeX;
            const int midW = midR - midL;

            const auto txt   = te->getText();
            const int textW  = juce::jmax (1, stringWidth (promptFont, txt));
            constexpr int kEditorPad = 6;
            const int editorW = textW + kEditorPad * 2;
            const int groupW  = editorW + hzGap + hzW;

            const int groupX = midL + juce::jmax (0, (midW - groupW) / 2);

            te->setBounds (groupX, y, editorW, rowH);
            hzLabel->setBounds (groupX + editorW + hzGap, y, hzW, rowH);

            const int barW = juce::jmax (60, barR - barX);
            bar->setBounds (barX, y + rowH + barGap, barW, barH);
        };

        placeRow (hpToggle, hpNameLabel, hpTe, hpHzLabel, hpSlopeLabel, hpBar, startY);
        placeRow (lpToggle, lpNameLabel, lpTe, lpHzLabel, lpSlopeLabel, lpBar, startY + rowTotal + gap);
    };

    // First layout pass
    layoutRows();
    *layoutFn = layoutRows;

    // Re-apply preparePromptTextEditor then re-layout
    preparePromptTextEditor (*aw, "hpFreq", scheme.bg, scheme.text, scheme.fg, promptFont, false);
    preparePromptTextEditor (*aw, "lpFreq", scheme.bg, scheme.text, scheme.fg, promptFont, false);
    layoutRows();

    styleAlertButtons (*aw, lnf);

    // Store initial values for CANCEL restore
    const float origHpFreq  = hpFreq;
    const float origLpFreq  = lpFreq;
    const int   origHpSlope = hpSlope;
    const int   origLpSlope = lpSlope;
    const bool  origHpOn    = hpOn;
    const bool  origLpOn    = lpOn;

    fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutRows] (juce::AlertWindow& a)
    {
        layoutAlertWindowButtons (a);
        layoutRows();
    });

    embedAlertWindowInOverlay (safeThis.getComponent(), aw);

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safeThis, aw, origHpFreq, origLpFreq, origHpSlope, origLpSlope, origHpOn, origLpOn,
             hpCyclerGuard, lpCyclerGuard, hpFwdGuard, lpFwdGuard] (int result)
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);

            if (safeThis == nullptr)
                return;

            if (result != 1)
            {
                // CANCEL — restore original values
                auto& p = safeThis->audioProcessor;
                auto setP = [&p] (const char* id, float plain)
                {
                    if (auto* param = p.apvts.getParameter (id))
                        param->setValueNotifyingHost (param->convertTo0to1 (plain));
                };
                setP (FREQTRAudioProcessor::kParamFilterHpFreq, origHpFreq);
                setP (FREQTRAudioProcessor::kParamFilterLpFreq, origLpFreq);
                setP (FREQTRAudioProcessor::kParamFilterHpSlope, (float) origHpSlope);
                setP (FREQTRAudioProcessor::kParamFilterLpSlope, (float) origLpSlope);
                if (auto* hpOnParam = p.apvts.getParameter (FREQTRAudioProcessor::kParamFilterHpOn))
                    hpOnParam->setValueNotifyingHost (origHpOn ? 1.0f : 0.0f);
                if (auto* lpOnParam = p.apvts.getParameter (FREQTRAudioProcessor::kParamFilterLpOn))
                    lpOnParam->setValueNotifyingHost (origLpOn ? 1.0f : 0.0f);

                safeThis->filterBar_.updateFromProcessor();
            }

            safeThis->setPromptOverlayActive (false);
        }),
        false);
}

void FREQTRAudioProcessorEditor::openRetrigPrompt()
{
    const bool wasOn = audioProcessor.apvts.getParameterAsValue (
        FREQTRAudioProcessor::kParamRetrig).getValue();
    if (auto* p = audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamRetrig))
        p->setValueNotifyingHost (wasOn ? 0.0f : 1.0f);

    const auto newTip = formatRetrigTooltip (! wasOn);
    retrigDisplay.setTooltip (newTip);

    // Show updated text immediately, then schedule auto-hide when mouse leaves
    if (tooltipWindow != nullptr)
    {
        const auto screenPos = retrigDisplay.localPointToGlobal (juce::Point<int> (0, 0));
        tooltipWindow->displayTip (screenPos, newTip);
        scheduleRetrigTipAutoHide();
    }
}

void FREQTRAudioProcessorEditor::scheduleRetrigTipAutoHide()
{
    juce::Timer::callAfterDelay (80,
        [safeThis = juce::Component::SafePointer<FREQTRAudioProcessorEditor> (this)]()
        {
            if (safeThis == nullptr) return;

            const auto mouseScreenPos = juce::Desktop::getInstance()
                                            .getMainMouseSource().getScreenPosition().toInt();
            const auto retrigScreenBounds = safeThis->retrigDisplay.getScreenBounds();

            if (retrigScreenBounds.contains (mouseScreenPos))
            {
                safeThis->scheduleRetrigTipAutoHide();   // still hovering — check again
            }
            else
            {
                if (safeThis->tooltipWindow != nullptr)
                    safeThis->tooltipWindow->hideTip();   // mouse left — hide
            }
        });
}

void FREQTRAudioProcessorEditor::openPdcMaxWindowPrompt()
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

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

    auto setMaxWindowParam = [this] (int window)
    {
        if (auto* p = audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamMaxWindow))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) FREQTRAudioProcessor::getCanonicalHilbertWindow (window)));
        syncWindowToMax (true);
        updatePdcTooltip();
    };

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("maxwin", juce::String (currentMaxWindow), juce::String());

    struct WindowInputFilter : juce::TextEditor::InputFilter
    {
        juce::String filterNewText (juce::TextEditor& editor, const juce::String& newText) override
        {
            juce::String result;
            for (auto c : newText)
            {
                if (juce::CharacterFunctions::isDigit (c))
                    result += c;
                if (result.length() >= 4)
                    break;
            }

            juce::String proposed = editor.getText();
            const int insertPos = editor.getCaretPosition();
            proposed = proposed.substring (0, insertPos) + result
                     + proposed.substring (insertPos + editor.getHighlightedText().length());

            if (proposed.length() > 4 || proposed.getIntValue() > FREQTRAudioProcessor::kHilbertWindowMax)
                return {};

            return result;
        }
    };

    struct PromptBar : public juce::Component
    {
        FREQScheme colours;
        float value01 = 1.0f;
        std::function<void (float)> onValueChanged;

        PromptBar (const FREQScheme& s, float initial01)
            : colours (s), value01 (juce::jlimit (0.0f, 1.0f, initial01)) {}

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (colours.outline);
            g.drawRect (r, 4.0f);
            auto inner = r.reduced (7.0f);
            g.setColour (colours.bg);
            g.fillRect (inner);
            g.setColour (colours.fg);
            g.fillRect (inner.withWidth (inner.getWidth() * value01));
        }

        void mouseDown (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDrag (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDoubleClick (const juce::MouseEvent&) override { setValue (1.0f); }

        void setValue (float newValue01)
        {
            value01 = juce::jlimit (0.0f, 1.0f, newValue01);
            repaint();
            if (onValueChanged)
                onValueChanged (value01);
        }

    private:
        void updateFromMouse (const juce::MouseEvent& e)
        {
            const float innerW = (float) getWidth() - 14.0f;
            const float next = innerW > 0.0f ? ((float) e.x - 7.0f) / innerW : 0.0f;
            setValue (next);
        }
    };

    const auto& f = kBoldFont40();
    auto* nameLabel = new juce::Label ("maxwin_label", "MAX WIN");
    nameLabel->setJustificationType (juce::Justification::centredLeft);
    nameLabel->setBorderSize (juce::BorderSize<int> (0));
    nameLabel->setFont (f);
    applyLabelTextColour (*nameLabel, scheme.text);
    aw->addAndMakeVisible (nameLabel);

    auto* bar = new PromptBar (scheme, windowToBar (currentMaxWindow));
    aw->addAndMakeVisible (bar);

    if (auto* te = aw->getTextEditor ("maxwin"))
    {
        te->setFont (f);
        te->applyFontToAllText (f);
        te->setInputFilter (new WindowInputFilter(), true);
    }

    auto syncing = std::make_shared<bool> (false);
    auto layoutRows = [aw, nameLabel, bar]()
    {
        auto* te = aw->getTextEditor ("maxwin");
        if (te == nullptr || nameLabel == nullptr || bar == nullptr)
            return;

        const int buttonsTop = getAlertButtonsTop (*aw);
        auto er = te->getBounds();
        er.setHeight ((int) (te->getFont().getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
        const int rowH = er.getHeight();
        const int barH = juce::jmax (12, rowH / 2);
        const int barGap = juce::jmax (4, rowH / 4);
        const int totalH = rowH + barGap + barH;
        const int y = juce::jmax (kPromptEditorMinTopPx, (buttonsTop - totalH) / 2);
        const int contentPad = kPromptInnerMargin;
        const int contentW = aw->getWidth() - contentPad * 2;
        const int labelW = stringWidth (nameLabel->getFont(), nameLabel->getText()) + 8;
        const int textW = juce::jmax (stringWidth (te->getFont(), "2048"), stringWidth (te->getFont(), te->getText()));
        const int editorW = juce::jmax (40, textW + 10);
        const int gap = juce::jmax (8, stringWidth (te->getFont(), " "));
        const int visualW = labelW + gap + editorW;
        const int blockLeft = contentPad + juce::jmax (0, (contentW - visualW) / 2);

        nameLabel->setBounds (blockLeft, y, labelW, rowH);
        te->setBounds (blockLeft + labelW + gap, y, editorW, rowH);
        bar->setBounds (contentPad, y + rowH + barGap, contentW, barH);
    };

    bar->onValueChanged = [aw, barToWindow, setMaxWindowParam, syncing, layoutRows] (float value01)
    {
        if (*syncing)
            return;

        *syncing = true;
        const int window = barToWindow (value01);
        if (auto* te = aw->getTextEditor ("maxwin"))
        {
            te->setText (juce::String (window), juce::sendNotification);
            te->selectAll();
        }
        *syncing = false;
        setMaxWindowParam (window);
        layoutRows();
    };

    if (auto* te = aw->getTextEditor ("maxwin"))
        te->onTextChange = [aw, bar, windowToBar, setMaxWindowParam, syncing, layoutRows]()
        {
            if (*syncing)
                return;

            *syncing = true;
            int window = FREQTRAudioProcessor::kHilbertMaxWindowDefault;
            if (auto* editor = aw->getTextEditor ("maxwin"))
                window = FREQTRAudioProcessor::getCanonicalHilbertWindow (editor->getText().getIntValue());
            bar->setValue (windowToBar (window));
            *syncing = false;
            setMaxWindowParam (window);
            layoutRows();
        };

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);
    preparePromptTextEditor (*aw, "maxwin", scheme.bg, scheme.text, scheme.fg, f, false);
    layoutRows();
    styleAlertButtons (*aw, lnf);

    setPromptOverlayActive (true);
    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);
    fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutRows] (juce::AlertWindow& a)
    {
        juce::ignoreUnused (a);
        layoutAlertWindowButtons (a);
        layoutRows();
    });
    embedAlertWindowInOverlay (safeThis.getComponent(), aw);

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safeThis, aw, savedMaxWindow = currentMaxWindow, savedPluginWindow = currentPluginWindow] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);
            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);
            if (safeThis == nullptr)
                return;

            if (result != 1)
            {
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamMaxWindow))
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) savedMaxWindow));
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamWindow))
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jmin (savedPluginWindow, savedMaxWindow)));
            }
            safeThis->syncWindowToMax (true);
            safeThis->updatePdcTooltip();
        }),
        false);
}

void FREQTRAudioProcessorEditor::openSidechainPrompt()
{
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    const float currentTime = juce::jlimit (FREQTRAudioProcessor::kSidechainTimeMin,
                                            FREQTRAudioProcessor::kSidechainTimeMax,
                                            audioProcessor.apvts.getRawParameterValue (
                                                FREQTRAudioProcessor::kParamSidechainTime)->load());
    const float currentTone = juce::jlimit (FREQTRAudioProcessor::kSidechainToneMin,
                                            FREQTRAudioProcessor::kSidechainToneMax,
                                            audioProcessor.apvts.getRawParameterValue (
                                                FREQTRAudioProcessor::kParamSidechainTone)->load());

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("time", juce::String (currentTime, 2), juce::String());
    aw->addTextEditor ("tone", juce::String (juce::roundToInt (currentTone)), juce::String());

    struct PromptBar : public juce::Component
    {
        FREQScheme colours;
        float value01 = 0.25f;
        float default01 = 0.25f;
        std::function<void (float)> onValueChanged;

        PromptBar (const FREQScheme& s, float initial01, float def01)
            : colours (s),
              value01 (juce::jlimit (0.0f, 1.0f, initial01)),
              default01 (juce::jlimit (0.0f, 1.0f, def01)) {}

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (colours.outline);
            g.drawRect (r, 4.0f);

            const float pad = 7.0f;
            auto inner = r.reduced (pad);
            g.setColour (colours.bg);
            g.fillRect (inner);

            g.setColour (colours.fg);
            g.fillRect (inner.withWidth (inner.getWidth() * value01));
        }

        void mouseDown (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDrag (const juce::MouseEvent& e) override { updateFromMouse (e); }
        void mouseDoubleClick (const juce::MouseEvent&) override { setValue (default01); }

        void setValue (float newValue01)
        {
            value01 = juce::jlimit (0.0f, 1.0f, newValue01);
            repaint();
            if (onValueChanged)
                onValueChanged (value01);
        }

    private:
        void updateFromMouse (const juce::MouseEvent& e)
        {
            const float pad = 7.0f;
            const float innerX = pad;
            const float innerW = (float) getWidth() - pad * 2.0f;
            const float next = (innerW > 0.0f) ? ((float) e.x - innerX) / innerW : 0.0f;
            setValue (next);
        }
    };

    struct UnitInputFilter : juce::TextEditor::InputFilter
    {
        float maxValue = 1.0f;
        int maxLength = 4;
        bool allowDecimal = true;

        UnitInputFilter (float maxVal, int maxLen, bool decimal)
            : maxValue (maxVal), maxLength (maxLen), allowDecimal (decimal) {}

        juce::String filterNewText (juce::TextEditor& editor, const juce::String& newText) override
        {
            const bool existingHasDot = editor.getText().containsChar ('.');
            bool seenDot = false;
            juce::String result;

            for (auto c : newText)
            {
                if (c == '.')
                {
                    if (! allowDecimal || seenDot || existingHasDot)
                        continue;
                    seenDot = true;
                    result += c;
                }
                else if (juce::CharacterFunctions::isDigit (c))
                {
                    result += c;
                }

                if (result.length() >= maxLength)
                    break;
            }

            juce::String proposed = editor.getText();
            const int insertPos = editor.getCaretPosition();
            proposed = proposed.substring (0, insertPos) + result
                     + proposed.substring (insertPos + editor.getHighlightedText().length());

            if (proposed.length() > maxLength || proposed.getFloatValue() > maxValue)
                return {};

            if (allowDecimal && proposed.length() > 1 && proposed[0] == '0' && proposed[1] != '.')
                return {};

            return result;
        }
    };

    const auto& f = kBoldFont40();

    auto makeLabel = [&] (const juce::String& name, const juce::String& text)
    {
        auto* l = new juce::Label (name, text);
        l->setJustificationType (juce::Justification::centredLeft);
        applyLabelTextColour (*l, scheme.text);
        l->setBorderSize (juce::BorderSize<int> (0));
        l->setFont (f);
        aw->addAndMakeVisible (l);
        return l;
    };

    auto* timeLabel = makeLabel ("sc_time_label", "TIME");
    auto* toneLabel = makeLabel ("sc_tone_label", "TONE");
    auto* timeUnit = makeLabel ("sc_time_unit", "x");
    auto* toneUnit = makeLabel ("sc_tone_unit", "Hz");

    const float toneLogMin = std::log (FREQTRAudioProcessor::kSidechainToneMin);
    const float toneLogRange = std::log (FREQTRAudioProcessor::kSidechainToneMax) - toneLogMin;
    auto toneToBar = [toneLogMin, toneLogRange] (float hz)
    {
        const float clamped = juce::jlimit (FREQTRAudioProcessor::kSidechainToneMin,
                                            FREQTRAudioProcessor::kSidechainToneMax, hz);
        return (std::log (clamped) - toneLogMin) / toneLogRange;
    };
    auto barToTone = [toneLogMin, toneLogRange] (float value01)
    {
        return std::exp (toneLogMin + juce::jlimit (0.0f, 1.0f, value01) * toneLogRange);
    };

    auto* timeBar = new PromptBar (scheme, currentTime, FREQTRAudioProcessor::kSidechainTimeDefault);
    auto* toneBar = new PromptBar (scheme, toneToBar (currentTone), toneToBar (FREQTRAudioProcessor::kSidechainToneDefault));
    aw->addAndMakeVisible (timeBar);
    aw->addAndMakeVisible (toneBar);

    if (auto* te = aw->getTextEditor ("time"))
    {
        te->setFont (f);
        te->applyFontToAllText (f);
        te->setInputFilter (new UnitInputFilter (1.0f, 4, true), true);
    }
    if (auto* te = aw->getTextEditor ("tone"))
    {
        te->setFont (f);
        te->applyFontToAllText (f);
        te->setInputFilter (new UnitInputFilter (FREQTRAudioProcessor::kSidechainToneMax, 4, false), true);
    }

    auto syncing = std::make_shared<bool> (false);
    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);

    auto setSidechainParam = [safeThis] (const char* paramId, float plainValue)
    {
        if (safeThis == nullptr)
            return;

        if (auto* p = safeThis->audioProcessor.apvts.getParameter (paramId))
            p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    };

    auto pushSidechainValues = [safeThis, setSidechainParam] (float time, float tone)
    {
        const float clampedTime = juce::jlimit (FREQTRAudioProcessor::kSidechainTimeMin,
                                                FREQTRAudioProcessor::kSidechainTimeMax, time);
        const float clampedTone = juce::jlimit (FREQTRAudioProcessor::kSidechainToneMin,
                                                FREQTRAudioProcessor::kSidechainToneMax, tone);

        setSidechainParam (FREQTRAudioProcessor::kParamSidechainTime, clampedTime);
        setSidechainParam (FREQTRAudioProcessor::kParamSidechainTone, clampedTone);

        if (safeThis != nullptr)
            safeThis->sidechainDisplay.setTooltip (formatSidechainTooltip (clampedTime, clampedTone));
    };

    auto layoutRows = [aw, timeLabel, toneLabel, timeUnit, toneUnit, timeBar, toneBar]()
    {
        auto* timeTe = aw->getTextEditor ("time");
        auto* toneTe = aw->getTextEditor ("tone");
        if (timeTe == nullptr || toneTe == nullptr
            || timeLabel == nullptr || toneLabel == nullptr
            || timeUnit == nullptr || toneUnit == nullptr
            || timeBar == nullptr || toneBar == nullptr)
            return;

        const int buttonsTop = getAlertButtonsTop (*aw);
        auto er = timeTe->getBounds();
        er.setHeight ((int) (timeTe->getFont().getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
        const int rowH = er.getHeight();
        const int barH = juce::jmax (12, rowH / 2);
        const int barGap = juce::jmax (4, rowH / 4);
        const int rowTotal = rowH + barGap + barH;
        const int rowGap = juce::jmax (4, rowH / 3);
        const int totalH = rowTotal * 2 + rowGap;
        const int rowY = juce::jmax (kPromptEditorMinTopPx, (buttonsTop - totalH) / 2);

        const int contentPad = kPromptInnerMargin;
        const int contentW = aw->getWidth() - contentPad * 2;
        const int maxLabelW = juce::jmax (stringWidth (timeLabel->getFont(), timeLabel->getText()),
                                          stringWidth (toneLabel->getFont(), toneLabel->getText())) + 8;
        const int labelValueGap = juce::jmax (8, stringWidth (timeTe->getFont(), " "));
        const int unitGap = 2;

        auto placeRow = [&] (juce::TextEditor* te, juce::Label* name, juce::Label* unit,
                             PromptBar* bar, int y)
        {
            const int textW = juce::jmax (1, stringWidth (te->getFont(), te->getText()));
            const bool isToneRow = te == toneTe;
            const int worstTextW = stringWidth (te->getFont(), isToneRow ? "5000" : "1.00");
            const int unitW = stringWidth (unit->getFont(), unit->getText()) + 4;
            const int idealEditorW = juce::jmax (36, juce::jmax (textW, worstTextW) + 10);
            const int maxEditorW = juce::jmax (36, contentW - maxLabelW - labelValueGap - unitGap - unitW);
            const int editorW = juce::jmin (idealEditorW, maxEditorW);
            const int visualW = maxLabelW + labelValueGap + editorW + unitGap + unitW;
            const int availableShift = juce::jmax (0, contentW - visualW);
            const int blockLeft = contentPad + (availableShift / 2);

            name->setBounds (blockLeft, y, maxLabelW, rowH);
            auto textBounds = juce::Rectangle<int> (blockLeft + maxLabelW + labelValueGap, y, editorW, rowH);
            te->setBounds (textBounds);
            unit->setBounds (textBounds.getRight() + unitGap, y, unitW, rowH);
            bar->setBounds (contentPad, y + rowH + barGap, contentW, barH);
        };

        placeRow (timeTe, timeLabel, timeUnit, timeBar, rowY);
        placeRow (toneTe, toneLabel, toneUnit, toneBar, rowY + rowTotal + rowGap);
    };

    timeBar->onValueChanged = [aw, toneBar, syncing, layoutRows, pushSidechainValues, barToTone] (float value01)
    {
        if (*syncing)
            return;

        *syncing = true;
        if (auto* te = aw->getTextEditor ("time"))
        {
            te->setText (juce::String (value01, 2), juce::sendNotification);
            te->selectAll();
        }
        *syncing = false;
        pushSidechainValues (value01, barToTone (toneBar->value01));
        layoutRows();
    };

    toneBar->onValueChanged = [aw, timeBar, syncing, layoutRows, pushSidechainValues, barToTone] (float value01)
    {
        if (*syncing)
            return;

        *syncing = true;
        const float toneHz = juce::jlimit (FREQTRAudioProcessor::kSidechainToneMin,
                                           FREQTRAudioProcessor::kSidechainToneMax,
                                           barToTone (value01));
        if (auto* te = aw->getTextEditor ("tone"))
        {
            te->setText (juce::String (juce::roundToInt (toneHz)), juce::sendNotification);
            te->selectAll();
        }
        *syncing = false;
        pushSidechainValues (timeBar->value01, toneHz);
        layoutRows();
    };

    if (auto* te = aw->getTextEditor ("time"))
        te->onTextChange = [aw, timeBar, toneBar, syncing, layoutRows, pushSidechainValues, barToTone]()
        {
            if (*syncing)
                return;

            *syncing = true;
            float nextTime = FREQTRAudioProcessor::kSidechainTimeMin;
            if (auto* editor = aw->getTextEditor ("time"))
                nextTime = juce::jlimit (FREQTRAudioProcessor::kSidechainTimeMin,
                                         FREQTRAudioProcessor::kSidechainTimeMax,
                                         editor->getText().getFloatValue());
            timeBar->setValue (nextTime);
            *syncing = false;
            pushSidechainValues (nextTime, barToTone (toneBar->value01));
            layoutRows();
        };

    if (auto* te = aw->getTextEditor ("tone"))
        te->onTextChange = [aw, timeBar, toneBar, syncing, layoutRows, pushSidechainValues, toneToBar]()
        {
            if (*syncing)
                return;

            *syncing = true;
            float nextTone = FREQTRAudioProcessor::kSidechainToneMin;
            if (auto* editor = aw->getTextEditor ("tone"))
                nextTone = juce::jlimit (FREQTRAudioProcessor::kSidechainToneMin,
                                         FREQTRAudioProcessor::kSidechainToneMax,
                                         editor->getText().getFloatValue());
            toneBar->setValue (toneToBar (nextTone));
            *syncing = false;
            pushSidechainValues (timeBar->value01, nextTone);
            layoutRows();
        };

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);
    preparePromptTextEditor (*aw, "time", scheme.bg, scheme.text, scheme.fg, f, false);
    preparePromptTextEditor (*aw, "tone", scheme.bg, scheme.text, scheme.fg, f, false);
    layoutRows();
    styleAlertButtons (*aw, lnf);

    setPromptOverlayActive (true);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutRows] (juce::AlertWindow& a)
        {
            juce::ignoreUnused (a);
            layoutAlertWindowButtons (a);
            layoutRows();
        });
        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
    }

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safeThis, aw, savedTime = currentTime, savedTone = currentTone] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);

            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);

            if (safeThis == nullptr)
                return;

            if (result != 1)
            {
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamSidechainTime))
                    p->setValueNotifyingHost (p->convertTo0to1 (savedTime));
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamSidechainTone))
                    p->setValueNotifyingHost (p->convertTo0to1 (savedTone));
                safeThis->sidechainDisplay.setTooltip (formatSidechainTooltip (savedTime, savedTone));
                return;
            }

            float newTime = savedTime;
            float newTone = savedTone;
            {
                if (auto* te = aw->getTextEditor ("time"))
                    newTime = juce::jlimit (FREQTRAudioProcessor::kSidechainTimeMin,
                                            FREQTRAudioProcessor::kSidechainTimeMax,
                                            te->getText().getFloatValue());
                if (auto* te = aw->getTextEditor ("tone"))
                    newTone = juce::jlimit (FREQTRAudioProcessor::kSidechainToneMin,
                                            FREQTRAudioProcessor::kSidechainToneMax,
                                            te->getText().getFloatValue());

                if (auto* p = safeThis->audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamSidechainTime))
                    p->setValueNotifyingHost (p->convertTo0to1 (newTime));
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (FREQTRAudioProcessor::kParamSidechainTone))
                    p->setValueNotifyingHost (p->convertTo0to1 (newTone));
            }

            safeThis->sidechainDisplay.setTooltip (formatSidechainTooltip (newTime, newTone));
        }),
        false);
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

// ═══════════════════ Chaos prompts ═══════════════════

void FREQTRAudioProcessorEditor::openChaosConfigPrompt (const char* amtParamId,
                                                         const char* spdParamId,
                                                         const juce::String& title)
{
    juce::ignoreUnused (title);
    using namespace TR;
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    const float currentAmt = audioProcessor.apvts.getRawParameterValue (amtParamId)->load();
    const float currentSpd = audioProcessor.apvts.getRawParameterValue (spdParamId)->load();

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);

    aw->addTextEditor ("amt", juce::String (juce::roundToInt (currentAmt)), juce::String());
    aw->addTextEditor ("spd", juce::String (currentSpd, 2), juce::String());

    // ── Inline bar component ──
    struct PromptBar : public juce::Component
    {
        FREQScheme colours;
        float value      = 0.5f;
        float defaultVal = 0.5f;
        std::function<void (float)> onValueChanged;

        PromptBar (const FREQScheme& s, float initial01, float default01)
            : colours (s), value (initial01), defaultVal (default01) {}

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (colours.outline);
            g.drawRect (r, 4.0f);

            const float pad = 7.0f;
            auto inner = r.reduced (pad);

            g.setColour (colours.bg);
            g.fillRect (inner);

            const float fillW = juce::jlimit (0.0f, inner.getWidth(), inner.getWidth() * value);
            g.setColour (colours.fg);
            g.fillRect (inner.withWidth (fillW));
        }

        void mouseDown (const juce::MouseEvent& e) override  { updateFromMouse (e); }
        void mouseDrag (const juce::MouseEvent& e) override  { updateFromMouse (e); }
        void mouseDoubleClick (const juce::MouseEvent&) override { setValue (defaultVal); }

        void setValue (float v01)
        {
            value = juce::jlimit (0.0f, 1.0f, v01);
            repaint();
            if (onValueChanged)
                onValueChanged (value);
        }

    private:
        void updateFromMouse (const juce::MouseEvent& e)
        {
            const float pad = 7.0f;
            const float innerX = pad;
            const float innerW = (float) getWidth() - pad * 2.0f;
            const float v = (innerW > 0.0f) ? ((float) e.x - innerX) / innerW : 0.0f;
            setValue (v);
        }
    };

    struct ResetLabel : public juce::Label
    {
        PromptBar* pairedBar = nullptr;
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (pairedBar != nullptr)
                pairedBar->setValue (pairedBar->defaultVal);
        }
    };

    const auto& f = kBoldFont40();

    ResetLabel* amtSuffix    = nullptr;
    ResetLabel* spdSuffix    = nullptr;
    juce::Label* amtUnitLabel = nullptr;
    juce::Label* spdUnitLabel = nullptr;

    auto setupField = [&] (const char* editorId, const juce::String& suffixText,
                           const juce::String& unitText, bool useDecimalFilter,
                           ResetLabel*& suffixOut, juce::Label*& unitOut)
    {
        if (auto* te = aw->getTextEditor (editorId))
        {
            te->setFont (f);
            te->applyFontToAllText (f);

            if (useDecimalFilter)
                te->setInputRestrictions (6, "0123456789.");
            else
                te->setInputFilter (new PctInputFilter(), true);

            auto r = te->getBounds();
            r.setHeight ((int) (f.getHeight() * kPromptEditorHeightScale) + kPromptEditorHeightPadPx);
            te->setBounds (r);

            suffixOut = new ResetLabel();
            suffixOut->setText (suffixText, juce::dontSendNotification);
            suffixOut->setJustificationType (juce::Justification::centredLeft);
            applyLabelTextColour (*suffixOut, scheme.text);
            suffixOut->setBorderSize (juce::BorderSize<int> (0));
            suffixOut->setFont (f);
            aw->addAndMakeVisible (suffixOut);

            unitOut = new juce::Label ("", unitText);
            unitOut->setJustificationType (juce::Justification::centredLeft);
            applyLabelTextColour (*unitOut, scheme.text);
            unitOut->setBorderSize (juce::BorderSize<int> (0));
            unitOut->setFont (f);
            aw->addAndMakeVisible (unitOut);
        }
    };

    setupField ("amt", "AMT", "%",  false, amtSuffix, amtUnitLabel);
    setupField ("spd", "SPD", "Hz", true,  spdSuffix, spdUnitLabel);

    // Bars: AMOUNT 0-100 → 0..1, SPEED 0.01-100 Hz → 0..1 (logarithmic)
    const float spdLogMin   = std::log (FREQTRAudioProcessor::kChaosSpdMin);
    const float spdLogMax   = std::log (FREQTRAudioProcessor::kChaosSpdMax);
    const float spdLogRange = spdLogMax - spdLogMin;

    auto hzToBar = [spdLogMin, spdLogRange] (float hz) -> float
    {
        if (hz <= FREQTRAudioProcessor::kChaosSpdMin) return 0.0f;
        if (hz >= FREQTRAudioProcessor::kChaosSpdMax) return 1.0f;
        return (std::log (hz) - spdLogMin) / spdLogRange;
    };

    auto barToHz = [spdLogMin, spdLogRange] (float v01) -> float
    {
        return std::exp (spdLogMin + v01 * spdLogRange);
    };

    auto* amtBar = new PromptBar (scheme, currentAmt * 0.01f,
                                  FREQTRAudioProcessor::kChaosAmtDefault * 0.01f);
    auto* spdBar = new PromptBar (scheme,
                                  hzToBar (currentSpd),
                                  hzToBar (FREQTRAudioProcessor::kChaosSpdDefault));
    aw->addAndMakeVisible (amtBar);
    aw->addAndMakeVisible (spdBar);

    if (amtSuffix != nullptr) amtSuffix->pairedBar = amtBar;
    if (spdSuffix != nullptr) spdSuffix->pairedBar = spdBar;

    auto syncing = std::make_shared<bool> (false);

    auto* amtApvts = audioProcessor.apvts.getParameter (amtParamId);
    auto* spdApvts = audioProcessor.apvts.getParameter (spdParamId);

    // Bar → text + APVTS
    auto barToTextAmt = [aw, syncing, amtApvts] (float v01)
    {
        if (*syncing) return;
        *syncing = true;
        if (auto* te = aw->getTextEditor ("amt"))
        {
            te->setText (juce::String (juce::roundToInt (v01 * 100.0f)), juce::sendNotification);
            te->selectAll();
        }
        if (amtApvts != nullptr)
            amtApvts->setValueNotifyingHost (amtApvts->convertTo0to1 (v01 * 100.0f));
        *syncing = false;
    };

    auto barToTextSpd = [aw, syncing, spdApvts, barToHz] (float v01)
    {
        if (*syncing) return;
        *syncing = true;
        const float hz = juce::jlimit (FREQTRAudioProcessor::kChaosSpdMin,
                                       FREQTRAudioProcessor::kChaosSpdMax, barToHz (v01));
        if (auto* te = aw->getTextEditor ("spd"))
        {
            te->setText (juce::String (hz, 2), juce::sendNotification);
            te->selectAll();
        }
        if (spdApvts != nullptr)
            spdApvts->setValueNotifyingHost (spdApvts->convertTo0to1 (hz));
        *syncing = false;
    };

    amtBar->onValueChanged = barToTextAmt;
    spdBar->onValueChanged = barToTextSpd;

    // Layout helper
    auto layoutRows = [aw, amtSuffix, spdSuffix, amtUnitLabel, spdUnitLabel, amtBar, spdBar] ()
    {
        auto* amtTe = aw->getTextEditor ("amt");
        auto* spdTe = aw->getTextEditor ("spd");
        if (amtTe == nullptr || spdTe == nullptr)
            return;

        const int buttonsTop = getAlertButtonsTop (*aw);
        const int rowH = amtTe->getHeight();
        const int barH = juce::jmax (10, rowH / 2);
        const int barGap = juce::jmax (2, rowH / 6);
        const int rowTotal = rowH + barGap + barH;
        const int gap = juce::jmax (4, rowH / 3);
        const int totalH = rowTotal * 2 + gap;
        const int startY = juce::jmax (kPromptEditorMinTopPx, (buttonsTop - totalH) / 2);

        const int contentPad = kPromptInlineContentPadPx;
        const int contentW = aw->getWidth() - contentPad * 2;
        const auto& font = amtTe->getFont();
        const int spaceW = juce::jmax (2, stringWidth (font, " "));

        auto placeRow = [&] (juce::TextEditor* te, juce::Label* suffix,
                             juce::Label* unitLabel, PromptBar* bar, int y)
        {
            if (te == nullptr || suffix == nullptr || bar == nullptr)
                return;

            const int labelW  = stringWidth (suffix->getFont(), suffix->getText()) + 2;
            const auto txt    = te->getText();
            const int textW   = juce::jmax (1, stringWidth (font, txt));
            const int unitW   = (unitLabel != nullptr)
                              ? stringWidth (font, unitLabel->getText()) + 2 : 0;

            constexpr int kEditorTextPadPx = 12;
            constexpr int kMinEditorWidthPx = 24;
            const int maxEditorWidthPx = (unitLabel != nullptr && unitLabel->getText() == "Hz")
                                        ? juce::jmax (80, stringWidth (font, "100.00") + kEditorTextPadPx * 2)
                                        : 80;
            const int editorW = juce::jlimit (kMinEditorWidthPx, maxEditorWidthPx,
                                              textW + kEditorTextPadPx * 2);

            const int visualW = labelW + spaceW + textW + unitW;
            const int centerX = contentPad + contentW / 2;
            int blockLeft = juce::jlimit (contentPad,
                                          juce::jmax (contentPad, contentPad + contentW - visualW),
                                          centerX - visualW / 2);

            suffix->setBounds (blockLeft, y, labelW, rowH);

            int teX = blockLeft + labelW + spaceW - (editorW - textW) / 2;
            teX = juce::jlimit (contentPad,
                                juce::jmax (contentPad, contentPad + contentW - editorW), teX);
            te->setBounds (teX, y, editorW, rowH);

            if (unitLabel != nullptr)
            {
                const int textRightX = blockLeft + labelW + spaceW + textW;
                unitLabel->setBounds (textRightX, y, unitW, rowH);
            }

            const int barX = kPromptInnerMargin;
            const int barW = juce::jmax (60, aw->getWidth() - kPromptInnerMargin * 2);
            bar->setBounds (barX, y + rowH + barGap, barW, barH);
        };

        placeRow (amtTe, amtSuffix, amtUnitLabel, amtBar, startY);
        placeRow (spdTe, spdSuffix, spdUnitLabel, spdBar, startY + rowTotal + gap);
    };

    // Text → bar + APVTS
    auto textToBar = [syncing, hzToBar] (juce::TextEditor* te, PromptBar* bar,
                                juce::RangedAudioParameter* param, bool isSpeed)
    {
        if (*syncing || te == nullptr || bar == nullptr) return;
        *syncing = true;
        const float raw = juce::jlimit (0.0f, 100.0f, te->getText().getFloatValue());
        if (isSpeed)
        {
            const float hz = juce::jlimit (FREQTRAudioProcessor::kChaosSpdMin,
                                           FREQTRAudioProcessor::kChaosSpdMax, raw);
            bar->value = hzToBar (hz);
            if (param != nullptr)
                param->setValueNotifyingHost (param->convertTo0to1 (hz));
        }
        else
        {
            bar->value = raw * 0.01f;
            if (param != nullptr)
                param->setValueNotifyingHost (param->convertTo0to1 (raw));
        }
        bar->repaint();
        *syncing = false;
    };

    if (auto* amtTe = aw->getTextEditor ("amt"))
        amtTe->onTextChange = [layoutRows, amtTe, amtBar, textToBar, amtApvts] () mutable
        {
            textToBar (amtTe, amtBar, amtApvts, false);
            layoutRows();
        };
    if (auto* spdTe = aw->getTextEditor ("spd"))
        spdTe->onTextChange = [layoutRows, spdTe, spdBar, textToBar, spdApvts] () mutable
        {
            textToBar (spdTe, spdBar, spdApvts, true);
            layoutRows();
        };

    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->setEscapeKeyCancels (true);
    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);
    layoutRows();

    const auto& kChaosFont = kBoldFont40();
    preparePromptTextEditor (*aw, "amt", scheme.bg, scheme.text, scheme.fg, kChaosFont, false);
    preparePromptTextEditor (*aw, "spd", scheme.bg, scheme.text, scheme.fg, kChaosFont, false);
    layoutRows();

    styleAlertButtons (*aw, lnf);

    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);

    if (safeThis != nullptr)
    {
        fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutRows] (juce::AlertWindow& a)
        {
            juce::ignoreUnused (a);
            layoutAlertWindowButtons (a);
            layoutRows();
        });

        embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    }
    else
    {
        aw->centreAroundComponent (this, aw->getWidth(), aw->getHeight());
        bringPromptWindowToFront (*aw);
    }

    // Final styling pass
    {
        preparePromptTextEditor (*aw, "amt", scheme.bg, scheme.text, scheme.fg, kChaosFont, false);
        preparePromptTextEditor (*aw, "spd", scheme.bg, scheme.text, scheme.fg, kChaosFont, false);
        layoutRows();

        if (amtSuffix != nullptr)
        {
            if (auto* te = aw->getTextEditor ("amt"))
            {
                amtSuffix->setFont (te->getFont());
                if (amtUnitLabel != nullptr) amtUnitLabel->setFont (te->getFont());
            }
        }
        if (spdSuffix != nullptr)
        {
            if (auto* te = aw->getTextEditor ("spd"))
            {
                spdSuffix->setFont (te->getFont());
                if (spdUnitLabel != nullptr) spdUnitLabel->setFont (te->getFont());
            }
        }

        layoutRows();

        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::MessageManager::callAsync ([safeAw]()
        {
            if (safeAw == nullptr) return;
            bringPromptWindowToFront (*safeAw);
            safeAw->repaint();
        });
    }

    setPromptOverlayActive (true);

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safeThis, aw, amtBar, spdBar,
             savedAmt = currentAmt, savedSpd = currentSpd,
             spdLogMin, spdLogRange,
             amtParamId, spdParamId] (int result) mutable
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);

            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);

            if (safeThis == nullptr)
                return;

            if (result != 1)
            {
                // CANCEL: revert to original values
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (amtParamId))
                    p->setValueNotifyingHost (p->convertTo0to1 (savedAmt));
                if (auto* p = safeThis->audioProcessor.apvts.getParameter (spdParamId))
                    p->setValueNotifyingHost (p->convertTo0to1 (savedSpd));
                return;
            }

            // OK: update tooltip
            const float newAmt = juce::jlimit (0.0f, 100.0f, amtBar->value * 100.0f);
            const float newSpd = juce::jlimit (FREQTRAudioProcessor::kChaosSpdMin,
                                                FREQTRAudioProcessor::kChaosSpdMax,
                                                std::exp (spdLogMin + juce::jlimit (0.0f, 1.0f, spdBar->value) * spdLogRange));
            auto tip = formatChaosTooltip (newAmt, newSpd);
            if (juce::String (amtParamId) == FREQTRAudioProcessor::kParamChaosAmtFilter)
                safeThis->chaosFilterDisplay.setTooltip (tip);
            else
                safeThis->chaosDelayDisplay.setTooltip (tip);
        }),
        false);
}

void FREQTRAudioProcessorEditor::openChaosFilterPrompt()
{
    openChaosConfigPrompt (FREQTRAudioProcessor::kParamChaosAmtFilter,
                           FREQTRAudioProcessor::kParamChaosSpdFilter,
                           "CHSF");
}

void FREQTRAudioProcessorEditor::openChaosDelayPrompt()
{
    openChaosConfigPrompt (FREQTRAudioProcessor::kParamChaosAmt,
                           FREQTRAudioProcessor::kParamChaosSpd,
                           "CHSD");
}

void FREQTRAudioProcessorEditor::openMixSendPrompt()
{
    using namespace TR;
    lnf.setScheme (activeScheme);
    const auto scheme = activeScheme;

    auto& proc = audioProcessor;
    const float curDry = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamDryLevel)->load();
    const float curWet = proc.apvts.getRawParameterValue (FREQTRAudioProcessor::kParamWetLevel)->load();

    auto* aw = new juce::AlertWindow ("", "", juce::AlertWindow::NoIcon);
    aw->setLookAndFeel (&lnf);

    // dB helpers
    auto linearToDb = [] (float g) -> float { return (g <= 0.0001f) ? -100.0f : 20.0f * std::log10 (g); };
    auto dbToLinear = [] (float dB) -> float { return (dB <= -100.0f) ? 0.0f : std::pow (10.0f, dB / 20.0f); };
    auto dbString = [&linearToDb] (float g) -> juce::String
    {
        const float dB = linearToDb (g);
        if (dB <= -100.0f) return "-INF";
        if (std::abs (dB) < 0.05f) return "0";
        return juce::String (dB, 1);
    };

    aw->addTextEditor ("dryLevel", dbString (curDry), juce::String());
    aw->addTextEditor ("wetLevel", dbString (curWet), juce::String());

    struct PromptBar : public juce::Component
    {
        FREQScheme colours;
        float  value01   = 1.0f;
        float  default01 = 1.0f;
        std::function<void (float)> onValueChanged;

        PromptBar (const FREQScheme& s, float initial, float def)
            : colours (s), value01 (initial), default01 (def) {}

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (colours.outline);
            g.drawRect (r, 4.0f);
            const float pad = 7.0f;
            auto inner = r.reduced (pad);
            g.setColour (colours.bg);
            g.fillRect (inner);
            const float fillW = juce::jlimit (0.0f, inner.getWidth(), inner.getWidth() * value01);
            g.setColour (colours.fg);
            g.fillRect (inner.withWidth (fillW));
        }

        void mouseDown (const juce::MouseEvent& e) override  { updateFromMouse (e); }
        void mouseDrag (const juce::MouseEvent& e) override  { updateFromMouse (e); }
        void mouseDoubleClick (const juce::MouseEvent&) override { setValue (default01); }

        void setValue (float v01)
        {
            value01 = juce::jlimit (0.0f, 1.0f, v01);
            repaint();
            if (onValueChanged) onValueChanged (value01);
        }

    private:
        void updateFromMouse (const juce::MouseEvent& e)
        {
            const float pad = 7.0f;
            const float innerW = (float) getWidth() - pad * 2.0f;
            setValue (innerW > 0.0f ? ((float) e.x - pad) / innerW : 0.0f);
        }
    };

    auto* dryBar = new PromptBar (scheme, curDry, FREQTRAudioProcessor::kDryLevelDefault);
    auto* wetBar = new PromptBar (scheme, curWet, FREQTRAudioProcessor::kWetLevelDefault);
    aw->addAndMakeVisible (dryBar);
    aw->addAndMakeVisible (wetBar);

    auto syncing  = std::make_shared<bool> (false);
    auto layoutFn = std::make_shared<std::function<void()>> ([] {});

    juce::Component::SafePointer<FREQTRAudioProcessorEditor> safeThis (this);

    auto pushParams = [safeThis, aw, dbToLinear] ()
    {
        if (safeThis == nullptr) return;
        auto& p = safeThis->audioProcessor;
        auto setP = [&p] (const char* id, float plain)
        {
            if (auto* param = p.apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (plain));
        };
        auto* dryTe = aw->getTextEditor ("dryLevel");
        auto* wetTe = aw->getTextEditor ("wetLevel");
        const float dryLin = dryTe ? juce::jlimit (0.0f, 1.0f, dbToLinear (dryTe->getText().getFloatValue())) : 1.0f;
        const float wetLin = wetTe ? juce::jlimit (0.0f, 1.0f, dbToLinear (wetTe->getText().getFloatValue())) : 1.0f;
        setP (FREQTRAudioProcessor::kParamDryLevel, dryLin);
        setP (FREQTRAudioProcessor::kParamWetLevel, wetLin);
        safeThis->dualMixBar_.updateFromProcessor();
    };

    auto barToText = [aw, syncing, pushParams, dbString] (const char* editorId, float v01)
    {
        if (*syncing) return;
        *syncing = true;
        if (auto* te = aw->getTextEditor (editorId))
        {
            te->setText (dbString (v01), juce::sendNotification);
            te->selectAll();
        }
        *syncing = false;
        pushParams();
    };

    dryBar->onValueChanged = [barToText] (float v) { barToText ("dryLevel", v); };
    wetBar->onValueChanged = [barToText] (float v) { barToText ("wetLevel", v); };

    auto textToBar = [syncing, pushParams, dbToLinear] (juce::TextEditor* te, PromptBar* bar)
    {
        if (*syncing || te == nullptr || bar == nullptr) return;
        *syncing = true;
        const float dB  = te->getText().getFloatValue();
        const float lin = juce::jlimit (0.0f, 1.0f, dbToLinear (dB));
        bar->value01 = lin;
        bar->repaint();
        *syncing = false;
        pushParams();
    };

    // Labels
    const auto& promptFont = kBoldFont40();

    auto* dryNameLabel = new juce::Label ("", "DRY");
    dryNameLabel->setJustificationType (juce::Justification::centredLeft);
    applyLabelTextColour (*dryNameLabel, scheme.text);
    dryNameLabel->setBorderSize (juce::BorderSize<int> (0));
    dryNameLabel->setFont (promptFont);
    aw->addAndMakeVisible (dryNameLabel);

    auto* wetNameLabel = new juce::Label ("", "WET");
    wetNameLabel->setJustificationType (juce::Justification::centredLeft);
    applyLabelTextColour (*wetNameLabel, scheme.text);
    wetNameLabel->setBorderSize (juce::BorderSize<int> (0));
    wetNameLabel->setFont (promptFont);
    aw->addAndMakeVisible (wetNameLabel);

    auto* dryDbLabel = new juce::Label ("", "dB");
    dryDbLabel->setJustificationType (juce::Justification::centredLeft);
    applyLabelTextColour (*dryDbLabel, scheme.text);
    dryDbLabel->setBorderSize (juce::BorderSize<int> (0));
    dryDbLabel->setFont (promptFont);
    aw->addAndMakeVisible (dryDbLabel);

    auto* wetDbLabel = new juce::Label ("", "dB");
    wetDbLabel->setJustificationType (juce::Justification::centredLeft);
    applyLabelTextColour (*wetDbLabel, scheme.text);
    wetDbLabel->setBorderSize (juce::BorderSize<int> (0));
    wetDbLabel->setFont (promptFont);
    aw->addAndMakeVisible (wetDbLabel);

    preparePromptTextEditor (*aw, "dryLevel", scheme.bg, scheme.text, scheme.fg, promptFont, false);
    preparePromptTextEditor (*aw, "wetLevel", scheme.bg, scheme.text, scheme.fg, promptFont, false);

    auto layoutRows = [aw, dryNameLabel, wetNameLabel, dryDbLabel, wetDbLabel,
                        dryBar, wetBar, promptFont] ()
    {
        auto* dryTe = aw->getTextEditor ("dryLevel");
        auto* wetTe = aw->getTextEditor ("wetLevel");
        if (dryTe == nullptr || wetTe == nullptr) return;

        const int buttonsTop = getAlertButtonsTop (*aw);
        const int rowH       = dryTe->getHeight();
        const int barH       = juce::jmax (10, rowH / 2);
        const int barGap     = juce::jmax (2, rowH / 6);
        const int gap        = juce::jmax (4, rowH / 3);
        const int rowTotal   = rowH + barGap + barH;
        const int totalH     = rowTotal * 2 + gap;
        const int startY     = juce::jmax (kPromptEditorMinTopPx, (buttonsTop - totalH) / 2);

        const int barX = kPromptInnerMargin;
        const int barR = aw->getWidth() - kPromptInnerMargin;

        const int nameW = stringWidth (promptFont, "WET") + 6;
        const int hzGap = 2;
        const int dbW   = stringWidth (promptFont, "dB") + 2;

        auto placeRow = [&] (juce::Label* nameLabel, juce::TextEditor* te,
                             juce::Label* dbLabel, PromptBar* bar, int y)
        {
            nameLabel->setFont (promptFont);
            dbLabel->setFont (promptFont);
            nameLabel->setBounds (barX, y, nameW, rowH);

            const int midL = barX + nameW;
            const int midR = barR;
            const int midW = midR - midL;

            const auto txt = te->getText();
            const int textW = juce::jmax (1, stringWidth (promptFont, txt));
            constexpr int kEditorPad = 6;
            const int editorW = textW + kEditorPad * 2;
            const int groupW  = editorW + hzGap + dbW;
            const int groupX = midL + juce::jmax (0, (midW - groupW) / 2);

            te->setBounds (groupX, y, editorW, rowH);
            dbLabel->setBounds (groupX + editorW + hzGap, y, dbW, rowH);

            const int barW = juce::jmax (60, barR - barX);
            bar->setBounds (barX, y + rowH + barGap, barW, barH);
        };

        placeRow (dryNameLabel, dryTe, dryDbLabel, dryBar, startY);
        placeRow (wetNameLabel, wetTe, wetDbLabel, wetBar, startY + rowTotal + gap);
    };

    auto* dryTe = aw->getTextEditor ("dryLevel");
    auto* wetTe = aw->getTextEditor ("wetLevel");
    if (dryTe != nullptr)
        dryTe->onTextChange = [textToBar, dryTe, dryBar, layoutFn] () { textToBar (dryTe, dryBar); if (*layoutFn) (*layoutFn)(); };
    if (wetTe != nullptr)
        wetTe->onTextChange = [textToBar, wetTe, wetBar, layoutFn] () { textToBar (wetTe, wetBar); if (*layoutFn) (*layoutFn)(); };

    aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    applyPromptShellSize (*aw);
    layoutAlertWindowButtons (*aw);
    layoutRows();

    preparePromptTextEditor (*aw, "dryLevel", scheme.bg, scheme.text, scheme.fg, promptFont, false);
    preparePromptTextEditor (*aw, "wetLevel", scheme.bg, scheme.text, scheme.fg, promptFont, false);
    layoutRows();

    styleAlertButtons (*aw, lnf);

    const float origDry = curDry;
    const float origWet = curWet;

    fitAlertWindowToEditor (*aw, safeThis.getComponent(), [layoutRows] (juce::AlertWindow& a)
    {
        layoutAlertWindowButtons (a);
        layoutRows();
    });

    embedAlertWindowInOverlay (safeThis.getComponent(), aw);
    *layoutFn = layoutRows;

    {
        preparePromptTextEditor (*aw, "dryLevel", scheme.bg, scheme.text, scheme.fg, promptFont, false);
        preparePromptTextEditor (*aw, "wetLevel", scheme.bg, scheme.text, scheme.fg, promptFont, false);
        layoutRows();

        juce::Component::SafePointer<juce::AlertWindow> safeAw (aw);
        juce::MessageManager::callAsync ([safeAw]()
        {
            if (safeAw == nullptr) return;
            bringPromptWindowToFront (*safeAw);
            safeAw->repaint();
        });
    }

    setPromptOverlayActive (true);

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safeThis, aw, origDry, origWet] (int result)
        {
            std::unique_ptr<juce::AlertWindow> killer (aw);

            if (safeThis != nullptr)
                safeThis->setPromptOverlayActive (false);

            if (safeThis == nullptr)
                return;

            if (result != 1)
            {
                auto& p = safeThis->audioProcessor;
                auto setP = [&p] (const char* id, float plain)
                {
                    if (auto* param = p.apvts.getParameter (id))
                        param->setValueNotifyingHost (param->convertTo0to1 (plain));
                };
                setP (FREQTRAudioProcessor::kParamDryLevel, origDry);
                setP (FREQTRAudioProcessor::kParamWetLevel, origWet);
                safeThis->dualMixBar_.updateFromProcessor();
            }
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

    // Toggle bar click → expand/collapse IO section
    if (cachedToggleBarArea_.contains (pt))
    {
        ioSectionExpanded_ = ! ioSectionExpanded_;
        audioProcessor.setUiIoExpanded (ioSectionExpanded_);
        resized();
        repaint();
        return;
    }

    // Right-click on value area → numeric entry
    if (e.mods.isPopupMenu())
    {
        if (auto* slider = getSliderForValueAreaPoint (pt))
        {
            if (slider != &windowSlider)
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
            openInfoPopup();
            return;
        }
    }

    // SYNC label click → toggle (left), retrig toggle (right)
    if (syncButton.isVisible() && syncButton.isEnabled()
        && (getSyncLabelArea().contains (pt) || retrigDisplay.getBounds().contains (pt)))
    {
        if (e.mods.isPopupMenu())
            openRetrigPrompt();
        else
            syncButton.setToggleState (! syncButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    // MIDI label click → toggle (left), channel prompt (right)
    if (midiButton.isVisible() && midiButton.isEnabled()
        && (getMidiLabelArea().contains (pt) || midiChannelDisplay.getBounds().contains (pt)))
    {
        if (e.mods.isPopupMenu())
            openMidiChannelPrompt();
        else
            midiButton.setToggleState (! midiButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    // ALIGN label click → toggle
    if (alignButton.isVisible() && getAlignLabelArea().contains (pt))
    {
        alignButton.setToggleState (! alignButton.getToggleState(), juce::sendNotificationSync);
        return;
    }

    // PDC label click → toggle
    if (pdcButton.isVisible()
        && (getPdcLabelArea().contains (pt) || pdcDisplay.getBounds().contains (pt)))
    {
        if (e.mods.isPopupMenu())
            openPdcMaxWindowPrompt();
        else
            pdcButton.setToggleState (! pdcButton.getToggleState(), juce::sendNotificationSync);
        updatePdcTooltip();
        return;
    }

    if (sidechainButton.isVisible()
        && (getSidechainLabelArea().contains (pt) || sidechainDisplay.getBounds().contains (pt)))
    {
        if (e.mods.isPopupMenu())
            openSidechainPrompt();
        else
            sidechainButton.setToggleState (! sidechainButton.getToggleState(), juce::sendNotificationSync);
        updateSidechainDependentControls();
        return;
    }

    // CHSF label click → toggle (left), config (right)
    if (chaosFilterButton.isVisible())
    {
        const int boxSide = juce::jlimit (14, juce::jmax (14, cachedVLayout_.box - 2),
                                          (int) std::lround ((double) cachedVLayout_.box * 0.65));
        const int labelX = chaosFilterButton.getX() + boxSide + 4 + 2;
        const int labelR = chaosDelayButton.getX() - 6;
        auto chsFArea = juce::Rectangle<int> (labelX, chaosFilterButton.getY(),
                                               juce::jmax (0, labelR - labelX), cachedVLayout_.barH);
        if (chsFArea.contains (pt) || chaosFilterDisplay.getBounds().contains (pt))
        {
            if (e.mods.isPopupMenu())
                openChaosFilterPrompt();
            else
                chaosFilterButton.setToggleState (! chaosFilterButton.getToggleState(), juce::sendNotificationSync);
            return;
        }
    }

    // CHSD label click → toggle (left), config (right)
    if (chaosDelayButton.isVisible())
    {
        const int boxSide = juce::jlimit (14, juce::jmax (14, cachedVLayout_.box - 2),
                                          (int) std::lround ((double) cachedVLayout_.box * 0.65));
        const int labelX = chaosDelayButton.getX() + boxSide + 4 + 2;
        const int labelR = getWidth() - 6;
        auto chsDArea = juce::Rectangle<int> (labelX, chaosDelayButton.getY(),
                                               juce::jmax (0, labelR - labelX), cachedVLayout_.barH);
        if (chsDArea.contains (pt) || chaosDelayDisplay.getBounds().contains (pt))
        {
            if (e.mods.isPopupMenu())
                openChaosDelayPrompt();
            else
                chaosDelayButton.setToggleState (! chaosDelayButton.getToggleState(), juce::sendNotificationSync);
            return;
        }
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
    juce::ignoreUnused (e);
    lastUserInteractionMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
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
    constexpr int kNumBars = 14;

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
FREQTRAudioProcessorEditor::buildVerticalLayout (int editorH, int biasY, bool ioExpanded)
{
    VerticalLayoutMetrics m;
    m.rhythm = juce::jlimit (6, 16, (int) std::round (editorH * 0.018));
    const int nominalBarH = juce::jlimit (14, 120, m.rhythm * 6);
    const int nominalGapY = juce::jmax (4, m.rhythm * 4);

    constexpr int kCompactTitleMaxHeightPx = 48;
    constexpr int kCompactToggleTopY = 85;

    m.titleH = juce::jlimit (24, kCompactTitleMaxHeightPx, m.rhythm * 4);
    m.titleAreaH = m.titleH + 4;
    const int computedTitleTopPad = 6 + biasY;
    m.titleTopPad = (computedTitleTopPad > 8) ? computedTitleTopPad : 8;
    const int titleGap = m.titleTopPad;
    m.topMargin = juce::jmax (kCompactToggleTopY, m.titleTopPad + m.titleAreaH + titleGap);
    m.betweenSlidersAndButtons = juce::jmax (8, m.rhythm * 2);
    m.bottomMargin = m.titleTopPad;

    m.box = juce::jlimit (40, kToggleBoxPx, (int) std::round (editorH * 0.085));
    m.btnRowGap = juce::jlimit (4, 14, (int) std::round (editorH * 0.008));
    m.btnRow3Y = editorH - m.bottomMargin - m.box;
    m.btnRow2Y = m.btnRow3Y - m.btnRowGap - m.box;
    m.btnRow1Y = m.btnRow2Y - m.btnRowGap - m.box;

    // Expanded: chaos sits above SIDECHAIN. Collapsed: two utility rows sit at the bottom.
    m.chaosRowY = ioExpanded ? m.btnRow2Y : 0;

    const int sliderBottomRef = ioExpanded ? m.chaosRowY : m.btnRow2Y;
    m.availableForSliders = juce::jmax (40, sliderBottomRef - m.betweenSlidersAndButtons - m.topMargin);

    // Bars below toggle: 8 IO rows when expanded (IN, OUT, TILT, FILTER, PAN, MIX, LIM + MODE_ROW), 10 main bars when collapsed.
    // Toggle bar stays fixed — only bar/gap sizing adapts to the visible count.
    const int numSliders = 10;
    const int numGaps    = 10;  // (N-1) inter-slider + 1 toggle-to-first

    m.toggleBarH = 20;  // fixed visual height for click area
    const int spaceForScale = juce::jmax (40, m.availableForSliders - m.toggleBarH);

    const int nominalStack = numSliders * nominalBarH + numGaps * nominalGapY;
    const double stackScale = nominalStack > 0 ? juce::jmin (1.0, (double) spaceForScale / (double) nominalStack)
                                               : 1.0;

    m.barH = juce::jmax (14, (int) std::round (nominalBarH * stackScale));
    m.gapY = juce::jmax (4,  (int) std::round (nominalGapY * stackScale));

    auto stackHeight = [&]() { return numSliders * m.barH + numGaps * m.gapY; };

    while (stackHeight() > spaceForScale && m.gapY > 4)
        --m.gapY;

    while (stackHeight() > spaceForScale && m.barH > 14)
        --m.barH;

    m.topY = m.topMargin;

    m.toggleBarY = m.topY;  // toggle bar always at top

    return m;
}

void FREQTRAudioProcessorEditor::updateCachedLayout()
{
    cachedHLayout_ = buildHorizontalLayout (getWidth(), getTargetValueColumnWidth());
    cachedVLayout_ = buildVerticalLayout (getHeight(), kLayoutVerticalBiasPx, ioSectionExpanded_);

    const juce::Slider* sliders[kNumBars] = { &inputSlider, &outputSlider, &mixSlider,
                                               &freqSlider, &modSlider, &combSlider, &feedbackSlider, &engineSlider,
        &windowSlider, &harmSlider, &polaritySlider, &jitterSlider, &styleSlider, &tiltSlider };

    for (int i = 0; i < kNumBars; ++i)
    {
        if (! sliders[i]->isVisible())
        {
            // MIX row (index 2): use dualMixBar_ bounds when SEND mode is active
            if (i == 2 && dualMixBar_.isVisible())
            {
                const auto& bb = dualMixBar_.getBounds();
                const int valueX = bb.getRight() + cachedHLayout_.valuePad;
                const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
                const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
                const int y    = bb.getCentreY() - (kValueAreaHeightPx / 2);
                cachedValueAreas_[2] = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
                continue;
            }
            cachedValueAreas_[(size_t) i] = {};
            continue;
        }

        const auto& bb = sliders[i]->getBounds();
        const int valueX = bb.getRight() + cachedHLayout_.valuePad;
        const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
        const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
        const int y    = bb.getCentreY() - (kValueAreaHeightPx / 2);
        cachedValueAreas_[(size_t) i] = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
    }

    // Filter bar value area
    if (filterBar_.isVisible())
    {
        const auto& fb = filterBar_.getBounds();
        const int valueX = fb.getRight() + cachedHLayout_.valuePad;
        const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
        const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
        const int y    = fb.getCentreY() - (kValueAreaHeightPx / 2);
        cachedFilterValueArea_ = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
    }
    else
    {
        cachedFilterValueArea_ = {};
    }

    // Pan slider value area
    if (panSlider.isVisible())
    {
        const auto& pb = panSlider.getBounds();
        const int valueX = pb.getRight() + cachedHLayout_.valuePad;
        const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
        const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
        const int y    = pb.getCentreY() - (kValueAreaHeightPx / 2);
        cachedPanValueArea_ = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
    }
    else
    {
        cachedPanValueArea_ = {};
    }

    // Lim threshold slider value area
    if (limThresholdSlider.isVisible())
    {
        const auto& lb = limThresholdSlider.getBounds();
        const int valueX = lb.getRight() + cachedHLayout_.valuePad;
        const int maxW = juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx);
        const int vw   = juce::jmin (cachedHLayout_.valueW, maxW);
        const int y    = lb.getCentreY() - (kValueAreaHeightPx / 2);
        cachedLimThresholdValueArea_ = { valueX, y, juce::jmax (0, vw), kValueAreaHeightPx };
    }
    else
    {
        cachedLimThresholdValueArea_ = {};
    }

    // Cache toggle bar area
    cachedToggleBarArea_ = { cachedHLayout_.leftX, cachedVLayout_.toggleBarY,
                             cachedHLayout_.contentW, cachedVLayout_.toggleBarH };
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
        "-100% FBK",
        "100% JITTER",
        "FREQ SHIFT ENGINE",
        "100% AM|RM ENGINE",
        "100% RM|FS ENGINE",
        "2048 WINDOW",
        "STEREO STYLE", "DUAL STYLE",
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
    const int valueX = barBounds.getRight() + cachedHLayout_.valuePad;
    const int valueW = juce::jmin (cachedHLayout_.valueW, juce::jmax (0, getWidth() - valueX - kValueAreaRightMarginPx));
    const int y = barBounds.getCentreY() - (kValueAreaHeightPx / 2);
    return { valueX, y, juce::jmax (0, valueW), kValueAreaHeightPx };
}

juce::Slider* FREQTRAudioProcessorEditor::getSliderForValueAreaPoint (juce::Point<int> p)
{
    juce::Slider* sliders[kNumBars] = { &inputSlider, &outputSlider, &mixSlider,
                                         &freqSlider, &modSlider, &combSlider, &feedbackSlider, &engineSlider,
        &windowSlider, &harmSlider, &polaritySlider, &jitterSlider, &styleSlider, &tiltSlider };

    for (int i = 0; i < kNumBars; ++i)
        if (cachedValueAreas_[(size_t) i].contains (p))
            return (sliders[i]->isVisible() && sliders[i]->isEnabled()) ? sliders[i] : nullptr;

    if (engineSlider.isVisible() && engineSlider.isEnabled()
        && engineSlider.getBounds().getUnion (getValueAreaFor (engineSlider.getBounds())).contains (p))
        return &engineSlider;

    return nullptr;
}

//========================== Label areas ==========================

juce::Rectangle<int> FREQTRAudioProcessorEditor::getSyncLabelArea() const
{
    return makeToggleLabelArea (syncButton, midiButton.getX() - kToggleLegendCollisionPadPx, "SYNC", "SYN");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getMidiLabelArea() const
{
    return makeToggleLabelArea (midiButton, getWidth() - kToggleLegendCollisionPadPx, "MIDI", "MIDI");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getAlignLabelArea() const
{
    return makeToggleLabelArea (alignButton, pdcButton.getX() - kToggleLegendCollisionPadPx, "ALIGN", "ALN");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getPdcLabelArea() const
{
    return makeToggleLabelArea (pdcButton, getWidth() - kToggleLegendCollisionPadPx, "PDC", "PD");
}

juce::Rectangle<int> FREQTRAudioProcessorEditor::getSidechainLabelArea() const
{
    return makeToggleLabelArea (sidechainButton, getWidth() - kToggleLegendCollisionPadPx, "SIDECHAIN", "SC");
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
                                  const juce::String& intOnlyLegend,
                                  bool enabled = true)
    {
        g.setColour (scheme.text.withMultipliedAlpha (enabled ? 1.0f : 0.35f));

        if (tryDrawLegend (area, fullLegend, fullShrinkFloor))
        {
            g.setColour (scheme.text);
            return;
        }

        if (tryDrawLegend (area, shortLegend, minFontPx))
        {
            g.setColour (scheme.text);
            return;
        }

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

        const auto titleArea = juce::Rectangle<int> (titleX, titleY, titleW, titleH + kTitleAreaExtraHeightPx);
        const juce::String titleText ("FREQ-TR");
        const auto infoIconArea = getInfoIconArea();
        const juce::String versionText = juce::String ("v") + InfoContent::version;

        auto titleFont = g.getCurrentFont();
        titleFont.setHeight ((float) titleH);

        auto versionFont = juce::Font (juce::FontOptions (juce::jmax (10.0f, (float) titleH * UiMetrics::versionFontRatio)).withStyle ("Bold"));
        const int versionH = juce::jlimit (10, infoIconArea.getHeight(), (int) std::round ((double) infoIconArea.getHeight() * UiMetrics::versionHeightRatio));
        const int versionY = infoIconArea.getBottom() - versionH;
        const int versionRight = infoIconArea.getX() - kVersionGapPx;
        const int measuredVersionW = stringWidth (versionFont, versionText) + 2;
        const int maxVersionW = juce::jmax (0, versionRight - titleArea.getX());
        const int versionW = juce::jmin (maxVersionW, juce::jmax (28, measuredVersionW));
        const int versionX = juce::jmax (titleArea.getX(), versionRight - versionW);

        // Fit against the version label, not just the gear icon, so the title cannot collide with v1.4.
        const int titleRightLimit = versionX - kTitleRightGapToInfoPx;
        const int titleMaxW = juce::jmax (0, titleRightLimit - titleArea.getX());
        const int titleBaseW = stringWidth (titleFont, titleText);
        const int originalTitleLimitW = juce::jmax (0, juce::jmin (titleW, horizontalLayout.barW));
        const bool originalWouldClipTitle = titleBaseW > originalTitleLimitW;

        auto fittedTitleFont = titleFont;
        if (titleMaxW > 0 && (originalWouldClipTitle || titleBaseW > titleMaxW))
        {
            fittedTitleFont.setHorizontalScale (1.0f);
            const float titleMinScale = juce::jlimit (0.4f, 1.0f, 12.0f / (float) titleH);
            for (float s = 1.0f; s >= titleMinScale; s -= 0.025f)
            {
                fittedTitleFont.setHorizontalScale (s);
                if (stringWidth (fittedTitleFont, titleText) <= titleMaxW)
                    break;
            }
        }

        g.setColour (scheme.text);
        g.setFont (fittedTitleFont);
        g.drawText (titleText, titleArea.getX(), titleArea.getY(),
                    titleMaxW > 0 ? juce::jmin (titleArea.getWidth(), titleMaxW) : titleArea.getWidth(),
                    titleArea.getHeight(), juce::Justification::left, false);

        g.setFont (versionFont);

        if (versionW > 0)
            g.drawText (versionText,
                        versionX, versionY, versionW, versionH,
                        juce::Justification::bottomRight, false);

        g.setFont (kBoldFont40());
    }

    // ── Bar legends ──
    {
        const juce::String* fullTexts[kNumBars]  = { &cachedInputTextFull, &cachedOutputTextFull, &cachedMixTextFull,
                                                      &cachedFreqTextFull, &cachedModTextFull, &cachedCombTextFull, &cachedFeedbackTextFull, &cachedEngineTextFull,
        &cachedWindowTextFull, &cachedHarmTextFull, &cachedPolarityTextFull, &cachedJitterTextFull, &cachedStyleTextFull, &cachedTiltTextFull };
        const juce::String* shortTexts[kNumBars] = { &cachedInputTextShort, &cachedOutputTextShort, &cachedMixTextShort,
                                                      &cachedFreqTextShort, &cachedModTextShort, &cachedCombTextShort, &cachedFeedbackTextShort, &cachedEngineTextShort,
        &cachedWindowTextShort, &cachedHarmTextShort, &cachedPolarityTextShort, &cachedJitterTextShort, &cachedStyleTextShort, &cachedTiltTextShort };
        const juce::String* intTexts[kNumBars] = {
            &cachedInputIntOnly,
            &cachedOutputIntOnly,
            &cachedMixIntOnly,
            &cachedFreqIntOnly,
            &cachedModIntOnly,
            &cachedCombIntOnly,
            &cachedFeedbackIntOnly,
            &cachedEngineIntOnly,
            &cachedWindowIntOnly,
            &cachedHarmIntOnly,
            &cachedPolarityIntOnly,
            &cachedJitterIntOnly,
            &cachedStyleIntOnly,
            &cachedTiltIntOnly
        };
        const juce::Slider* sliders[kNumBars] = {
            &inputSlider, &outputSlider, &mixSlider,
            &freqSlider, &modSlider, &combSlider, &feedbackSlider, &engineSlider,
            &windowSlider, &harmSlider, &polaritySlider, &jitterSlider, &styleSlider, &tiltSlider
        };

        for (int i = 0; i < kNumBars; ++i)
            drawLegendForMode (cachedValueAreas_[(size_t) i], *fullTexts[i], *shortTexts[i], *intTexts[i],
                               sliders[i]->isEnabled());

        // Filter bar legend
        if (! cachedFilterValueArea_.isEmpty())
            drawLegendForMode (cachedFilterValueArea_, cachedFilterTextFull, cachedFilterTextShort, cachedFilterTextShort);

        // Pan slider legend
        if (! cachedPanValueArea_.isEmpty())
            drawLegendForMode (cachedPanValueArea_, cachedPanTextFull, cachedPanTextShort, cachedPanTextShort);

        // Lim threshold legend
        if (limThresholdSlider.isVisible() && cachedLimThresholdValueArea_.getWidth() > 0)
            drawLegendForMode (cachedLimThresholdValueArea_, cachedLimThresholdTextFull, cachedLimThresholdTextShort, cachedLimThresholdIntOnly);
    }

    // ── Toggle bar (IN/OUT/MIX collapsible) ──
    {
        if (! cachedToggleBarArea_.isEmpty())
        {
            const float barRadius = (float) cachedToggleBarArea_.getHeight() * 0.3f;
            g.setColour (scheme.fg.withAlpha (0.25f));
            g.fillRoundedRectangle (cachedToggleBarArea_.toFloat(), barRadius);

            // Triangle indicator — larger + white for UX clarity
            const float triH = (float) cachedToggleBarArea_.getHeight() * 0.8f;
            const float triW = triH * 1.125f;
            const float cx = (float) cachedToggleBarArea_.getCentreX();
            const float cy = (float) cachedToggleBarArea_.getCentreY();

            juce::Path tri;
            if (ioSectionExpanded_)
            {
                // ▲ pointing up (collapse)
                tri.addTriangle (cx - triW * 0.5f, cy + triH * 0.35f,
                                 cx + triW * 0.5f, cy + triH * 0.35f,
                                 cx,               cy - triH * 0.35f);
            }
            else
            {
                // ▼ pointing down (expand)
                tri.addTriangle (cx - triW * 0.5f, cy - triH * 0.35f,
                                 cx + triW * 0.5f, cy - triH * 0.35f,
                                 cx,               cy + triH * 0.35f);
            }
            g.setColour (scheme.text);
            g.fillPath (tri);
        }
    }

    // ── Button labels ──
    {
        g.setColour (scheme.text);
        const auto& labelFont = kBoldFont40();
        g.setFont (labelFont);

        const int alignCR = pdcButton.getX() - kToggleLegendCollisionPadPx;
        const int pdcCR   = W - kToggleLegendCollisionPadPx;
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
                g.setColour (scheme.text.withMultipliedAlpha (btn.isEnabled() ? 1.0f : 0.35f));
                drawIfFitsWithOptionalShrink (g, area, text, baseFontPx, minFontPx);
                g.setColour (scheme.text);
            }
        };

        if (alignButton.isVisible())
        {
            drawToggleLabel (alignButton, chooseToggleLabel (alignButton, alignCR, "ALIGN", "ALN"), alignCR);
            drawToggleLabel (pdcButton,   "PDC",   pdcCR);
            drawToggleLabel (syncButton,  "SYNC",  syncCR);
            drawToggleLabel (midiButton,  "MIDI",  midiCR);
        }

        if (sidechainButton.isVisible())
            drawToggleLabel (sidechainButton, "SIDECHAIN", W - kToggleLegendCollisionPadPx);

        // Mode In / Mode Out / Sum Bus / Limiter Mode labels above combos
        if (modeInCombo.isVisible())
        {
            const auto font = juce::Font (juce::FontOptions (17.0f).withStyle ("Bold"));
            g.setFont (font);
            auto drawComboLabel = [&] (const juce::ComboBox& combo, const juce::String& full, const juce::String& shortTxt)
            {
                const auto area = combo.getBounds().withHeight (20).translated (0, -21);
                const float comboW = (float) combo.getWidth();
                juce::GlyphArrangement ga;
                ga.addLineOfText (font, full, 0.0f, 0.0f);
                const bool useShort = ga.getBoundingBox (0, -1, false).getWidth() > comboW;
                g.drawText (useShort ? shortTxt : full, area, juce::Justification::centred);
            };
            g.setColour (activeScheme.text);
            drawComboLabel (modeInCombo,  "MODE IN",  "IN");
            drawComboLabel (modeOutCombo, "MODE OUT", "OUT");
            drawComboLabel (sumBusCombo,  "SUM BUS",  "SUM");
            drawComboLabel (limModeCombo, "LIMIT",    "LIM");
            drawComboLabel (mixModeCombo,   "MIX",    "MIX");
            drawComboLabel (filterPosCombo, "F / T", "F/T");
            drawComboLabel (invPolCombo,    "INV POL", "POL");
            drawComboLabel (invStrCombo,    "INV STR", "STR");
        }

        if (chaosFilterButton.isVisible())
        {
            const int chsFCR = chaosDelayButton.isVisible()
                             ? chaosDelayButton.getX() - kToggleLegendCollisionPadPx
                             : W - kToggleLegendCollisionPadPx;
            const int chsDCR = W - kToggleLegendCollisionPadPx;
            drawToggleLabel (chaosFilterButton, "CHSF", chsFCR);
            drawToggleLabel (chaosDelayButton,  "CHSD", chsDCR);
        }
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
    const auto verticalLayout = buildVerticalLayout (H, kLayoutVerticalBiasPx, ioSectionExpanded_);

    // Position sliders — toggle bar always at top, swaps between main and IO bars
    const int step = verticalLayout.barH + verticalLayout.gapY;
    const int mainTop = verticalLayout.toggleBarY + verticalLayout.toggleBarH + verticalLayout.gapY;

    if (ioSectionExpanded_)
    {
        // Expanded: [toggle bar] → INPUT, OUTPUT, TILT, FILTER, PAN, MIX; chaos buttons; main params hidden
        inputSlider.setVisible (true);
        outputSlider.setVisible (true);
        tiltSlider.setVisible (true);
        filterBar_.setVisible (true);
        panSlider.setVisible (true);
        mixSlider.setVisible (true);
        limThresholdSlider.setVisible (true);

        inputSlider.setBounds   (horizontalLayout.leftX, mainTop + 0 * step, horizontalLayout.barW, verticalLayout.barH);
        outputSlider.setBounds  (horizontalLayout.leftX, mainTop + 1 * step, horizontalLayout.barW, verticalLayout.barH);
        tiltSlider.setBounds    (horizontalLayout.leftX, mainTop + 2 * step, horizontalLayout.barW, verticalLayout.barH);
        filterBar_.setBounds    (horizontalLayout.leftX, mainTop + 3 * step, horizontalLayout.barW, verticalLayout.barH);
        panSlider.setBounds     (horizontalLayout.leftX, mainTop + 4 * step, horizontalLayout.barW, verticalLayout.barH);
        mixSlider.setBounds     (horizontalLayout.leftX, mainTop + 5 * step, horizontalLayout.barW, verticalLayout.barH);
        limThresholdSlider.setBounds (horizontalLayout.leftX, mainTop + 6 * step, horizontalLayout.barW, verticalLayout.barH);

        // Mode In / Mode Out / Sum Bus / Limiter Mode — 4 combos on row 7
        {
            const int comboGapX = 4;
            const int comboGapY = 10;
            const int totalW = horizontalLayout.barW + horizontalLayout.valuePad + horizontalLayout.valueW;
            const int comboW = (totalW - comboGapX * 3) / 4;
            const int comboH = juce::jlimit (38, 48, verticalLayout.barH + 14);
            const int labelOffset = 19;
            const int comboBlockH = labelOffset + comboH + comboGapY + labelOffset + comboH;
            const int blockTopLimit = limThresholdSlider.getBottom() + verticalLayout.gapY;
            const int blockBottomLimit = verticalLayout.chaosRowY - verticalLayout.gapY;
            const int availableBlockH = juce::jmax (comboBlockH, blockBottomLimit - blockTopLimit);
            const int visualTop = blockTopLimit + juce::jmax (0, (availableBlockH - comboBlockH) / 2);
            const int modeY = visualTop + labelOffset;
            modeInCombo.setBounds  (horizontalLayout.leftX,                           modeY, comboW, comboH);
            modeOutCombo.setBounds (horizontalLayout.leftX + (comboW + comboGapX),      modeY, comboW, comboH);
            sumBusCombo.setBounds  (horizontalLayout.leftX + (comboW + comboGapX) * 2,  modeY, comboW, comboH);
            limModeCombo.setBounds (horizontalLayout.leftX + (comboW + comboGapX) * 3,  modeY, comboW, comboH);

            const int invY = modeY + comboH + comboGapY + labelOffset;
            const int comboW2 = (totalW - comboGapX * 3) / 4;
            mixModeCombo.setBounds   (horizontalLayout.leftX,                             invY, comboW2, comboH);
            filterPosCombo.setBounds (horizontalLayout.leftX + (comboW2 + comboGapX),       invY, comboW2, comboH);
            invPolCombo.setBounds    (horizontalLayout.leftX + (comboW2 + comboGapX) * 2,   invY, comboW2, comboH);
            invStrCombo.setBounds    (horizontalLayout.leftX + (comboW2 + comboGapX) * 3,   invY, comboW2, comboH);
        }

        // DualMixBar at same position as mixSlider
        dualMixBar_.setBounds (horizontalLayout.leftX, mainTop + 5 * step, horizontalLayout.barW, verticalLayout.barH);

        // Chaos buttons at chaosRowY
        const int chaosY = verticalLayout.chaosRowY;
        const int chaosH = verticalLayout.box;
        const int chaosRightX = horizontalLayout.leftX + horizontalLayout.barW + horizontalLayout.valuePad;
        const int chaosLeftW  = chaosRightX - horizontalLayout.leftX;
        const int chaosRightW = horizontalLayout.leftX + horizontalLayout.contentW - chaosRightX;
        chaosFilterButton.setBounds  (horizontalLayout.leftX, chaosY, chaosLeftW,  chaosH);
        chaosFilterDisplay.setBounds (horizontalLayout.leftX, chaosY, chaosLeftW,  chaosH);
        chaosDelayButton.setBounds   (chaosRightX,            chaosY, chaosRightW, chaosH);
        chaosDelayDisplay.setBounds  (chaosRightX,            chaosY, chaosRightW, chaosH);

        modeInCombo.setVisible (true);
        modeOutCombo.setVisible (true);
        sumBusCombo.setVisible (true);
        limModeCombo.setVisible (true);
        invPolCombo.setVisible (true);
        invStrCombo.setVisible (true);
        mixModeCombo.setVisible (true);
        filterPosCombo.setVisible (true);
        {
            const bool isSendMode = mixModeCombo.getSelectedId() == 2;
            mixSlider.setVisible (! isSendMode);
            dualMixBar_.setVisible (isSendMode);
        }
        chaosFilterButton.setVisible (true);
        chaosFilterDisplay.setVisible (true);
        chaosDelayButton.setVisible (true);
        chaosDelayDisplay.setVisible (true);

        syncButton.setVisible (false);
        midiButton.setVisible (false);
        midiChannelDisplay.setVisible (false);
        alignButton.setVisible (false);
        pdcButton.setVisible (false);
        pdcDisplay.setVisible (false);
        retrigDisplay.setVisible (false);
        sidechainButton.setVisible (true);
        sidechainDisplay.setVisible (true);

        freqSlider.setBounds (0, 0, 0, 0);
        modSlider.setBounds (0, 0, 0, 0);
        feedbackSlider.setBounds (0, 0, 0, 0);
        jitterSlider.setBounds (0, 0, 0, 0);
        combSlider.setBounds (0, 0, 0, 0);
        engineSlider.setBounds (0, 0, 0, 0);
        windowSlider.setBounds (0, 0, 0, 0);
        harmSlider.setBounds (0, 0, 0, 0);
        polaritySlider.setBounds (0, 0, 0, 0);
        styleSlider.setBounds (0, 0, 0, 0);

        freqSlider.setVisible (false);
        modSlider.setVisible (false);
        feedbackSlider.setVisible (false);
        jitterSlider.setVisible (false);
        combSlider.setVisible (false);
        engineSlider.setVisible (false);
        windowSlider.setVisible (false);
        harmSlider.setVisible (false);
        polaritySlider.setVisible (false);
        styleSlider.setVisible (false);
    }
    else
    {
        // Collapsed: [toggle bar] → main params; IO + filter + pan + tilt + chaos hidden
        inputSlider.setVisible (false);
        outputSlider.setVisible (false);
        tiltSlider.setVisible (false);
        filterBar_.setVisible (false);
        panSlider.setVisible (false);
        mixSlider.setVisible (false);
        limThresholdSlider.setVisible (false);
        chaosFilterButton.setVisible (false);
        chaosFilterDisplay.setVisible (false);
        chaosDelayButton.setVisible (false);
        chaosDelayDisplay.setVisible (false);
        modeInCombo.setVisible (false);
        modeOutCombo.setVisible (false);
        sumBusCombo.setVisible (false);
        limModeCombo.setVisible (false);
        invPolCombo.setVisible (false);
        invStrCombo.setVisible (false);
        mixModeCombo.setVisible (false);
        filterPosCombo.setVisible (false);
        dualMixBar_.setVisible (false);
        inputSlider.setBounds (0, 0, 0, 0);
        outputSlider.setBounds (0, 0, 0, 0);
        tiltSlider.setBounds (0, 0, 0, 0);
        filterBar_.setBounds (0, 0, 0, 0);
        panSlider.setBounds (0, 0, 0, 0);
        mixSlider.setBounds (0, 0, 0, 0);
        dualMixBar_.setBounds (0, 0, 0, 0);
        limThresholdSlider.setBounds (0, 0, 0, 0);

        syncButton.setVisible (true);
        midiButton.setVisible (true);
        midiChannelDisplay.setVisible (true);
        alignButton.setVisible (true);
        pdcButton.setVisible (true);
        pdcDisplay.setVisible (true);
        retrigDisplay.setVisible (true);
        sidechainButton.setVisible (false);
        sidechainDisplay.setVisible (false);

        freqSlider.setVisible (true);
        modSlider.setVisible (true);
        feedbackSlider.setVisible (true);
        jitterSlider.setVisible (true);
        combSlider.setVisible (true);
        engineSlider.setVisible (true);
        windowSlider.setVisible (true);
        harmSlider.setVisible (true);
        polaritySlider.setVisible (true);
        styleSlider.setVisible (true);

        freqSlider.setBounds     (horizontalLayout.leftX, mainTop + 0 * step, horizontalLayout.barW, verticalLayout.barH);
        modSlider.setBounds      (horizontalLayout.leftX, mainTop + 1 * step, horizontalLayout.barW, verticalLayout.barH);
        combSlider.setBounds     (horizontalLayout.leftX, mainTop + 2 * step, horizontalLayout.barW, verticalLayout.barH);
        feedbackSlider.setBounds (horizontalLayout.leftX, mainTop + 3 * step, horizontalLayout.barW, verticalLayout.barH);
        engineSlider.setBounds   (horizontalLayout.leftX, mainTop + 4 * step, horizontalLayout.barW, verticalLayout.barH);
        windowSlider.setBounds   (horizontalLayout.leftX, mainTop + 5 * step, horizontalLayout.barW, verticalLayout.barH);
        harmSlider.setBounds     (horizontalLayout.leftX, mainTop + 6 * step, horizontalLayout.barW, verticalLayout.barH);
        polaritySlider.setBounds (horizontalLayout.leftX, mainTop + 7 * step, horizontalLayout.barW, verticalLayout.barH);
        jitterSlider.setBounds   (horizontalLayout.leftX, mainTop + 8 * step, horizontalLayout.barW, verticalLayout.barH);
        styleSlider.setBounds    (horizontalLayout.leftX, mainTop + 9 * step, horizontalLayout.barW, verticalLayout.barH);
    }

    // Button area: 2 rows — row1: ALIGN+PDC, row2: SYNC+MIDI
    const int buttonAreaX = horizontalLayout.leftX;

    const int toggleVisualSide = juce::jlimit (14,
                                               juce::jmax (14, verticalLayout.box - 2),
                                               (int) std::lround ((double) verticalLayout.box * 0.65));
    const int toggleHitW = toggleVisualSide + 6;

    const int leftBlockX = buttonAreaX;
    const int rightBlockX = horizontalLayout.leftX + horizontalLayout.barW + horizontalLayout.valuePad;

    const int utilityRow1Y = ioSectionExpanded_ ? verticalLayout.btnRow1Y : verticalLayout.btnRow2Y;
    const int utilityRow2Y = ioSectionExpanded_ ? verticalLayout.btnRow2Y : verticalLayout.btnRow3Y;
    alignButton.setBounds (leftBlockX,  utilityRow1Y, toggleHitW, verticalLayout.box);
    pdcButton.setBounds   (rightBlockX, utilityRow1Y, toggleHitW, verticalLayout.box);
    syncButton.setBounds  (leftBlockX,  utilityRow2Y, toggleHitW, verticalLayout.box);
    midiButton.setBounds  (rightBlockX, utilityRow2Y, toggleHitW, verticalLayout.box);
    sidechainButton.setBounds (leftBlockX, verticalLayout.btnRow3Y, toggleHitW, verticalLayout.box);

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

    pdcDisplay.setBounds (pdcButton.getBounds().getUnion (getPdcLabelArea()));
    sidechainDisplay.setBounds (sidechainButton.getBounds().getUnion (getSidechainLabelArea()));
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
