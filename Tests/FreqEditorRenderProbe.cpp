#include "../Source/PluginProcessor.h"
#include "../Source/Modulation/FreqModulationConfig.h"
#include "../../TR-Shared/SimpleUIV2/Components/SimpleDescriptorControl.h"
#include "../../TR-Shared/SimpleUIV2/Components/SimpleDetailPanel.h"
#include "../../TR-Shared/SimpleUIV2/Components/SimpleChoiceSelector.h"
#include "../../TR-Shared/SimpleUIV2/Components/SimpleRailValueControl.h"
#include "../../TR-Shared/SimpleUIV2/Style/SimpleV2LookAndFeel.h"
#include "../../TR-Shared/Modulation/UI/TRModulationWorkspace.h"

#include <iostream>
#include <cmath>
#include <stdexcept>

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace
{
void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

void verifySharedMotionRecipeContract()
{
    bool recipeInstalled = false;
    juce::String installedId;
    int installedMacro = 0;
    TR::Modulation::UI::WorkspaceCallbacks callbacks;
    callbacks.recipeRequested = [&](const juce::String& id, int macroOneBased)
    {
        recipeInstalled = true;
        installedId = id;
        installedMacro = macroOneBased;
        return true;
    };
    TR::Modulation::UI::ModulationWorkspace workspace(std::move(callbacks));
    workspace.setBounds(0, 0, 1040, 680);
    workspace.setDestinationOptions({ { "core:frequency", "CORE", "FREQUENCY" } });
    workspace.setMotionRecipeOptions({ { "native-jitter", "NATIVE JITTER" } });
    workspace.focusMacro(2);
    workspace.openMotionRecipeSelector();
    workspace.setSelectorSearchText("NATIVE JITTER");
    require(workspace.visibleSelectorChoiceCount() == 1
                && workspace.chooseVisibleSelectorItem(0)
                && recipeInstalled && installedId == "native-jitter" && installedMacro == 3,
            "Shared Motion recipe selector did not preserve recipe/Macro identity");

    const auto state = TR::FreqModulation::makeJitterParityRecipe(
        TR::Modulation::makeDefaultState(), 3);
    require(workspace.setState(state), "Shared Motion source test rejected a valid recipe");
    workspace.openSourceSelector();
    workspace.setSelectorSearchText("MOTION 2 / ADAPTIVE CHAOS");
    require(workspace.visibleSelectorChoiceCount() == 1
                && workspace.chooseVisibleSelectorItem(0)
                && workspace.state().routes.front().source == TR::Modulation::SourceId::motion(2),
            "Shared Motion source label/selection contract failed");
}

bool containsColour(const juce::Image& image, juce::Colour colour, int sideInset = 0)
{
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = sideInset; x < image.getWidth() - sideInset; ++x)
            if (image.getPixelAt(x, y).getARGB() == colour.getARGB()) return true;
    return false;
}

juce::Component* findById(juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren())
        if (auto* result = findById(*child, id)) return result;
    return nullptr;
}

template <typename ComponentType>
ComponentType* findDirectChild(juce::Component& parent)
{
    for (auto* child : parent.getChildren())
        if (auto* result = dynamic_cast<ComponentType*>(child)) return result;
    return nullptr;
}

juce::Button* findButtonByText(juce::Component& parent, const juce::String& text)
{
    if (auto* button = dynamic_cast<juce::Button*>(&parent))
        if (button->getButtonText() == text) return button;
    for (auto* child : parent.getChildren())
        if (auto* result = findButtonByText(*child, text)) return result;
    return nullptr;
}

void dispatchPendingMessages()
{
#if JUCE_WINDOWS
    const auto deadline = juce::Time::getMillisecondCounter() + 80;
    MSG message {};
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        bool dispatched = false;
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
            dispatched = true;
        }
        if (!dispatched) juce::Thread::sleep(1);
    }
#endif
}

void writePng(const juce::Image& image, const juce::File& output)
{
    output.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(output.createOutputStream());
    if (stream == nullptr || !juce::PNGImageFormat().writeImageToStream(image, *stream))
        throw std::runtime_error("Could not write FREQ editor snapshot");
}

void configureCarrierRoute(FREQTRAudioProcessor& processor)
{
    auto state = TR::Modulation::makeDefaultState();
    auto& source = state.analysisSources[4];
    source.feature = TR::Modulation::AnalysisFeature::rmsEnvelope;
    source.detector.response = TR::Modulation::DetectorResponse::carrierTracking;
    source.detector.highPassEnabled = true;
    source.detector.highPassHz = 80.0f;
    source.detector.highPassSlope = 12;
    source.detector.lowPassEnabled = true;
    source.detector.lowPassHz = 12000.0f;
    source.detector.lowPassSlope = 12;
    require(TR::Modulation::appendRoute(state, { 0, 0, true,
                TR::Modulation::SourceId::sidechainAnalysis(4),
                TR::Modulation::Polarity::unipolar, 1.0f, "signal:carrier",
                TR::Modulation::SourceId::none(), TR::Modulation::Polarity::unipolar,
                TR::Modulation::makeLinearCurve(), TR::Modulation::makeLinearCurve() })
                && processor.setModulationState(state),
            "Could not configure SIDECHAIN/DIRECT -> SIGNAL/DIRECT");
    require(processor.routedCarrierConfigured(), "Carrier route was not published");
}

void verifyCarrierJourney(const juce::File& output)
{
    auto processor = std::make_unique<FREQTRAudioProcessor>();
    configureCarrierRoute(*processor);
    processor->prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    dispatchPendingMessages();

    auto* harm = findById(*editor, "harm-control");
    auto* shadow = findById(*editor, "shadow-control");
    require(harm != nullptr && shadow != nullptr && !harm->isShowing() && shadow->isShowing(),
            "Configured carrier route does not expose SHADOW in the real editor");
    writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
             output.getChildFile("01-freq-main-shadow.png"));

    auto* matrix = dynamic_cast<juce::Button*>(findById(*editor, "matrix-workspace-button"));
    require(matrix != nullptr, "MATRIX button is missing");
    matrix->triggerClick();
    dispatchPendingMessages();
    auto* workspace = findById(*editor, "auxiliary-workspace");
    require(workspace != nullptr && workspace->isShowing(), "MATRIX did not open");
    auto* matrixSource = dynamic_cast<juce::Button*>(
        findById(*editor, "matrix-inspector-source"));
    auto* matrixDestination = dynamic_cast<juce::Button*>(
        findById(*editor, "matrix-inspector-destination"));
    require(matrixSource != nullptr && matrixDestination != nullptr,
            "MATRIX source/destination inspectors are missing");
    std::cout << "FREQ MATRIX route: " << matrixSource->getButtonText()
              << " -> " << matrixDestination->getButtonText() << '\n';
    require(matrixSource->getButtonText().startsWith("SIDECHAIN / DIRECT")
                && matrixDestination->getButtonText() == "SIGNAL / DIRECT",
            "MATRIX does not expose the direct-sidechain contract");
    auto* modulationWorkspace = dynamic_cast<TR::Modulation::UI::ModulationWorkspace*>(
        findById(*editor, "modulation-workspace"));
    require(modulationWorkspace != nullptr, "FREQ modulation workspace is missing");
    matrixSource->triggerClick();
    dispatchPendingMessages();
    modulationWorkspace->setSelectorSearchText("SC3 / SPECTRAL");
    require(modulationWorkspace->visibleSelectorChoiceCount() == 1
                && !modulationWorkspace->chooseVisibleSelectorItem(0),
            "MATRIX allows SC3/SPECTRAL to feed the audio-rate carrier destination");
    modulationWorkspace->closeSelector();
    writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
             output.getChildFile("02-freq-matrix-carrier-route.png"));

    auto* sources = dynamic_cast<juce::Button*>(findById(*editor, "matrix-sources"));
    require(sources != nullptr, "SOURCES button is missing");
    sources->triggerClick();
    dispatchPendingMessages();
    auto* profile = dynamic_cast<TR::SimpleUIV2::SimpleChoiceSelector*>(
        findById(*editor, "source-profile"));
    require(profile != nullptr, "Carrier source profile selector is missing");
    require(findById(*editor, "source-response") == nullptr,
            "Obsolete SIGNAL RESPONSE selector still exists");
    profile->setSelectedId(5, juce::sendNotificationSync);
    dispatchPendingMessages();
    require(profile->selectedText().equalsIgnoreCase("SIDECHAIN / DIRECT"),
            "Source editor does not identify SIDECHAIN / DIRECT exactly");
    auto* center = findById(*editor, "source-bipolar-center");
    auto* gain = dynamic_cast<TR::SimpleUIV2::SimpleRailValueControl*>(
        findById(*editor, "source-gain"));
    auto* smooth = dynamic_cast<TR::SimpleUIV2::SimpleRailValueControl*>(
        findById(*editor, "source-smooth"));
    require(center != nullptr && !center->isShowing() && gain != nullptr && smooth != nullptr,
            "DIRECT exposes an irrelevant scalar control or omits conditioning controls");
    gain->setValue(6.5, juce::sendNotificationSync);
    smooth->setValue(0.72, juce::sendNotificationSync);
    dispatchPendingMessages();
    const auto editedState = processor->modulationState();
    require(std::abs(editedState.analysisSources[4].detector.gainDb - 6.5f) <= 1.0e-6f
                && std::abs(editedState.analysisSources[4].detector.smooth - 0.72f) <= 1.0e-6f,
            "DIRECT GAIN/SMOOTH UI controls did not reach the processor state");
    writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
             output.getChildFile("03-freq-source-sidechain-carrier.png"));
    profile->setSelectedId(4, juce::sendNotificationSync);
    dispatchPendingMessages();
    require(profile->selectedText().equalsIgnoreCase("SC3 / SPECTRAL"),
            "SOURCES does not use the canonical SC3 / SPECTRAL name");
    writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
             output.getChildFile("04-freq-source-sc3-spectral.png"));
    editor->removeFromDesktop();
}

void verifyHumanMain(const juce::File& output)
{
    auto processor = std::make_unique<FREQTRAudioProcessor>();
    processor->prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    editor->setVisible(true);
    dispatchPendingMessages();
    juce::AudioBuffer<float> telemetryAudio(2, 512);
    juce::MidiBuffer telemetryMidi;
    double phase = 0.0;
    for (int block = 0; block < 8; ++block)
    {
        for (int sample = 0; sample < telemetryAudio.getNumSamples(); ++sample)
        {
            const auto value = 0.25f * std::sin(static_cast<float>(phase));
            phase += juce::MathConstants<double>::twoPi * 440.0 / 48000.0;
            for (int channel = 0; channel < telemetryAudio.getNumChannels(); ++channel)
                telemetryAudio.setSample(channel, sample, value);
        }
        processor->processBlock(telemetryAudio, telemetryMidi);
    }
    const auto wetSnapshot = processor->getWetTelemetrySnapshot();
    require(wetSnapshot.sampleCount >= 64,
            "FREQ wet telemetry did not publish audio-thread samples");
    dispatchPendingMessages();
    auto* signature = findById(*editor, "visual-signature");
    require(signature != nullptr, "FREQ visual signature missing");
    const auto signatureImage = signature->createComponentSnapshot(
        signature->getLocalBounds(), true, 1.0f);
    const auto processedColour = TR::SimpleUIV2::simpleColour(
        TR::SimpleUIV2::SimpleColourRole::processedSignal, signature);
    require(containsColour(signatureImage, processedColour, 8),
            "FREQ real processedSignal trace is not visible");
    for (const auto scale : { 1.0f, 1.25f, 1.5f })
    {
        const auto scaled = signature->createComponentSnapshot(
            signature->getLocalBounds(), true, scale);
        require(scaled.getWidth() == static_cast<int>(std::lround(signature->getWidth() * scale))
                    && scaled.getHeight() == static_cast<int>(std::lround(signature->getHeight() * scale))
                    && containsColour(scaled, processedColour, static_cast<int>(8.0f * scale)),
                "FREQ processed trace is missing or clipped at an accepted scale");
        writePng(scaled, output.getChildFile(
            "freq-wet-" + juce::String(static_cast<int>(std::lround(scale * 100.0f))) + ".png"));
    }
    for (int block = 0; block < 16; ++block)
    {
        telemetryAudio.clear();
        processor->processBlock(telemetryAudio, telemetryMidi);
    }
    dispatchPendingMessages();
    const auto silentImage = signature->createComponentSnapshot(
        signature->getLocalBounds(), true, 1.0f);
    require(!containsColour(silentImage, processedColour, 8),
            "FREQ processed trace did not extinguish after sustained silence");

    auto* mainTab = findButtonByText(*editor, "MAIN");
    auto* ioTab = findButtonByText(*editor, "I/O");
    require(mainTab != nullptr && ioTab != nullptr, "MAIN/I/O navigation is incomplete");
    require(mainTab->getToggleState() && !ioTab->getToggleState(), "MAIN must be selected initially");
    require(std::abs(mainTab->getWidth() - ioTab->getWidth()) <= 1,
            "MAIN and I/O tabs must divide the rail evenly");
    require(findButtonByText(*editor, "CORE") == nullptr
                && findButtonByText(*editor, "SHAPE") == nullptr
                && findButtonByText(*editor, "CTRL") == nullptr,
            "Legacy task tabs remain visible");

    require(findById(*editor, "macro-engine") != nullptr, "ENGINE macro descriptor not found");
    require(findById(*editor, "macro-engine-route") == nullptr,
            "ENGINE must not duplicate BIAS/RECTIFY through a route button");
    require(findById(*editor, "engine-bias-control") != nullptr
                && findById(*editor, "engine-rectify-control") != nullptr,
            "BIAS and RECTIFY are not direct MAIN controls");
    require(findById(*editor, "rm-spread-control") != nullptr
                && findById(*editor, "rm-modulator-control") != nullptr,
            "RM SPREAD/MODULATOR controls were not constructed");
    auto* mod = findById(*editor, "mod-control");
    auto* modHarm = findById(*editor, "mod-control-inline-toggle");
    require(mod != nullptr && modHarm != nullptr
                && findById(*editor, "mod-harmonic-control") == nullptr,
            "MOD harmonic law is not composed into the MOD row");
    auto* modSlider = findDirectChild<juce::Slider>(*mod);
    require(modSlider != nullptr && !modSlider->getBounds().intersects(modHarm->getBounds())
                && mod->getLocalBounds().contains(modHarm->getBounds()),
            "MOD inline toggle overlaps its rail or escapes the row");
    auto* modHarmButton = dynamic_cast<juce::Button*>(modHarm);
    require(modHarmButton != nullptr, "MOD harmonic accessory is not clickable");
    modHarmButton->triggerClick();
    dispatchPendingMessages();
    require(processor->apvts.getRawParameterValue("mod_harm")->load() > 0.5f,
            "MOD harmonic accessory is not attached to mod_harm");
    auto* window = findById(*editor, "window-control");
    auto* hilbert = findById(*editor, "hilbert-mode-control");
    require(window != nullptr && hilbert != nullptr,
            "Frequency-shifter controls were not constructed");
    require(!window->isVisible() && !hilbert->isVisible(),
            "Frequency-shifter-only controls must not add noise in AM/RM");
    require(findById(*editor, "align-control") == nullptr
                && findById(*editor, "pdc-control") == nullptr
                && findById(*editor, "max-window-control") == nullptr,
            "Latency controls leaked into MAIN instead of their fixed I/O route");

    auto* viewport = findById(*editor, "task-viewport");
    auto* sync = findById(*editor, "sync-control");
    auto* retrig = findById(*editor, "retrig-control");
    auto* midi = findById(*editor, "midi-control");
    require(viewport != nullptr && sync != nullptr && retrig != nullptr
                && midi != nullptr,
            "MAIN terminal source rows are incomplete");
    auto* midiRoute = dynamic_cast<juce::Button*>(findById(*editor, "midi-control-route"));
    require(midiRoute != nullptr && midiRoute->getButtonText() == "+",
            "Prompt routes must use the shared plus glyph");
    auto* mainViewport = dynamic_cast<juce::Viewport*>(viewport);
    require(mainViewport != nullptr && mainViewport->getViewedComponent() != nullptr,
            "MAIN scroll viewport type changed");
    auto* mainContent = mainViewport->getViewedComponent();
    require(mainContent->isParentOf(sync) && mainContent->isParentOf(retrig)
                && mainContent->isParentOf(midi)
                && sync->getBottom() == retrig->getY()
                && retrig->getBottom() == midi->getY(),
            "MAIN source/session controls do not form one terminal scroll sequence");
    require(mainViewport->getVerticalScrollBar().isVisible(),
            "Overflowing MAIN does not expose its scrollbar");
    midiRoute->grabKeyboardFocus();
    dispatchPendingMessages();
    require(mainViewport->getViewPositionY() > 0,
            "Focusing the terminal MIDI row did not reveal it");
    mainViewport->setViewPosition(0, 0);
    dispatchPendingMessages();
    require(mainViewport->getViewPositionY() == 0,
            "Focused terminal control pins MAIN and blocks user scrolling");

    writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
             output.getChildFile("freq-human-main.png"));

    auto* engineParameter = processor->apvts.getParameter("engine");
    require(engineParameter != nullptr, "ENGINE APVTS parameter is missing");
	engineParameter->setValueNotifyingHost(0.5f);
	dispatchPendingMessages();
	auto* rmSpread = findById(*editor, "rm-spread-control");
	auto* rmModulator = findById(*editor, "rm-modulator-control");
	require(rmSpread != nullptr && rmSpread->isVisible()
	            && rmModulator != nullptr && rmModulator->isVisible(),
	        "RM SPREAD/MODULATOR controls are not visible at the RM engine position");
	mainViewport->setViewPosition(0, 0);
	dispatchPendingMessages();
	writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
	         output.getChildFile("freq-human-main-rm.png"));
    engineParameter->setValueNotifyingHost(1.0f);
    dispatchPendingMessages();
    require(window->isVisible() && hilbert->isVisible(),
            "Frequency-shifter controls did not enter MAIN with the engine");
    auto* windowParameter = processor->apvts.getParameter("window");
    require(windowParameter != nullptr, "WINDOW APVTS parameter is missing");
    windowParameter->setValueNotifyingHost(windowParameter->convertTo0to1(1024.0f));
    processor->setFreqShiftHilbertMode(FREQTRAudioProcessor::FreqShiftHilbertMode::allpass);
    dispatchPendingMessages();
    require(window->isVisible() && !window->isEnabled(),
            "Allpass must keep WINDOW visible but unavailable");
    require(std::abs(processor->apvts.getRawParameterValue("window")->load() - 1024.0f) < 0.01f,
            "Allpass mode overwrote the stored Linear FIR window");
    mainViewport->setViewPosition(0, 168);
    dispatchPendingMessages();
    writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
             output.getChildFile("freq-human-main-allpass.png"));
    processor->setFreqShiftHilbertMode(FREQTRAudioProcessor::FreqShiftHilbertMode::linear);
    dispatchPendingMessages();
    require(window->isEnabled()
                && std::abs(processor->apvts.getRawParameterValue("window")->load() - 1024.0f) < 0.01f,
            "Linear mode did not restore editable WINDOW without mutation");
    mainViewport->setViewPosition(0, 168);
    dispatchPendingMessages();
    writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
             output.getChildFile("freq-human-main-fs.png"));

    ioTab->triggerClick();
    dispatchPendingMessages();
    require(ioTab->getToggleState() && !mainTab->getToggleState(), "I/O selection failed");
    require(findById(*editor, "filter-options-action") != nullptr
                && findById(*editor, "routing-options-action") != nullptr
                && findById(*editor, "latency-options-action") != nullptr,
            "I/O fixed utilities are missing");
    require(findById(*editor, "input-control") != nullptr
                && findById(*editor, "output-control") != nullptr,
            "I/O level pair is missing");
    writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
             output.getChildFile("freq-human-io.png"));

    auto* filterActionControl = findById(*editor, "filter-options-action");
    auto* filterAction = filterActionControl != nullptr
                             ? findDirectChild<juce::Button>(*filterActionControl) : nullptr;
    require(filterAction != nullptr, "FILTER OPTIONS action is not clickable");
    filterAction->triggerClick();
    dispatchPendingMessages();
    auto* hpOn = findById(*editor, "prompt-filter-hp-inline-toggle");
    auto* hp = findById(*editor, "prompt-filter-hp");
    auto* hpSlope = findById(*editor, "prompt-filter-hp-inline-choice-2");
    require(hp != nullptr && hpOn != nullptr && hpSlope != nullptr,
            "Canonical FILTER HP rows are incomplete");
    auto* hpSlider = findDirectChild<juce::Slider>(*hp);
    auto* hpOnButton = dynamic_cast<juce::Button*>(hpOn);
    auto* hpSlopeButton = dynamic_cast<juce::Button*>(hpSlope);
    require(hpSlider != nullptr && hpOnButton != nullptr && hpSlopeButton != nullptr,
            "FILTER band actions are not clickable");
    hpOnButton->triggerClick();
    hpSlopeButton->triggerClick();
    dispatchPendingMessages();
    require(processor->apvts.getRawParameterValue("filter_hp_on")->load() > 0.5f
                && std::abs(processor->apvts.getRawParameterValue("filter_hp_slope")->load() - 2.0f) < 0.01f,
            "FILTER band actions are not attached to enable and slope parameters");
    writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
             output.getChildFile("freq-human-filter.png"));

    auto* detail = findById(*editor, "detail-panel");
    require(detail != nullptr, "FILTER detail panel did not open");
    detail->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    dispatchPendingMessages();
    auto* latencyActionControl = findById(*editor, "latency-options-action");
    auto* latencyAction = latencyActionControl != nullptr
                              ? findDirectChild<juce::Button>(*latencyActionControl) : nullptr;
    require(latencyAction != nullptr, "ALIGNMENT action is not clickable");
    latencyAction->triggerClick();
    dispatchPendingMessages();
    auto* alignmentControl = findById(*editor, "compensated-alignment-control");
    auto* liveButton = dynamic_cast<juce::Button*>(
        findById(*editor, "compensated-alignment-control-segment-0"));
    auto* compensatedButton = dynamic_cast<juce::Button*>(
        findById(*editor, "compensated-alignment-control-segment-1"));
    require(alignmentControl != nullptr && liveButton != nullptr && compensatedButton != nullptr,
            "ALIGNMENT does not expose the LIVE / COMPENSATED mode");
    require(findById(*editor, "align-control") != nullptr
                && findById(*editor, "pdc-control") != nullptr
                && findById(*editor, "max-window-control") != nullptr,
            "ALIGNMENT does not expose DRY/WET ALIGN, HOST COMP and MAX WINDOW");
    liveButton->triggerClick();
    dispatchPendingMessages();
    require(processor->apvts.getRawParameterValue("align")->load() < 0.5f
                && processor->apvts.getRawParameterValue("pdc")->load() < 0.5f,
            "LIVE did not disable both alignment parameters");
    auto* alignParameter = processor->apvts.getParameter("align");
    auto* pdcParameter = processor->apvts.getParameter("pdc");
    alignParameter->setValueNotifyingHost(1.0f);
    pdcParameter->setValueNotifyingHost(0.0f);
    dispatchPendingMessages();
    require(!liveButton->getToggleState() && !compensatedButton->getToggleState()
                && alignmentControl->getDescription().containsIgnoreCase("CUSTOM")
                && alignmentControl->getDescription().containsIgnoreCase("HOST COMP OFF"),
            "Mixed alignment state is not preserved and described as CUSTOM");
    compensatedButton->triggerClick();
    dispatchPendingMessages();
    require(processor->apvts.getRawParameterValue("align")->load() > 0.5f
                && processor->apvts.getRawParameterValue("pdc")->load() > 0.5f,
            "COMPENSATED did not enable both alignment parameters");
    writePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f),
             output.getChildFile("freq-human-alignment.png"));
    detail = findById(*editor, "detail-panel");
    require(detail != nullptr, "ALIGNMENT detail panel did not open");
    detail->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    dispatchPendingMessages();
    editor->removeFromDesktop();
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    try
    {
        if (argc != 2) throw std::runtime_error("Expected output directory");
        const juce::File output(juce::String::fromUTF8(argv[1]));
        require(output.createDirectory(), "Could not create output directory");
        verifySharedMotionRecipeContract();
        verifyHumanMain(output);
        verifyCarrierJourney(output);
        std::cout << "FREQ human MAIN render probe passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FREQ human MAIN render probe failed: " << exception.what() << '\n';
        return 1;
    }
}
