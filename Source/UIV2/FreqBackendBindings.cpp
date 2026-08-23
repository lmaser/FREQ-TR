#include "FreqBackendBindings.h"
#include "FreqUiDefinition.h"
#include "../Modulation/FreqModulationConfig.h"
#include "../../../TR-Shared/Modulation/Integration/TRModulationPresetCodec.h"

#include <algorithm>
#include <cmath>

namespace TR::FreqUIV2
{
namespace
{
constexpr const char* midiPortKey = "midiPort";
constexpr const char* midiDelayKey = "midiDelayMs";
constexpr const char* selectedTaskKey = "uiV2SelectedTask";
constexpr const char* surfaceKey = "uiV2Surface";

float raw(const FREQTRAudioProcessor& processor, const char* id, float fallback) noexcept
{
    if (const auto* value = processor.apvts.getRawParameterValue(id))
        return value->load(std::memory_order_relaxed);
    return fallback;
}

double linearMultiplier(double value) noexcept
{
    value = juce::jlimit(0.0, 1.0, value);
    return value < 0.5 ? 1.0 / (4.0 - 6.0 * value)
                       : 1.0 + (value - 0.5) * 6.0;
}

double sliderFromLinearMultiplier(double multiplier) noexcept
{
    multiplier = juce::jlimit(0.25, 4.0, multiplier);
    return multiplier < 1.0 ? (4.0 - 1.0 / multiplier) / 6.0
                            : 0.5 + (multiplier - 1.0) / 6.0;
}

int harmonicStep(double value) noexcept
{
    return juce::jlimit(-8, 8, juce::roundToInt(juce::jlimit(0.0, 1.0, value) * 16.0 - 8.0));
}

double sliderFromHarmonicStep(int step) noexcept
{
    return static_cast<double>(juce::jlimit(-8, 8, step) + 8) / 16.0;
}

juce::String engineName(double value)
{
    value = juce::jlimit(0.0, 1.0, value);
    if (value < 0.01) return "AM";
    if (std::abs(value - 0.5) < 0.01) return "RM";
    if (value > 0.99) return "FS";
    if (value < 0.5)
        return "AM|RM " + juce::String(juce::roundToInt(value * 200.0)) + "%";
    return "RM|FS " + juce::String(juce::roundToInt((value - 0.5) * 200.0)) + "%";
}

std::vector<double> canonicalWindowsUpTo(int maximum)
{
    std::vector<double> result;
    for (const auto candidate : FREQTRAudioProcessor::kHilbertWindows)
        if (candidate <= maximum) result.push_back(static_cast<double>(candidate));
	return result;
}

}

FreqBackendBindings::FreqBackendBindings(FREQTRAudioProcessor& processorToUse) noexcept
    : processor(processorToUse)
{
    processor.beginWetTelemetryCapture();
}

FreqBackendBindings::~FreqBackendBindings()
{
    processor.endWetTelemetryCapture();
}

juce::AudioProcessorValueTreeState& FreqBackendBindings::parameters() const noexcept
{
    return processor.apvts;
}

SimpleUIV2::ParameterSnapshot FreqBackendBindings::parameterSnapshot() const
{
    SimpleUIV2::ParameterSnapshot values;
    updateParameterSnapshot(values);
    return values;
}

void FreqBackendBindings::updateParameterSnapshot(SimpleUIV2::ParameterSnapshot& values) const
{
    if (values.empty()) values.reserve(definition().parameters.size());
    for (const auto& parameter : definition().parameters)
    {
        if (parameter.domain != SimpleUIV2::StateDomain::musicalParameter) continue;
        if (const auto* value = processor.apvts.getRawParameterValue(juce::String(parameter.parameterId)))
            values[parameter.parameterId] = static_cast<double>(value->load(std::memory_order_relaxed));
    }
    values["carrier_route_active"] = processor.routedCarrierConfigured() ? 1.0 : 0.0;
}

void FreqBackendBindings::prepareForUiRefresh()
{
    carrierTelemetry = processor.getCarrierTopologyTelemetry();
    wetTelemetrySnapshot = processor.getWetTelemetrySnapshot();
    signatureAudio.sampleCount = juce::jmin(wetTelemetrySnapshot.sampleCount,
                                            signatureAudio.samples.size());
    std::copy_n(wetTelemetrySnapshot.samples.begin(), signatureAudio.sampleCount,
                signatureAudio.samples.begin());
    signatureAudio.sequence = wetTelemetrySnapshot.sequence;
    signatureAudio.sampleRate = wetTelemetrySnapshot.sampleRate;
    signatureAudio.engine = wetTelemetrySnapshot.engine;
    signatureAudio.triggerActive = wetTelemetrySnapshot.active;
    const int maximum = FREQTRAudioProcessor::getCanonicalHilbertWindow(juce::roundToInt(
        raw(processor, FREQTRAudioProcessor::kParamMaxWindow,
            static_cast<float>(FREQTRAudioProcessor::kHilbertMaxWindowDefault))));
    if (maximum == preparedMaxWindow) return;

    const int current = FREQTRAudioProcessor::getCanonicalHilbertWindow(juce::roundToInt(
        raw(processor, FREQTRAudioProcessor::kParamWindow,
            static_cast<float>(FREQTRAudioProcessor::kHilbertWindowDefault))));
    if (current > maximum)
        if (auto* parameter = processor.apvts.getParameter(FREQTRAudioProcessor::kParamWindow))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(maximum)));
    preparedMaxWindow = maximum;
}

const SimpleUIV2::SignatureAudioSnapshot*
FreqBackendBindings::signatureAudioSnapshot() const noexcept
{
    return &signatureAudio;
}

std::optional<float> FreqBackendBindings::signatureSemanticValue(std::string_view role) const
{
    if (role == "carrierFrequency") return carrierTelemetry.carrierFrequency;
    if (role == "enginePosition") return carrierTelemetry.enginePosition;
    if (role == "harmonicAmount") return carrierTelemetry.harmonicAmount;
    if (role == "harmonicCapacity") return carrierTelemetry.harmonicCapacity;
    if (role == "feedbackMagnitude") return carrierTelemetry.feedbackMagnitude;
    if (role == "feedbackPolarity") return carrierTelemetry.feedbackPolarity;
    if (role == "feedbackSpacing") return carrierTelemetry.feedbackSpacing;
    if (role == "engineBias") return carrierTelemetry.engineBias;
    if (role == "engineRectify") return carrierTelemetry.engineRectify;
    if (role == "stereoTopology") return carrierTelemetry.stereoTopology;
    return std::nullopt;
}

std::optional<SimpleUIV2::ControlValuePolicy> FreqBackendBindings::controlValuePolicy(
    std::string_view controlId, std::string_view parameterId) const
{
    juce::ignoreUnused(parameterId);
    if (controlId == "max-window-control")
        return SimpleUIV2::ControlValuePolicy { 128.0, 2048.0, 0.0, 512.0,
                                                canonicalWindowsUpTo(2048) };
    if (controlId == "window-control")
    {
        const int maximum = FREQTRAudioProcessor::getCanonicalHilbertWindow(juce::roundToInt(
            raw(processor, FREQTRAudioProcessor::kParamMaxWindow,
                static_cast<float>(FREQTRAudioProcessor::kHilbertMaxWindowDefault))));
        return SimpleUIV2::ControlValuePolicy { 128.0, static_cast<double>(maximum), 0.0,
                                                static_cast<double>(juce::jmin(512, maximum)),
                                                canonicalWindowsUpTo(maximum) };
    }
    return std::nullopt;
}

std::optional<juce::String> FreqBackendBindings::formatControlValue(
    std::string_view controlId, double value) const
{
    if (controlId == "macro-frequency")
    {
        if (raw(processor, FREQTRAudioProcessor::kParamSync, 0.0f) > 0.5f)
            return FREQTRAudioProcessor::getFreqSyncName(juce::roundToInt(value));
        return std::nullopt;
    }
    if (controlId == "macro-engine") return engineName(value);
    if (controlId == "mod-control")
    {
        if (raw(processor, FREQTRAudioProcessor::kParamModHarm, 0.0f) > 0.5f)
        {
            const int step = harmonicStep(value);
            return step > 0 ? "H+" + juce::String(step) : "H" + juce::String(step);
        }
        return "x" + juce::String(linearMultiplier(value), 2);
    }
    if (controlId == "window-control" || controlId == "max-window-control")
        return juce::String(FREQTRAudioProcessor::getCanonicalHilbertWindow(juce::roundToInt(value)));
    return std::nullopt;
}

std::optional<double> FreqBackendBindings::parseControlValue(
    std::string_view controlId, const juce::String& text) const
{
    if (controlId == "macro-frequency"
        && raw(processor, FREQTRAudioProcessor::kParamSync, 0.0f) > 0.5f)
    {
        const auto choices = FREQTRAudioProcessor::getFreqSyncChoices();
        const int exact = choices.indexOf(text.trim(), true);
        if (exact >= 0) return static_cast<double>(exact);
        return std::nullopt;
    }

    const auto numeric = text.retainCharacters("0123456789-+.,").replaceCharacter(',', '.');
    if (controlId == "macro-engine")
    {
        const auto upper = text.toUpperCase();
        if (upper.trim() == "AM") return 0.0;
        if (upper.trim() == "RM") return 0.5;
        if (upper.trim() == "FS" || upper.contains("FREQ")) return 1.0;
        return juce::jlimit(0.0, 1.0, numeric.getDoubleValue() * 0.01);
    }
    if (controlId == "mod-control")
    {
        if (raw(processor, FREQTRAudioProcessor::kParamModHarm, 0.0f) > 0.5f)
            return sliderFromHarmonicStep(numeric.getIntValue());
        return sliderFromLinearMultiplier(numeric.getDoubleValue());
    }
    if (controlId == "window-control" || controlId == "max-window-control")
        return static_cast<double>(FREQTRAudioProcessor::getCanonicalHilbertWindow(numeric.getIntValue()));
    return std::nullopt;
}

std::string FreqBackendBindings::resolveControlParameter(
    std::string_view controlId, std::string_view declaredParameter) const
{
    if (controlId == "macro-frequency"
        && raw(processor, FREQTRAudioProcessor::kParamSync, 0.0f) > 0.5f)
        return FREQTRAudioProcessor::kParamFreqSync;
    return SimpleUIV2::SimpleJuceBackend::resolveControlParameter(controlId, declaredParameter);
}

float FreqBackendBindings::inputMeterPeak() const noexcept { return processor.getInputMeterPeak(); }
float FreqBackendBindings::outputMeterPeak() const noexcept { return processor.getOutputMeterPeak(); }

SimpleUIV2::MusicalState FreqBackendBindings::readMusicalState() const
{
    SimpleUIV2::MusicalState state;
	state.values.emplace(midiPortKey, static_cast<double>(processor.getMidiChannel()));
	state.values.emplace(midiDelayKey, static_cast<double>(processor.getMidiDelayMs()));
	Modulation::Integration::writePresetState(state, processor.modulationState());
    return state;
}

SimpleUIV2::MusicalState FreqBackendBindings::defaultMusicalState() const
{
    SimpleUIV2::MusicalState state;
	state.values = { { midiPortKey, 0.0 }, { midiDelayKey, 0.0 } };
	Modulation::Integration::writePresetState(state, Modulation::makeDefaultState());
    return state;
}

bool FreqBackendBindings::validateMusicalState(const SimpleUIV2::MusicalState& state) const noexcept
{
	const auto marker = state.values.find(Modulation::Integration::presetStateId);
	const bool legacyMarker = marker != state.values.end() && marker->second == 0.0;
	if (state.values.size() != static_cast<std::size_t>(legacyMarker ? 3 : 2)
		|| state.textValues.size() > 1
		|| (!state.textValues.empty()
			&& state.textValues.find(Modulation::Integration::presetStateId)
				== state.textValues.end())) return false;
    const auto channel = state.values.find(midiPortKey);
    const auto delay = state.values.find(midiDelayKey);
    if (channel == state.values.end() || delay == state.values.end())
        return false;
    const auto integral = [](double value) { return std::isfinite(value) && std::floor(value) == value; };
	Modulation::State modulation;
	return integral(channel->second) && channel->second >= 0.0 && channel->second <= 16.0
		&& integral(delay->second) && delay->second >= 0.0 && delay->second <= 100.0
		&& Modulation::Integration::readPresetState(state, modulation);
}

void FreqBackendBindings::writeMusicalState(const SimpleUIV2::MusicalState& state)
{
    if (!validateMusicalState(state)) return;
	processor.setMidiChannel(static_cast<int>(state.values.at(midiPortKey)));
	processor.setMidiDelayMs(static_cast<int>(state.values.at(midiDelayKey)));
	Modulation::State modulation;
	if (Modulation::Integration::readPresetState(state, modulation))
		processor.setModulationState(modulation);
}

SimpleUIV2::UiInstanceState FreqBackendBindings::readUiInstanceState() const
{
    SimpleUIV2::UiInstanceState state;
    state.selectedTask = static_cast<SimpleUIV2::TaskId>(juce::jlimit(
        0, 3, static_cast<int>(processor.apvts.state.getProperty(selectedTaskKey, 0))));
    state.surface = static_cast<SimpleUIV2::UiSurface>(juce::jlimit(
        0, 2, static_cast<int>(processor.apvts.state.getProperty(surfaceKey, 0))));
    return state;
}

void FreqBackendBindings::writeUiInstanceState(const SimpleUIV2::UiInstanceState& state)
{
    processor.apvts.state.setProperty(selectedTaskKey, static_cast<int>(state.selectedTask), nullptr);
    processor.apvts.state.setProperty(surfaceKey, static_cast<int>(state.surface), nullptr);
}

void FreqBackendBindings::setMacroName(int index, const juce::String& name)
{
    if (index < 0 || index >= Modulation::macroCount) return;
    auto mod = processor.modulationState();
    mod.macros[static_cast<std::size_t>(index)].name = name;
    processor.setModulationState(mod);
}

Modulation::State FreqBackendBindings::modulationState() const { return processor.modulationState(); }
std::uint64_t FreqBackendBindings::modulationStateGeneration() const noexcept { return processor.modulationStateGeneration(); }
std::array<float, Modulation::macroCount> FreqBackendBindings::modulationMacroValues() const noexcept { return processor.modulationMacroValues(); }
void FreqBackendBindings::setModulationMacroValue(int macro, float value) { processor.setModulationMacroValue(macro, value); }
bool FreqBackendBindings::setModulationState(const Modulation::State& state) { return processor.setModulationState(state); }
Modulation::UI::SourceCapabilities FreqBackendBindings::modulationSourceCapabilities() const noexcept { return { true }; }
std::vector<Modulation::UI::MotionRecipeOption> FreqBackendBindings::modulationRecipeOptions() const
{
    return { { "native-jitter", "NATIVE JITTER" } };
}
bool FreqBackendBindings::installModulationRecipe(const juce::String& id, int macro)
{
    if (id != "native-jitter") return false;
    auto* parameter = processor.apvts.getParameter(FREQTRAudioProcessor::kParamJitter);
    if (parameter == nullptr) return false;
    const auto nativeAmount = parameter->getValue();
    const auto candidate = FreqModulation::makeJitterParityRecipe(
        processor.modulationState(), macro);
    if (!processor.setModulationState(candidate)) return false;
    processor.setModulationMacroValue(macro - 1, nativeAmount);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(0.0f));
    parameter->endChangeGesture();
    return true;
}
Modulation::Runtime::TelemetrySnapshot FreqBackendBindings::modulationTelemetry() const noexcept { return processor.modulationTelemetry(); }
Modulation::UI::SidechainWorkspaceCallbacks FreqBackendBindings::sidechainWorkspaceCallbacks()
{
    return {};
}
}
