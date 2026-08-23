#include "../Source/PluginProcessor.h"
#include "../Source/UIV2/FreqBackendBindings.h"
#include "../Source/UIV2/FreqUiDefinition.h"
#include "../Source/Modulation/FreqModulationConfig.h"
#include "../../TR-Shared/Modulation/Tests/TRNativeSidechainBaseline.h"
#include "../../TR-Shared/Modulation/Tests/TRModulationJourneyAssertions.h"
#include "../../TR-Shared/Modulation/Tests/TRJitterMotionEvidence.h"
#include "../../TR-Shared/Modulation/Tests/TRMotionRecipeUiAssertions.h"
#include "../../TR-Shared/SimpleUIV2/Preset/TRPresetManager.h"
#include "../../TR-Shared/Testing/TRPluginCpuBenchmark.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

struct FreqNativeSidechainTestAccess
{
    static void captureJitter(FREQTRAudioProcessor& processor, float* left, float* right,
                              int capacity) noexcept
    {
        processor.jitterEvidenceNativeLForTests_ = left;
        processor.jitterEvidenceNativeRForTests_ = right;
        processor.jitterEvidenceCapacityForTests_ = capacity;
        processor.jitterEvidenceIndexForTests_ = 0;
    }
	static void useNative(FREQTRAudioProcessor& processor, bool enabled)
	{
		processor.useNativeSidechainForTests_ = enabled;
	}
    static bool routedCarrierIsValid(const FREQTRAudioProcessor& processor) noexcept
    {
        return processor.sharedConditionedSidechain_.valid()
            && std::abs(processor.sharedSidechainRouteAmount_ - 1.0f) <= 1.0e-6f;
    }
    static void extract(const FREQTRAudioProcessor& processor, int sample, float* values)
    {
        const auto index = static_cast<std::size_t>(sample);
        values[0] = processor.sidechainModBufL[index];
        values[1] = processor.sidechainModBufR[index];
        values[2] = processor.sidechainFreqShiftSmoothL_;
        values[3] = processor.sidechainFreqShiftSmoothR_;
        values[4] = processor.sidechainRmsEnv_;
        values[5] = processor.sidechainGateSmoothed_;
        values[6] = processor.sidechainDepthSmoothed_;
    }

    static void extractShared(const FREQTRAudioProcessor& processor, int sample, float* values)
	{
		const auto audio = processor.sharedConditionedSidechain_;
		const auto control = processor.sharedSidechainControl_;
		const auto activity = processor.sharedSidechainActivity_;
		const auto gate = activity.valid() && sample < activity.sampleCount ? activity.samples[sample] : 0.0f;
		values[0] = audio.valid() && sample < audio.sampleCount ? audio.channels[0][sample] * gate : 0.0f;
		values[1] = audio.valid() && audio.channelCount > 1 && sample < audio.sampleCount
			? audio.channels[1][sample] * gate : values[0];
		values[2] = control.valid() && sample < control.sampleCount ? control.samples[sample] : 0.0f;
	}
    static float nativeCarrierDeviation(const FREQTRAudioProcessor& processor, int channel) noexcept
    {
        const auto base = channel == 0 ? processor.smoothedFreq
            : processor.smoothedFreq * (processor.jitterStereo_ ? 0.5f : 1.0f);
        const auto moved = processor.getJitteredFrequencyHz(base, channel);
        const auto magnitude = std::abs(base);
        const auto reference = std::sqrt(magnitude * magnitude
            + FREQTRAudioProcessor::kJitterFrequencyFloorHz
              * FREQTRAudioProcessor::kJitterFrequencyFloorHz);
        const auto sign = base < 0.0f ? -1.0f : 1.0f;
        return reference > 1.0e-6f ? (base - moved) / (sign * reference) : 0.0f;
    }
    static bool feedbackInvariant(const FREQTRAudioProcessor& processor) noexcept
    {
        const auto input = processor.feedbackSmoothed.getCurrentValue();
        return std::abs(processor.applyJitterToFeedbackMagnitude(input) - input) <= 1.0e-7f;
    }
    static float matrixCarrierDeviation(const FREQTRAudioProcessor& processor, int channel,
                                        int sample) noexcept
    {
        return processor.modulation.effectiveNativeAtSample(
            channel == 0 ? TR::FreqModulation::carrierDeviationL
                         : TR::FreqModulation::carrierDeviationR,
            sample, 0.0f);
    }
    static float nativeBiasJitter(const FREQTRAudioProcessor& processor) noexcept
    {
        return 0.5f * (processor.jitterFreqOut_[0] + processor.jitterFreqOut_[1])
            * 0.040f * processor.jitterAmountSmoothed_;
    }
    static float matrixBiasJitter(const FREQTRAudioProcessor& processor, int sample) noexcept
    {
        return 0.5f * (processor.modulation.effectiveNativeAtSample(
            TR::FreqModulation::biasJitterL, sample, 0.0f)
            + processor.modulation.effectiveNativeAtSample(
                TR::FreqModulation::biasJitterR, sample, 0.0f));
    }
    static float nativeCombOctave(const FREQTRAudioProcessor& processor) noexcept
    {
        return processor.jitterCombOut_ * processor.jitterCombDepthOct_;
    }
    static float matrixCombOctave(const FREQTRAudioProcessor& processor, int sample) noexcept
    {
        return processor.modulation.effectiveNativeAtSample(
            TR::FreqModulation::combOctave, sample, 0.0f);
    }
};

namespace
{
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
juce::Component* findById(juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren()) if (auto* found = findById(*child, id)) return found;
    return nullptr;
}
void process(FREQTRAudioProcessor& processor, bool noteOn, float sidechain = 0.0f)
{
    constexpr int blockSize = 512;
    juce::AudioBuffer<float> audio(processor.getTotalNumInputChannels(), blockSize);
    for (int sample = 0; sample < blockSize; ++sample)
    {
        const auto value = 0.1f * std::sin(0.01f * static_cast<float>(sample));
        audio.setSample(0, sample, value); audio.setSample(1, sample, value);
        if (audio.getNumChannels() >= 4)
        {
            const auto sc = sidechain * std::sin(0.13f * static_cast<float>(sample));
            audio.setSample(2, sample, sc); audio.setSample(3, sample, sc);
        }
    }
    juce::MidiBuffer midi;
    if (noteOn) midi.addEvent(juce::MidiMessage::noteOn(1, 127, static_cast<juce::uint8>(127)), 16);
    processor.processBlock(audio, midi);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < blockSize; ++sample)
            require(std::isfinite(audio.getSample(channel, sample)), "FREQ produced non-finite audio");
}

struct FreqJitterRender
{
    std::vector<float> audio;
    std::vector<float> controlL;
    std::vector<float> controlR;
    std::vector<float> reference;
    bool feedbackInvariant = true;
};

FreqJitterRender renderFreqJitterEvidence(int path, float amount, float frequencyHz,
                                          double sampleRate = 48000.0, int blockSize = 257,
                                          bool automate = false)
{
    const int seconds = automate ? 24 : 8;
    const auto totalSamples = static_cast<int>(sampleRate) * seconds;
    auto processor = std::make_unique<FREQTRAudioProcessor>();
    processor->prepareToPlay(sampleRate, blockSize);
    using TR::Modulation::Tests::setNativeBaselineParameter;
    require(setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamFreq, frequencyHz)
                && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamJitter,
                                               0.0f)
                && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamStyle, 3.0f)
                && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamMix, 1.0f)
                && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamFeedback, 0.55f)
                && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamComb, 180.0f),
            "FREQ Jitter evidence parameters rejected");
    if (path == 2)
    {
        auto recipe = TR::FreqModulation::makeJitterParityRecipe(TR::Modulation::makeDefaultState());
        require(setNativeBaselineParameter(processor->apvts, "mod_macro_1", 0.0f)
                    && processor->setModulationState(recipe),
                "FREQ Jitter evidence recipe rejected");
    }

    // Settle product parameter smoothing before both inactive Jitter engines are
    // activated. Otherwise the native path follows the ramped carrier while the
    // block-level reference describes its final value, invalidating the comparison.
    for (int remaining = static_cast<int>(sampleRate); remaining > 0;)
    {
        const auto count = juce::jmin(blockSize, remaining);
        juce::AudioBuffer<float> settle(
            juce::jmax(2, processor->getTotalNumInputChannels()), count);
        settle.clear();
        juce::MidiBuffer midi;
        processor->processBlock(settle, midi);
        remaining -= count;
    }
    require(setNativeBaselineParameter(processor->apvts,
                path == 1 ? FREQTRAudioProcessor::kParamJitter : "mod_macro_1", amount),
            "FREQ Jitter evidence activation rejected");

    FreqJitterRender result;
    result.audio.resize(static_cast<std::size_t>(totalSamples * 2));
    int offset = 0;
    while (offset < totalSamples)
    {
        const auto count = juce::jmin(blockSize, totalSamples - offset);
        if (automate)
        {
            constexpr float frequencies[] { 80.0f, 500.0f, 6000.0f, 120.0f };
            constexpr float amounts[] { 0.2f, 1.0f, 0.6f, 0.35f };
            const auto segment = static_cast<int>(offset / (sampleRate * 0.5)) & 3;
            require(setNativeBaselineParameter(processor->apvts,
                        FREQTRAudioProcessor::kParamFreq, frequencies[segment])
                        && setNativeBaselineParameter(processor->apvts,
                            path == 1 ? FREQTRAudioProcessor::kParamJitter : "mod_macro_1",
                            amounts[segment]),
                    "FREQ Jitter automation target rejected");
        }
        juce::AudioBuffer<float> block(juce::jmax(2, processor->getTotalNumInputChannels()), count);
        for (int sample = 0; sample < count; ++sample)
        {
            const auto absolute = offset + sample;
            const auto sawPhase = std::fmod(65.406 * absolute / sampleRate, 1.0);
            const auto saw = 0.16f * (2.0f * static_cast<float>(sawPhase) - 1.0f);
            const auto harmonic = 0.04f * static_cast<float>(std::sin(
                juce::MathConstants<double>::twoPi * 261.626 * absolute / sampleRate));
            block.setSample(0, sample, saw + harmonic);
            block.setSample(1, sample, 0.82f * saw - harmonic);
        }
        juce::MidiBuffer midi;
        std::vector<float> nativeL(static_cast<std::size_t>(count), 0.0f);
        std::vector<float> nativeR(static_cast<std::size_t>(count), 0.0f);
        if (path == 1)
            FreqNativeSidechainTestAccess::captureJitter(
                *processor, nativeL.data(), nativeR.data(), count);
        processor->processBlock(block, midi);
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel)
                result.audio[static_cast<std::size_t>((offset + sample) * 2 + channel)] =
                    block.getSample(channel, sample);
        const auto telemetry = processor->modulationTelemetry();
        const auto motion = TR::Modulation::Runtime::additionalMotionSourceFirstIndex;
        for (int sample = 0; sample < count; ++sample)
        {
            result.controlL.push_back(path == 1 ? nativeL[static_cast<std::size_t>(sample)]
                : path == 2 ? FreqNativeSidechainTestAccess::matrixCarrierDeviation(
                    *processor, 0, sample) : 0.0f);
            result.controlR.push_back(path == 1 ? nativeR[static_cast<std::size_t>(sample)]
                : path == 2 ? FreqNativeSidechainTestAccess::matrixCarrierDeviation(
                    *processor, 1, sample) : 0.0f);
            result.reference.push_back(path == 2
                ? telemetry.sources[motion].effectiveMotionReference : 0.0f);
        }
        FreqNativeSidechainTestAccess::captureJitter(*processor, nullptr, nullptr, 0);
        result.feedbackInvariant = result.feedbackInvariant
            && FreqNativeSidechainTestAccess::feedbackInvariant(*processor);
        offset += count;
    }
    return result;
}

bool writeFreqJitterHostMatrix(const juce::File& output)
{
    std::ofstream csv(output.getFullPathName().toStdString(), std::ios::trunc);
    csv << "sample_rate_hz,block_size,lane_l_rms_ratio,lane_r_rms_ratio,feedback_invariant\n";
    bool passed = true;
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto blockSize : { 64, 257, 2048 })
        {
            const auto native = renderFreqJitterEvidence(1, 0.6f, 500.0f, sampleRate, blockSize);
            const auto matrix = renderFreqJitterEvidence(2, 0.6f, 500.0f, sampleRate, blockSize);
            const auto skip = static_cast<std::size_t>(sampleRate);
            const auto left = TR::Modulation::Tests::rmsRatio(
                native.controlL, matrix.controlL, skip);
            const auto right = TR::Modulation::Tests::rmsRatio(
                native.controlR, matrix.controlR, skip);
            const auto feedback = native.feedbackInvariant && matrix.feedbackInvariant;
            const auto rowPassed = left >= 0.97 && left <= 1.03
                && right >= 0.97 && right <= 1.03 && feedback;
            passed = passed && rowPassed;
            csv << sampleRate << ',' << blockSize << ',' << left << ',' << right
                << ',' << feedback << '\n';
        }
    return csv.good() && passed;
}

bool writeFreqJitterAutomationMatrix(const juce::File& output)
{
    std::ofstream csv(output.getFullPathName().toStdString(), std::ios::trunc);
    csv << "sample_rate_hz,block_size,lane_l_rms_ratio,lane_r_rms_ratio,lane_l_max_window_rms_error,lane_r_max_window_rms_error,feedback_invariant\n";
    bool passed = true;
    for (const auto sampleRate : { 48000.0, 96000.0 })
        for (const auto blockSize : { 64, 2048 })
        {
            const auto native = renderFreqJitterEvidence(1, 0.6f, 500.0f,
                sampleRate, blockSize, true);
            const auto matrix = renderFreqJitterEvidence(2, 0.6f, 500.0f,
                sampleRate, blockSize, true);
            const auto skip = static_cast<std::size_t>(sampleRate);
            const auto window = static_cast<std::size_t>(sampleRate * 0.25);
            const auto leftRatio = TR::Modulation::Tests::rmsRatio(
                native.controlL, matrix.controlL, skip);
            const auto rightRatio = TR::Modulation::Tests::rmsRatio(
                native.controlR, matrix.controlR, skip);
            const auto left = TR::Modulation::Tests::maximumWindowedRmsRatioError(
                native.controlL, matrix.controlL, skip, window);
            const auto right = TR::Modulation::Tests::maximumWindowedRmsRatioError(
                native.controlR, matrix.controlR, skip, window);
            const auto feedback = native.feedbackInvariant && matrix.feedbackInvariant;
            passed = passed && leftRatio >= 0.95 && leftRatio <= 1.05
                && rightRatio >= 0.95 && rightRatio <= 1.05 && feedback;
            csv << sampleRate << ',' << blockSize << ',' << leftRatio << ',' << rightRatio
                << ',' << left << ',' << right
                << ',' << feedback << '\n';
        }
    return csv.good() && passed;
}

bool writeFreqJitterEvidence(const juce::File& output)
{
    require(output.createDirectory(), "FREQ Jitter evidence directory unavailable");
    std::ofstream metrics(output.getChildFile("metrics.csv").getFullPathName().toStdString());
    std::ofstream trace(output.getChildFile("control-trace.csv").getFullPathName().toStdString());
    std::ofstream manifest(output.getChildFile("control-manifest.csv").getFullPathName().toStdString());
    metrics << "frequency_hz,amount,native_deviation_rms,matrix_deviation_rms,ratio,control_stereo_native,control_stereo_matrix,feedback_invariant\n";
    trace << "frequency_hz,amount,frame,native_l,native_r,matrix_l,matrix_r,matrix_reference\n";
    manifest << "frequency_hz,amount,file,sample_rate_hz,lane_relationship\n";
    for (const auto frequency : { 40.0f, 500.0f, 4000.0f })
        for (const auto amount : { 0.25f, 0.6f, 1.0f })
        {
            const auto baseline = renderFreqJitterEvidence(0, amount, frequency);
            const auto native = renderFreqJitterEvidence(1, amount, frequency);
            const auto matrix = renderFreqJitterEvidence(2, amount, frequency);
            const auto comparison = TR::Modulation::Tests::compareJitterRenders(
                baseline.audio, native.audio, matrix.audio, 48000u * 2u);
            metrics << frequency << ',' << amount << ',' << comparison.nativeDeviationRms << ','
                    << comparison.matrixDeviationRms << ',' << comparison.deviationRatio << ','
                    << TR::Modulation::Tests::correlation(native.controlL, native.controlR, 16) << ','
                    << TR::Modulation::Tests::correlation(matrix.controlL, matrix.controlR, 16) << ','
                    << (native.feedbackInvariant && matrix.feedbackInvariant) << '\n';
            const auto traceName = "control-" + juce::String((int) frequency) + "hz-"
                + juce::String((int) std::round(amount * 100.0f)) + "pct.f32";
            require(TR::Modulation::Tests::writeFourLaneFloatTrace(
                        output.getChildFile(traceName), native.controlL, native.controlR,
                        matrix.controlL, matrix.controlR),
                    "FREQ full-rate control trace export failed");
            manifest << frequency << ',' << amount << ',' << traceName
                     << ",48000,independent\n";
            if (frequency == 500.0f && amount == 1.0f)
                for (std::size_t frame = 0; frame < native.controlL.size(); frame += 64)
                    trace << frequency << ',' << amount << ',' << frame << ','
                          << native.controlL[frame] << ',' << native.controlR[frame] << ','
                          << matrix.controlL[frame] << ',' << matrix.controlR[frame] << ','
                          << matrix.reference[frame] << '\n';
            if (frequency == 500.0f && amount == 1.0f)
            {
                using TR::Modulation::Tests::differenceRender;
                using TR::Modulation::Tests::writeStereoWav;
                require(writeStereoWav(output.getChildFile("baseline.wav"), baseline.audio, 48000.0)
                            && writeStereoWav(output.getChildFile("native.wav"), native.audio, 48000.0)
                            && writeStereoWav(output.getChildFile("matrix.wav"), matrix.audio, 48000.0)
                            && writeStereoWav(output.getChildFile("native-minus-baseline.wav"),
                                              differenceRender(native.audio, baseline.audio), 48000.0)
                            && writeStereoWav(output.getChildFile("matrix-minus-baseline.wav"),
                                              differenceRender(matrix.audio, baseline.audio), 48000.0),
                        "FREQ Jitter evidence WAV export failed");
            }
        }
    auto presetProcessor = std::make_unique<FREQTRAudioProcessor>();
    auto presetState = TR::FreqModulation::makeJitterParityRecipe(
        TR::Modulation::makeDefaultState());
    require(TR::Modulation::Tests::setNativeBaselineParameter(
                presetProcessor->apvts, FREQTRAudioProcessor::kParamJitter, 0.0f)
                && TR::Modulation::Tests::setNativeBaselineParameter(
                    presetProcessor->apvts, "mod_macro_1", 1.0f)
                && presetProcessor->setModulationState(presetState),
            "FREQ Jitter MATRIX preset state rejected");
    const auto staging = output.getChildFile("preset-staging");
    TR::FreqUIV2::FreqBackendBindings presetBackend(*presetProcessor);
    TR::SimpleUIV2::TRPresetManager presetManager(
        TR::FreqUIV2::definition(), presetBackend, staging);
    constexpr const char* presetName = "FREQ Jitter MATRIX 100";
    require(presetManager.saveAs(presetName, true).wasOk(),
            "FREQ Jitter MATRIX preset could not be saved");
    const auto savedPreset = presetManager.libraryFolder().getChildFile(
        juce::String(presetName) + ".trpreset");
    const auto evidencePreset = output.getChildFile(savedPreset.getFileName());
    require(savedPreset.existsAsFile() && savedPreset.copyFileTo(evidencePreset),
            "FREQ Jitter MATRIX preset could not be copied to evidence");

    auto restored = std::make_unique<FREQTRAudioProcessor>();
    TR::FreqUIV2::FreqBackendBindings restoredBackend(*restored);
    TR::SimpleUIV2::TRPresetManager restoredManager(
        TR::FreqUIV2::definition(), restoredBackend, staging);
    require(restoredManager.load(presetName).wasOk()
                && restored->modulationState() == presetState
                && std::abs(restored->apvts.getRawParameterValue(
                    FREQTRAudioProcessor::kParamJitter)->load()) <= 1.0e-7f
                && std::abs(restored->apvts.getRawParameterValue("mod_macro_1")->load()
                            - 1.0f) <= 1.0e-7f,
            "FREQ Jitter MATRIX preset did not round-trip exactly");
    std::ofstream presetProof(output.getChildFile("preset-verification.csv")
                                  .getFullPathName().toStdString());
    presetProof << "preset,native_jitter,macro_1,route_count,round_trip\n"
                << presetName << ",0,1," << presetState.routes.size() << ",1\n";
    return metrics.good() && trace.good() && manifest.good() && presetProof.good();
}

std::vector<float> renderCarrierParity(bool nativePath, float engine, float shadow,
                                       double sampleRate, int blockSize, int style = 1,
                                       float polarity = 1.0f, int hilbertMode = 0,
                                       float frequencyHz = 500.0f,
                                       bool hpEnabled = FREQTRAudioProcessor::kSidechainHpOnDefault,
                                       float hpHz = FREQTRAudioProcessor::kSidechainHpDefault,
                                       bool lpEnabled = FREQTRAudioProcessor::kSidechainLpOnDefault,
                                       float lpHz = FREQTRAudioProcessor::kSidechainLpDefault,
                                       float gainDb = FREQTRAudioProcessor::kSidechainGainDefault,
                                       float smooth = FREQTRAudioProcessor::kSidechainSmoothDefault,
                                       float bias = 0.0f, float rectify = 0.0f,
                                       float sidechainScale = 1.0f,
                                       float rmSpread = FREQTRAudioProcessor::kRmSpreadDefault,
                                       int rmModulator = FREQTRAudioProcessor::kRmModulatorTone)
{
    constexpr int totalSamples = 12288;
    auto processor = std::make_unique<FREQTRAudioProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    require(processor->isBusesLayoutSupported(layout) && processor->setBusesLayout(layout),
            "FREQ parity layout rejected");
    processor->prepareToPlay(sampleRate, blockSize);
    FreqNativeSidechainTestAccess::useNative(*processor, nativePath);
    using TR::Modulation::Tests::setNativeBaselineParameter;
    require(setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamSidechain,
                                       nativePath ? 1.0f : 0.0f)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamEngine, engine)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamFreq, frequencyHz)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamMix, 1.0f)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamStyle,
                                          static_cast<float>(style))
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamPolarity,
                                          polarity)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamHilbertMode,
                                          static_cast<float>(hilbertMode))
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamSidechainShadow, shadow)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamEngineBias, bias)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamEngineRectify, rectify)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamRmSpread, rmSpread)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamRmModulator,
                                          static_cast<float>(rmModulator))
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamSidechainGain,
                                          gainDb)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamSidechainSmooth,
                                          smooth)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamSidechainHpOn,
                                          hpEnabled ? 1.0f : 0.0f)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamSidechainHp, hpHz)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamSidechainLpOn,
                                          lpEnabled ? 1.0f : 0.0f)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamSidechainLp, lpHz),
            "FREQ parity parameters rejected");

    if (!nativePath)
    {
        auto state = TR::Modulation::makeDefaultState();
        auto& source = state.analysisSources[4];
        source.feature = TR::Modulation::AnalysisFeature::rmsEnvelope;
        source.detector.response = TR::Modulation::DetectorResponse::carrierTracking;
        source.detector.gainDb = gainDb;
        TR::Modulation::setLinkedSmooth(source.detector, smooth);
        source.detector.highPassEnabled = hpEnabled;
        source.detector.highPassHz = hpHz;
        source.detector.highPassSlope = 12;
        source.detector.lowPassEnabled = lpEnabled;
        source.detector.lowPassHz = lpHz;
        source.detector.lowPassSlope = 12;
        require(TR::Modulation::appendRoute(state, { 0, 0, true,
            TR::Modulation::SourceId::sidechainAnalysis(4), TR::Modulation::Polarity::unipolar,
            1.0f, "signal:carrier", TR::Modulation::SourceId::none(),
            TR::Modulation::Polarity::unipolar, TR::Modulation::makeLinearCurve(),
            TR::Modulation::makeLinearCurve() }) && processor->setModulationState(state),
            "FREQ parity carrier route rejected");
    }

    std::vector<float> rendered(static_cast<std::size_t>(totalSamples * 2));
    int offset = 0;
    while (offset < totalSamples)
    {
        const auto count = juce::jmin(blockSize, totalSamples - offset);
        juce::AudioBuffer<float> block(4, count);
        for (int sample = 0; sample < count; ++sample)
        {
            const auto absolute = offset + sample;
            const auto phase = juce::MathConstants<double>::twoPi * absolute / sampleRate;
            const auto saw = 0.08f * (2.0f * static_cast<float>(
                std::fmod(130.81 * absolute / sampleRate, 1.0)) - 1.0f);
            block.setSample(0, sample, saw);
            block.setSample(1, sample, 0.8f * saw);
            block.setSample(2, sample, sidechainScale * 0.55f * static_cast<float>(std::sin(997.0 * phase)));
            block.setSample(3, sample, sidechainScale * 0.40f * static_cast<float>(std::sin(1601.0 * phase + 0.31)));
        }
        juce::MidiBuffer midi;
        processor->processBlock(block, midi);
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel)
                rendered[static_cast<std::size_t>((offset + sample) * 2 + channel)] =
                    block.getSample(channel, sample);
        offset += count;
    }
    return rendered;
}

struct ComparisonMetrics
{
    double correlation = 0.0;
    double nullRms = 0.0;
    double changeRms = 0.0;
    float peakDelta = 0.0f;
};

ComparisonMetrics compareRender(const std::vector<float>& reference,
                                const std::vector<float>& candidate,
                                const std::vector<float>* changeReference = nullptr,
                                std::size_t warmup = 4096)
{
    require(reference.size() == candidate.size(), "FREQ evidence render size mismatch");
    require(warmup < reference.size(), "FREQ evidence warmup exceeds render size");
    double dot = 0.0, aEnergy = 0.0, bEnergy = 0.0, nullEnergy = 0.0, changeEnergy = 0.0;
    for (std::size_t index = warmup; index < reference.size(); ++index)
    {
        const auto a = reference[index];
        const auto b = candidate[index];
        const auto delta = a - b;
        dot += static_cast<double>(a) * b;
        aEnergy += static_cast<double>(a) * a;
        bEnergy += static_cast<double>(b) * b;
        nullEnergy += static_cast<double>(delta) * delta;
        if (changeReference != nullptr)
        {
            const auto change = b - (*changeReference)[index];
            changeEnergy += static_cast<double>(change) * change;
        }
    }
    const auto count = static_cast<double>(reference.size() - warmup);
    ComparisonMetrics result;
    result.correlation = dot / std::sqrt(aEnergy * bEnergy + 1.0e-30);
    result.nullRms = std::sqrt(nullEnergy / count);
    result.changeRms = std::sqrt(changeEnergy / count);
    for (std::size_t index = warmup; index < reference.size(); ++index)
        result.peakDelta = juce::jmax(result.peakDelta,
                                     std::abs(reference[index] - candidate[index]));
    return result;
}

void writeStereoWav(const std::vector<float>& interleaved, const juce::File& file,
                    double sampleRate)
{
    file.deleteFile();
    auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream());
    require(stream != nullptr, "FREQ evidence WAV could not be opened");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
        stream.release(), sampleRate, 2, 24, {}, 0));
    require(writer != nullptr, "FREQ evidence WAV writer could not be created");
    const auto frames = static_cast<int>(interleaved.size() / 2);
    juce::AudioBuffer<float> audio(2, frames);
    for (int frame = 0; frame < frames; ++frame)
        for (int channel = 0; channel < 2; ++channel)
            audio.setSample(channel, frame,
                            interleaved[static_cast<std::size_t>(frame * 2 + channel)]);
    require(writer->writeFromAudioSampleBuffer(audio, 0, frames),
            "FREQ evidence WAV could not be written");
}

std::vector<float> renderSpectralControl(float gainDb, float smooth);

void writeCarrierEvidence(const juce::File& output)
{
    require(output.createDirectory(), "FREQ evidence directory could not be created");
    std::ofstream parity(output.getChildFile("parity-matrix.csv").getFullPathName().toStdString());
    parity << "engine,shadow,frequency_hz,native_routed_correlation,null_rms,peak_delta,change_vs_25hz_rms\n";
    std::vector<float> frequencyReference;
    for (const auto engine : { 0.0f, 0.5f, 1.0f })
        for (const auto shadow : { 0.0f, 0.5f, 1.0f })
            for (const auto frequency : { 25.0f, 100.0f, 500.0f, 2000.0f, 10000.0f })
            {
                const auto native = renderCarrierParity(true, engine, shadow, 48000.0, 257,
                                                        1, 1.0f, 0, frequency);
                const auto routed = renderCarrierParity(false, engine, shadow, 48000.0, 257,
                                                        1, 1.0f, 0, frequency);
                if (engine == 1.0f && shadow == 1.0f && frequency == 25.0f)
                    frequencyReference = routed;
                const auto* change = engine == 1.0f && shadow == 1.0f
                    && frequency != 25.0f ? &frequencyReference : nullptr;
                const auto metrics = compareRender(native, routed, change);
                require(metrics.peakDelta == 0.0f, "FREQ evidence parity is not bit-exact");
                parity << engine << ',' << shadow << ',' << frequency << ','
                       << metrics.correlation << ',' << metrics.nullRms << ','
                       << metrics.peakDelta << ',' << metrics.changeRms << '\n';
            }

    std::ofstream filters(output.getChildFile("carrier-filter-response.csv").getFullPathName().toStdString());
    filters << "filter,cutoff_hz,native_routed_correlation,null_rms,peak_delta,change_vs_reference_rms\n";
    std::vector<float> hpReference, lpReference;
    for (const auto cutoff : { 20.0f, 250.0f, 1000.0f, 3000.0f })
    {
        const auto native = renderCarrierParity(true, 0.5f, 1.0f, 48000.0, 257,
                                                1, 1.0f, 0, 500.0f, true, cutoff, false, 20000.0f);
        const auto routed = renderCarrierParity(false, 0.5f, 1.0f, 48000.0, 257,
                                                1, 1.0f, 0, 500.0f, true, cutoff, false, 20000.0f);
        if (cutoff == 20.0f) hpReference = routed;
        const auto metrics = compareRender(native, routed, cutoff == 20.0f ? nullptr : &hpReference);
        filters << "HP," << cutoff << ',' << metrics.correlation << ',' << metrics.nullRms
                << ',' << metrics.peakDelta << ',' << metrics.changeRms << '\n';
        filters.flush();
        require(metrics.correlation >= 0.999999 && metrics.nullRms <= 1.0e-6
                    && metrics.peakDelta <= 2.0e-6f,
                "FREQ HP evidence exceeds the continuous-control epsilon gate");
    }
    for (const auto cutoff : { 20000.0f, 5000.0f, 1500.0f, 500.0f })
    {
        const auto native = renderCarrierParity(true, 0.5f, 1.0f, 48000.0, 257,
                                                1, 1.0f, 0, 500.0f, false, 20.0f, true, cutoff);
        const auto routed = renderCarrierParity(false, 0.5f, 1.0f, 48000.0, 257,
                                                1, 1.0f, 0, 500.0f, false, 20.0f, true, cutoff);
        if (cutoff == 20000.0f) lpReference = routed;
        const auto metrics = compareRender(native, routed, cutoff == 20000.0f ? nullptr : &lpReference);
        filters << "LP," << cutoff << ',' << metrics.correlation << ',' << metrics.nullRms
                << ',' << metrics.peakDelta << ',' << metrics.changeRms << '\n';
        filters.flush();
        require(metrics.correlation >= 0.999999 && metrics.nullRms <= 1.0e-6
                    && metrics.peakDelta <= 2.0e-6f,
                "FREQ LP evidence exceeds the continuous-control epsilon gate");
    }

    std::ofstream detector(output.getChildFile("carrier-detector-parity.csv")
                               .getFullPathName().toStdString());
    detector << "gain_db,smooth,correlation,null_rms,peak_delta\n";
    for (const auto gainDb : { -18.0f, 0.0f, 9.0f })
        for (const auto smooth : { 0.0f, 0.35f, 0.9f })
        {
            const auto native = renderCarrierParity(
                true, 1.0f, 0.75f, 48000.0, 257, 1, 1.0f, 0, 500.0f,
                false, 20.0f, false, 20000.0f, gainDb, smooth);
            const auto routed = renderCarrierParity(
                false, 1.0f, 0.75f, 48000.0, 257, 1, 1.0f, 0, 500.0f,
                false, 20.0f, false, 20000.0f, gainDb, smooth);
            const auto metrics = compareRender(native, routed);
            detector << gainDb << ',' << smooth << ',' << metrics.correlation << ','
                     << metrics.nullRms << ',' << metrics.peakDelta << '\n';
        }

    const auto spectralLowGain = renderSpectralControl(-30.0f, 0.0f);
    const auto spectralUnity = renderSpectralControl(0.0f, 0.0f);
    const auto spectralSmooth = renderSpectralControl(0.0f, 1.0f);
    std::ofstream spectral(output.getChildFile("sc3-spectral-control.csv")
                               .getFullPathName().toStdString());
    spectral << "block,gain_minus_30_smooth_0,gain_0_smooth_0,gain_0_smooth_1\n";
    for (std::size_t index = 0; index < spectralUnity.size(); ++index)
        spectral << index << ',' << spectralLowGain[index] << ',' << spectralUnity[index]
                 << ',' << spectralSmooth[index] << '\n';

    const auto nativeListen = renderCarrierParity(true, 1.0f, 1.0f, 48000.0, 257,
                                                   1, 1.0f, 0, 500.0f);
    const auto routedListen = renderCarrierParity(false, 1.0f, 1.0f, 48000.0, 257,
                                                   1, 1.0f, 0, 500.0f);
    const auto routedHp20 = renderCarrierParity(false, 0.5f, 1.0f, 48000.0, 257,
                                                 1, 1.0f, 0, 500.0f,
                                                 true, 20.0f, false, 20000.0f);
    const auto routedHp3000 = renderCarrierParity(false, 0.5f, 1.0f, 48000.0, 257,
                                                   1, 1.0f, 0, 500.0f,
                                                   true, 3000.0f, false, 20000.0f);
    writeStereoWav(nativeListen, output.getChildFile("legacy-native-fs-shadow100.wav"), 48000.0);
    writeStereoWav(routedListen, output.getChildFile("matrix-carrier-fs-shadow100.wav"), 48000.0);
    writeStereoWav(routedHp20, output.getChildFile("matrix-carrier-hp20.wav"), 48000.0);
    writeStereoWav(routedHp3000, output.getChildFile("matrix-carrier-hp3000.wav"), 48000.0);

    auto processor = std::make_unique<FREQTRAudioProcessor>();
    auto state = TR::Modulation::makeDefaultState();
    auto& source = state.analysisSources[4];
    source.feature = TR::Modulation::AnalysisFeature::rmsEnvelope;
    source.detector.response = TR::Modulation::DetectorResponse::carrierTracking;
    source.detector.highPassEnabled = true;
    source.detector.highPassHz = 80.0f;
    source.detector.lowPassEnabled = true;
    source.detector.lowPassHz = 12000.0f;
    require(TR::Modulation::appendRoute(state, { 0, 0, true,
                TR::Modulation::SourceId::sidechainAnalysis(4), TR::Modulation::Polarity::unipolar,
                1.0f, "signal:carrier", TR::Modulation::SourceId::none(),
                TR::Modulation::Polarity::unipolar, TR::Modulation::makeLinearCurve(),
                TR::Modulation::makeLinearCurve() }) && processor->setModulationState(state),
            "FREQ evidence preset carrier route rejected");
    TR::FreqUIV2::FreqBackendBindings backend(*processor);
    TR::SimpleUIV2::TRPresetManager manager(TR::FreqUIV2::definition(), backend);
    require(manager.saveAs("FREQ Sidechain Carrier Parity", true).wasOk(),
            "FREQ evidence preset could not be saved");
    const auto preset = manager.libraryFolder().getChildFile("FREQ Sidechain Carrier Parity.trpreset");
    require(preset.existsAsFile()
                && preset.copyFileTo(output.getChildFile(preset.getFileName())),
            "FREQ evidence preset could not be copied into evidence package");
}

void verifyCarrierParity()
{
    for (const auto engine : { 0.0f, 0.5f, 1.0f })
        for (const auto shadow : { 0.0f, 0.5f, 1.0f })
        {
            const auto native = renderCarrierParity(true, engine, shadow, 48000.0, 257);
            const auto routed = renderCarrierParity(false, engine, shadow, 48000.0, 257);
            double dot = 0.0, nativeEnergy = 0.0, routedEnergy = 0.0, nullEnergy = 0.0;
            float peakDelta = 0.0f;
            constexpr int warmupFrames = 2048;
            for (std::size_t index = static_cast<std::size_t>(warmupFrames * 2);
                 index < native.size(); ++index)
            {
                const auto a = native[index];
                const auto b = routed[index];
                const auto delta = a - b;
                dot += static_cast<double>(a) * b;
                nativeEnergy += static_cast<double>(a) * a;
                routedEnergy += static_cast<double>(b) * b;
                nullEnergy += static_cast<double>(delta) * delta;
                peakDelta = juce::jmax(peakDelta, std::abs(delta));
            }
            const auto correlation = dot / std::sqrt(nativeEnergy * routedEnergy + 1.0e-30);
            const auto nullRms = std::sqrt(nullEnergy
                / static_cast<double>(native.size() - warmupFrames * 2));
            std::cout << "FREQ carrier parity engine=" << engine << " shadow=" << shadow
                      << " correlation=" << correlation << " null_rms=" << nullRms
                      << " peak_delta=" << peakDelta << '\n';
            require(correlation >= 0.99, "FREQ routed carrier is below 99% native correlation");
        }
}

void verifyCarrierRateAndBlockParity()
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto blockSize : { 16, 64, 257, 1024 })
        {
            const auto native = renderCarrierParity(true, 1.0f, 0.5f, sampleRate, blockSize);
            const auto routed = renderCarrierParity(false, 1.0f, 0.5f, sampleRate, blockSize);
            float peakDelta = 0.0f;
            for (std::size_t index = 0; index < native.size(); ++index)
                peakDelta = juce::jmax(peakDelta, std::abs(native[index] - routed[index]));
            require(peakDelta == 0.0f,
                    "FREQ routed FS carrier is not bit-exact across sample rate/block matrix");
        }
    std::cout << "FREQ carrier sample-rate/block matrix is bit-exact\n";
}

void verifyCarrierTopologyParity()
{
    for (const auto style : { 0, 1, 2, 3 })
        for (const auto polarity : { -1.0f, 0.0f, 1.0f })
            for (const auto hilbertMode : { 0, 1 })
            {
                const auto native = renderCarrierParity(
                    true, 1.0f, 0.5f, 48000.0, 257, style, polarity, hilbertMode);
                const auto routed = renderCarrierParity(
                    false, 1.0f, 0.5f, 48000.0, 257, style, polarity, hilbertMode);
                float peakDelta = 0.0f;
                for (std::size_t index = 0; index < native.size(); ++index)
                    peakDelta = juce::jmax(peakDelta, std::abs(native[index] - routed[index]));
                require(peakDelta == 0.0f,
                        "FREQ routed carrier is not bit-exact across style/polarity/Hilbert matrix");
            }
    std::cout << "FREQ carrier topology matrix is bit-exact\n";
}

void verifyCarrierDetectorParity()
{
    for (const auto gainDb : { -18.0f, 0.0f, 9.0f })
        for (const auto smooth : { 0.0f, 0.35f, 0.9f })
        {
            const auto native = renderCarrierParity(
                true, 1.0f, 0.75f, 48000.0, 257, 1, 1.0f, 0, 500.0f,
                false, 20.0f, false, 20000.0f, gainDb, smooth);
            const auto routed = renderCarrierParity(
                false, 1.0f, 0.75f, 48000.0, 257, 1, 1.0f, 0, 500.0f,
                false, 20.0f, false, 20000.0f, gainDb, smooth);
            const auto metrics = compareRender(native, routed);
            std::cout << "FREQ carrier detector gain_db=" << gainDb
                      << " smooth=" << smooth
                      << " correlation=" << metrics.correlation
                      << " null_rms=" << metrics.nullRms
                      << " peak_delta=" << metrics.peakDelta << '\n';
            require(metrics.correlation >= 0.999999
                        && metrics.nullRms <= 1.0e-6
                        && metrics.peakDelta <= 2.0e-6f,
                    "FREQ SC4 GAIN/SMOOTH does not reproduce the legacy carrier path");
        }
    std::cout << "FREQ carrier GAIN/SMOOTH matrix passed the 2e-6 epsilon gate\n";
}

void verifyCarrierSourceContract()
{
    auto invalid = TR::Modulation::makeDefaultState();
    auto& spectral = invalid.analysisSources[3];
    spectral.feature = TR::Modulation::AnalysisFeature::spectralBandEnergy;
    spectral.detector.gainDb = 7.0f;
    TR::Modulation::setLinkedSmooth(spectral.detector, 0.63f);
    spectral.detector.highPassEnabled = true;
    spectral.detector.highPassHz = 311.0f;
    spectral.detector.lowPassEnabled = true;
    spectral.detector.lowPassHz = 7111.0f;
    require(TR::Modulation::appendRoute(invalid, { 0, 0, true,
                TR::Modulation::SourceId::sidechainAnalysis(3),
                TR::Modulation::Polarity::unipolar, 1.0f, "signal:carrier",
                TR::Modulation::SourceId::none(), TR::Modulation::Polarity::unipolar,
                TR::Modulation::makeLinearCurve(), TR::Modulation::makeLinearCurve() }),
            "FREQ invalid legacy route fixture could not be constructed");

    const std::vector<TR::Modulation::Runtime::DestinationDefinition> destinations {{
        "signal:carrier", 0.0f,
        TR::Modulation::Runtime::ModulationDomain::audioSignal,
        TR::Modulation::Runtime::DestinationTopology::linear,
        TR::Modulation::Runtime::SignalRepresentation::conditionedAudio
    }};
    const auto direct = TR::Modulation::Runtime::compileGraph(invalid, destinations);
    require(direct.ok && direct.unavailableRouteCount == 1
                && !direct.graph.routes[0].available,
            "Runtime accepted a scalar SC3 source as an audio-rate carrier");

    auto processor = std::make_unique<FREQTRAudioProcessor>();
    require(processor->setModulationState(invalid),
            "FREQ bridge rejected migration of the previous invalid carrier route");
    const auto migrated = processor->modulationState();
    require(migrated.routes.size() == 1
                && migrated.routes[0].source == TR::Modulation::SourceId::sidechainAnalysis(4),
            "FREQ bridge did not migrate the previous carrier route to SC4");
    const auto& carrier = migrated.analysisSources[4];
    require(carrier.detector.response == TR::Modulation::DetectorResponse::carrierTracking
                && carrier.feature == TR::Modulation::AnalysisFeature::rmsEnvelope
                && std::abs(carrier.detector.gainDb - 7.0f) <= 1.0e-6f
                && std::abs(carrier.detector.smooth - 0.63f) <= 1.0e-6f
                && carrier.detector.highPassEnabled
                && std::abs(carrier.detector.highPassHz - 311.0f) <= 1.0e-6f
                && carrier.detector.lowPassEnabled
                && std::abs(carrier.detector.lowPassHz - 7111.0f) <= 1.0e-6f,
            "FREQ carrier migration did not preserve detector controls");
    require(migrated.analysisSources[3].feature
                == TR::Modulation::AnalysisFeature::spectralBandEnergy
                && migrated.analysisSources[3].detector.response
                == TR::Modulation::DetectorResponse::control,
            "FREQ carrier migration corrupted the independent SC3 control profile");
    std::cout << "FREQ control/direct sidechain contract and migration passed\n";
}

std::vector<float> renderSpectralControl(float gainDb, float smooth)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    auto processor = std::make_unique<FREQTRAudioProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    require(processor->setBusesLayout(layout), "FREQ spectral-control layout rejected");
    processor->prepareToPlay(sampleRate, blockSize);
    auto state = TR::Modulation::makeDefaultState();
    auto& source = state.analysisSources[3];
    source.feature = TR::Modulation::AnalysisFeature::spectralBandEnergy;
    source.detector.gainDb = gainDb;
    TR::Modulation::setLinkedSmooth(source.detector, smooth);
    source.detector.highPassEnabled = true;
    source.detector.highPassHz = 700.0f;
    source.detector.lowPassEnabled = true;
    source.detector.lowPassHz = 1400.0f;
    require(TR::Modulation::appendRoute(state, { 0, 0, true,
                TR::Modulation::SourceId::sidechainAnalysis(3),
                TR::Modulation::Polarity::unipolar, 1.0f, "core:frequency",
                TR::Modulation::SourceId::none(), TR::Modulation::Polarity::unipolar,
                TR::Modulation::makeLinearCurve(), TR::Modulation::makeLinearCurve() })
                && processor->setModulationState(state),
            "FREQ SC3/SPECTRAL scalar route rejected");

    std::vector<float> trace;
    trace.reserve(96);
    double phase = 0.0;
    for (int blockIndex = 0; blockIndex < 96; ++blockIndex)
    {
        juce::AudioBuffer<float> block(4, blockSize);
        const auto amplitude = (blockIndex / 8) % 2 == 0 ? 0.01f : 0.6f;
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto sc = amplitude * static_cast<float>(std::sin(phase));
            phase += juce::MathConstants<double>::twoPi * 997.0 / sampleRate;
            block.setSample(0, sample, 0.03f);
            block.setSample(1, sample, -0.03f);
            block.setSample(2, sample, sc);
            block.setSample(3, sample, 0.8f * sc);
        }
        juce::MidiBuffer midi;
        processor->processBlock(block, midi);
        trace.push_back(processor->modulationTelemetry().sources[3].value);
    }
    return trace;
}

void verifySpectralDetectorControls()
{
    const auto lowGain = renderSpectralControl(-30.0f, 0.0f);
    const auto unityGain = renderSpectralControl(0.0f, 0.0f);
    const auto smoothed = renderSpectralControl(0.0f, 1.0f);
    double gainDelta = 0.0;
    double smoothDelta = 0.0;
    double unityEnergy = 0.0;
    for (std::size_t index = 0; index < unityGain.size(); ++index)
    {
        gainDelta += std::abs(unityGain[index] - lowGain[index]);
        smoothDelta += std::abs(unityGain[index] - smoothed[index]);
        unityEnergy += std::abs(unityGain[index]);
    }
    gainDelta /= static_cast<double>(unityGain.size());
    smoothDelta /= static_cast<double>(unityGain.size());
    unityEnergy /= static_cast<double>(unityGain.size());
    std::cout << "FREQ SC3/SPECTRAL controls mean_value=" << unityEnergy
              << " gain_delta=" << gainDelta
              << " smooth_delta=" << smoothDelta << '\n';
    require(unityEnergy > 1.0e-5 && gainDelta > 1.0e-4 && smoothDelta > 1.0e-4,
            "FREQ SC3/SPECTRAL GAIN or SMOOTH is operationally inert");
}

void verifyLegacyCarrierMigration()
{
    auto legacy = std::make_unique<FREQTRAudioProcessor>();
    require(TR::Modulation::Tests::setNativeBaselineParameter(
                legacy->apvts, FREQTRAudioProcessor::kParamSidechain, 1.0f)
            && TR::Modulation::Tests::setNativeBaselineParameter(
                legacy->apvts, FREQTRAudioProcessor::kParamSidechainSmooth, 0.61f),
            "FREQ legacy migration fixture parameters rejected");
    juce::MemoryBlock preset;
    legacy->getStateInformation(preset);
    auto restored = std::make_unique<FREQTRAudioProcessor>();
    restored->setStateInformation(preset.getData(), static_cast<int>(preset.getSize()));
    require(restored->routedCarrierConfigured(),
            "FREQ legacy sidechain state did not migrate to a carrier route");
    const auto state = restored->modulationState();
    require(state.analysisSources[4].detector.response
                == TR::Modulation::DetectorResponse::carrierTracking
            && std::abs(state.analysisSources[4].detector.smooth - 0.61f) <= 1.0e-6f,
            "FREQ legacy detector settings did not migrate to SIDECHAIN / DIRECT");
    require(restored->apvts.getRawParameterValue(FREQTRAudioProcessor::kParamSidechain)
                ->load(std::memory_order_relaxed) < 0.5f,
            "FREQ legacy sidechain toggle remained active after migration");
}

std::vector<float> renderRectify(float engine, float bias, float rectify)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 257;
    constexpr int totalSamples = 8192;
    auto processor = std::make_unique<FREQTRAudioProcessor>();
    processor->prepareToPlay(sampleRate, blockSize);
    using TR::Modulation::Tests::setNativeBaselineParameter;
    require(setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamEngine, engine)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamFreq, 137.0f)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamEngineBias, bias)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamEngineRectify, rectify)
            && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamMix, 1.0f),
            "FREQ RECTIFY fixture parameters rejected");
    std::vector<float> output(static_cast<std::size_t>(totalSamples * 2));
    int offset = 0;
    while (offset < totalSamples)
    {
        const auto count = juce::jmin(blockSize, totalSamples - offset);
        juce::AudioBuffer<float> block(processor->getTotalNumInputChannels(), count);
        for (int sample = 0; sample < count; ++sample)
        {
            const auto absolute = offset + sample;
            const auto phase = std::fmod(130.81 * absolute / sampleRate, 1.0);
            const auto saw = 0.15f * (2.0f * static_cast<float>(phase) - 1.0f);
            block.setSample(0, sample, saw);
            block.setSample(1, sample, 0.83f * saw);
        }
        juce::MidiBuffer midi;
        processor->processBlock(block, midi);
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel)
                output[static_cast<std::size_t>((offset + sample) * 2 + channel)] =
                    block.getSample(channel, sample);
        offset += count;
    }
    return output;
}

void verifyRectifySemantics()
{
    const auto amNeutral = renderRectify(0.0f, 0.0f, 0.0f);
    const auto amRectified = renderRectify(0.0f, 0.0f, 100.0f);
    const auto rmNegative = renderRectify(0.5f, 0.0f, -100.0f);
    const auto rmNeutral = renderRectify(0.5f, 0.0f, 0.0f);
    const auto rmPositive = renderRectify(0.5f, 0.0f, 100.0f);
    const auto fsNeutral = renderRectify(1.0f, 0.0f, 0.0f);
    const auto fsRectified = renderRectify(1.0f, 0.0f, 100.0f);
    const auto am = compareRender(amNeutral, amRectified);
    const auto positive = compareRender(rmNeutral, rmPositive);
    const auto negative = compareRender(rmNeutral, rmNegative);
    const auto opposite = compareRender(rmNegative, rmPositive);
    const auto fs = compareRender(fsNeutral, fsRectified);
    std::cout << "FREQ rectify semantics am_null=" << am.nullRms
              << " rm_positive_null=" << positive.nullRms
              << " rm_negative_null=" << negative.nullRms
              << " rm_opposite_correlation=" << opposite.correlation
              << " fs_null=" << fs.nullRms << '\n';
    require(am.nullRms == 0.0 && fs.nullRms <= 1.0e-5,
            "FREQ RECTIFY leaked outside the RM engine");
    require(positive.nullRms > 0.01 && negative.nullRms > 0.01
                && opposite.correlation < -0.99,
            "FREQ RECTIFY does not provide perceptible signed rectification in RM");

    for (const auto smooth : { 0.0f, 0.35f, 0.9f })
    {
        const auto positiveSidechain = renderCarrierParity(false, 0.5f, 0.0f, 48000.0, 257,
            1, 1.0f, 0, 500.0f, false, 20.0f, false, 20000.0f, 0.0f, smooth,
            0.0f, 100.0f, 1.0f);
        const auto rmscDry = renderCarrierParity(false, 0.5f, 0.0f, 48000.0, 257,
            1, 1.0f, 0, 500.0f, false, 20.0f, false, 20000.0f, 0.0f, smooth,
            100.0f, -100.0f, 0.0f);
        const auto rmsc = renderCarrierParity(false, 0.5f, 0.0f, 48000.0, 257,
            1, 1.0f, 0, 500.0f, false, 20.0f, false, 20000.0f, 0.0f, smooth,
            100.0f, -100.0f, 1.0f);
        auto expected = rmscDry;
        for (std::size_t index = 0; index < expected.size(); ++index)
            expected[index] -= positiveSidechain[index];
        const auto rmscMetrics = compareRender(expected, rmsc, nullptr, 20000);
        std::cout << "FREQ RMSC smooth=" << smooth
                  << " null_rms=" << rmscMetrics.nullRms
                  << " peak_delta=" << rmscMetrics.peakDelta << '\n';
        require(rmscMetrics.nullRms <= 2.0e-6 && rmscMetrics.peakDelta <= 2.0e-5f,
                "FREQ RMSC law is not input minus positive-rectified sidechain RM");
    }
}

std::vector<float> renderInternalRm(float spread, int modulator, float frequencyHz = 320.0f)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 257;
    constexpr int totalSamples = 48000;
    auto processor = std::make_unique<FREQTRAudioProcessor>();
    processor->prepareToPlay(sampleRate, blockSize);
    using TR::Modulation::Tests::setNativeBaselineParameter;
    require(setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamEngine, 0.5f)
                && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamFreq, frequencyHz)
                && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamMix, 1.0f)
                && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamStyle, 1.0f)
                && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamHarm, 0.0f)
                && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamRmSpread, spread)
                && setNativeBaselineParameter(processor->apvts, FREQTRAudioProcessor::kParamRmModulator,
                                              static_cast<float>(modulator)),
            "FREQ internal RM fixture parameters rejected");
    std::vector<float> output(static_cast<std::size_t>(totalSamples * 2));
    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const auto count = juce::jmin(blockSize, totalSamples - offset);
        juce::AudioBuffer<float> block(processor->getTotalNumInputChannels(), count);
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
            block.clear(channel, 0, count);
        for (int sample = 0; sample < count; ++sample)
        {
            block.setSample(0, sample, 0.2f);
            block.setSample(1, sample, 0.2f);
        }
        juce::MidiBuffer midi;
        processor->processBlock(block, midi);
        for (int sample = 0; sample < count; ++sample)
            for (int channel = 0; channel < 2; ++channel)
                output[static_cast<std::size_t>((offset + sample) * 2 + channel)] =
                    block.getSample(channel, sample);
    }
    return output;
}

void verifyRingModulatorOptions()
{
    const auto splitChannels = [] (const std::vector<float>& interleaved)
    {
        std::array<std::vector<float>, 2> channels;
        const auto frames = interleaved.size() / 2;
        for (auto& channel : channels) channel.reserve(frames);
        for (std::size_t frame = 4096; frame < frames; ++frame)
        {
            channels[0].push_back(interleaved[frame * 2]);
            channels[1].push_back(interleaved[frame * 2 + 1]);
        }
        return channels;
    };
    const auto correlation = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        double aa = 0.0, bb = 0.0, ab = 0.0;
        for (std::size_t index = 0; index < a.size(); ++index)
        {
            aa += (double) a[index] * a[index];
            bb += (double) b[index] * b[index];
            ab += (double) a[index] * b[index];
        }
        return ab / std::sqrt(juce::jmax(1.0e-30, aa * bb));
    };

    const auto centered = splitChannels(renderInternalRm(0.0f, FREQTRAudioProcessor::kRmModulatorTone));
    const auto spread = splitChannels(renderInternalRm(100.0f, FREQTRAudioProcessor::kRmModulatorTone));
    const auto centeredCorrelation = correlation(centered[0], centered[1]);
    const auto spreadCorrelation = correlation(spread[0], spread[1]);
    require(centeredCorrelation > 0.999999 && spreadCorrelation < -0.995,
            "FREQ RM SPREAD does not reach the measured +/-90-degree stereo law");

    const auto noiseA = splitChannels(renderInternalRm(100.0f, FREQTRAudioProcessor::kRmModulatorNoise));
    const auto noiseB = splitChannels(renderInternalRm(100.0f, FREQTRAudioProcessor::kRmModulatorNoise));
    require(compareRender(noiseA[0], noiseB[0]).peakDelta == 0.0f,
            "FREQ RM NOISE is not deterministic across fresh instances");
    const auto noiseStereoCorrelation = correlation(noiseA[0], noiseA[1]);
    require(std::abs(noiseStereoCorrelation) < 0.2,
            "FREQ RM NOISE channels are not independently decorrelated");

    const auto directNeutral = renderCarrierParity(false, 0.5f, 1.0f, 48000.0, 257,
        1, 1.0f, 0, 500.0f, false, 20.0f, false, 20000.0f, 0.0f, 0.35f,
        100.0f, -100.0f, 1.0f, 0.0f, FREQTRAudioProcessor::kRmModulatorTone);
    const auto directInternalOptions = renderCarrierParity(false, 0.5f, 1.0f, 48000.0, 257,
        1, 1.0f, 0, 500.0f, false, 20.0f, false, 20000.0f, 0.0f, 0.35f,
        100.0f, -100.0f, 1.0f, 100.0f, FREQTRAudioProcessor::kRmModulatorNoise);
    require(compareRender(directNeutral, directInternalOptions).peakDelta == 0.0f,
            "FREQ internal RM options leaked into SIDECHAIN / DIRECT");

    std::cout << "FREQ RM options centered_correlation=" << centeredCorrelation
              << " spread100_correlation=" << spreadCorrelation
              << " noise_stereo_correlation=" << noiseStereoCorrelation << '\n';
}
}

int main(int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        if (argc == 3 && juce::String(argv[1]) == "--qualify-jitter-host-matrix")
            return writeFreqJitterHostMatrix(juce::File(argv[2])) ? 0 : 3;
        if (argc == 3 && juce::String(argv[1]) == "--qualify-jitter-automation")
            return writeFreqJitterAutomationMatrix(juce::File(argv[2])) ? 0 : 4;
        if (argc == 3 && juce::String(argv[1]) == "--export-jitter-motion-evidence")
            return writeFreqJitterEvidence(juce::File(argv[2])) ? 0 : 2;
        verifyLegacyCarrierMigration();
        verifyCarrierParity();
        verifyCarrierRateAndBlockParity();
        verifyCarrierTopologyParity();
        verifyCarrierDetectorParity();
        verifyCarrierSourceContract();
        verifySpectralDetectorControls();
        verifyRectifySemantics();
        verifyRingModulatorOptions();
        if (argc == 3 && juce::String(argv[1]) == "--export-carrier-evidence")
        {
            writeCarrierEvidence(juce::File(argv[2]));
            std::cout << "FREQ carrier evidence package written\n";
            return 0;
        }
        if (argc == 3 && juce::String(argv[1]) == "--export-native-sidechain-baseline")
        {
            const auto renderProfile = [&](juce::String name, float shadow)
            {
                return TR::Modulation::Tests::exportNativeSidechainBaseline<FREQTRAudioProcessor>(
                    juce::File(argv[2]), std::move(name),
                    "carrier_l,carrier_r,freq_shift_l,freq_shift_r,rms,gate,depth", 7,
                    [](auto& processor) -> auto& { return processor.apvts; },
					[shadow](auto& processor, auto& state)
                    {
						FreqNativeSidechainTestAccess::useNative(processor, true);
                        using namespace TR::Modulation::Tests;
                        return setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamSidechain, 1.0f)
                            && setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamEngine, 1.0f)
                            && setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamFreq, 500.0f)
                            && setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamSidechainGain, 0.0f)
                            && setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamSidechainSmooth, 0.5f)
                            && setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamSidechainShadow, shadow);
                    },
                    [](const auto& processor, int sample, int, float* values)
                    { FreqNativeSidechainTestAccess::extract(processor, sample, values); });
            };
            const auto ok = renderProfile("FREQ-TR-shadow-0", 0.0f)
                && renderProfile("FREQ-TR", 0.5f)
                && renderProfile("FREQ-TR-shadow-100", 1.0f);
            return ok ? 0 : 2;
        }
		if (argc == 3 && juce::String(argv[1]) == "--export-shared-sidechain-baseline")
		{
			const auto renderProfile = [&](juce::String name, float shadow)
			{
				return TR::Modulation::Tests::exportNativeSidechainBaseline<FREQTRAudioProcessor>(
					juce::File(argv[2]), std::move(name),
					"conditioned_l,conditioned_r,control", 3,
					[](auto& processor) -> auto& { return processor.apvts; },
					[shadow](auto&, auto& state)
					{
						using namespace TR::Modulation::Tests;
						return setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamSidechain, 1.0f)
							&& setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamEngine, 1.0f)
							&& setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamFreq, 500.0f)
							&& setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamSidechainGain, 0.0f)
							&& setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamSidechainSmooth, 0.5f)
							&& setNativeBaselineParameter(state, FREQTRAudioProcessor::kParamSidechainShadow, shadow);
					},
					[](const auto& processor, int sample, int, float* values)
					{ FreqNativeSidechainTestAccess::extractShared(processor, sample, values); });
			};
			const auto ok = renderProfile("FREQ-TR-shadow-0", 0.0f)
				&& renderProfile("FREQ-TR", 0.5f)
				&& renderProfile("FREQ-TR-shadow-100", 1.0f);
			return ok ? 0 : 2;
		}
        {
            auto auditProcessor = std::make_unique<FREQTRAudioProcessor>();
            TR::FreqUIV2::FreqBackendBindings auditBackend(*auditProcessor);
            require(TR::Modulation::Tests::auditMotionRecipeBackend(
                        auditBackend, auditProcessor->apvts,
                        FREQTRAudioProcessor::kParamJitter, "native-jitter", 3, 5, 1).passed(),
                    "FREQ Jitter recipe UI/backend contract failed");
        }
        auto processor = std::make_unique<FREQTRAudioProcessor>();
        require(processor->acceptsMidi(), "FREQ does not advertise MIDI input");
        auto layout = processor->getBusesLayout();
        layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
        require(processor->isBusesLayoutSupported(layout) && processor->setBusesLayout(layout),
                "FREQ shared sidechain stereo layout rejected");
        auto monoLayout = layout;
        monoLayout.inputBuses.set(1, juce::AudioChannelSet::mono());
        require(processor->isBusesLayoutSupported(monoLayout),
                "FREQ shared sidechain mono layout rejected");
        processor->prepareToPlay(48000.0, 512);
        auto state = TR::Modulation::makeDefaultState();
        state.analysisSources[4].feature = TR::Modulation::AnalysisFeature::rmsEnvelope;
        state.analysisSources[4].detector.response =
            TR::Modulation::DetectorResponse::carrierTracking;
        TR::Modulation::setLinkedSmooth(state.analysisSources[4].detector, 0.0f);
        TR::Modulation::setLinkedSmooth(state.analysisSources[2].detector, 0.0f);
        TR::Modulation::setLinkedSmooth(state.analysisSources[3].detector, 0.0f);
        state.midiSources[static_cast<std::size_t>(TR::Modulation::MidiSourceType::note)].smoothingSeconds = 0.0f;
        require(TR::Modulation::appendRoute(state, { 0, 0, true,
            TR::Modulation::SourceId::midi(TR::Modulation::MidiSourceType::note),
            TR::Modulation::Polarity::unipolar, 1.0f, "macro:1", TR::Modulation::SourceId::none(),
            TR::Modulation::Polarity::unipolar, TR::Modulation::makeLinearCurve(), TR::Modulation::makeLinearCurve() }),
            "FREQ MIDI to Macro route rejected");
        require(TR::Modulation::appendRoute(state, { 0, 0, true, TR::Modulation::SourceId::macro(1),
            TR::Modulation::Polarity::unipolar, 1.0f, "core:frequency", TR::Modulation::SourceId::none(),
            TR::Modulation::Polarity::unipolar, TR::Modulation::makeLinearCurve(), TR::Modulation::makeLinearCurve() }),
            "FREQ Macro to Frequency route rejected");
        for (int source = 1; source <= 3; ++source)
            require(TR::Modulation::appendRoute(state, { 0, 0, true,
                source == 1 ? TR::Modulation::SourceId::sidechainEnvelope()
                            : TR::Modulation::SourceId::sidechainAnalysis(source),
                TR::Modulation::Polarity::unipolar, 1.0f, "core:feedback",
                TR::Modulation::SourceId::none(), TR::Modulation::Polarity::unipolar,
                TR::Modulation::makeLinearCurve(), TR::Modulation::makeLinearCurve() }),
                "FREQ shared Sidechain source route rejected");
        require(TR::Modulation::appendRoute(state, { 0, 0, true,
            TR::Modulation::SourceId::sidechainAnalysis(4),
            TR::Modulation::Polarity::unipolar, 1.0f, "signal:carrier",
            TR::Modulation::SourceId::none(), TR::Modulation::Polarity::unipolar,
            TR::Modulation::makeLinearCurve(), TR::Modulation::makeLinearCurve() }),
            "FREQ shared audio carrier route rejected");
        require(processor->setModulationState(state), "FREQ modulation state rejected");
        require(!TR::Modulation::Tests::setNativeBaselineParameter(
                    processor->apvts, FREQTRAudioProcessor::kParamSidechain, 0.0f)
                    || processor->apvts.getRawParameterValue(FREQTRAudioProcessor::kParamSidechain)
                        ->load(std::memory_order_relaxed) < 0.5f,
                "FREQ legacy sidechain toggle did not remain disabled");

        TR::FreqUIV2::FreqBackendBindings backend(*processor);
        const auto presetState = backend.readMusicalState();
        require(backend.validateMusicalState(presetState)
                    && presetState.textValues.count(TR::Modulation::Integration::presetStateId) == 1,
                "FREQ internal preset omitted modulation XML");
        require(backend.parameterSnapshot().count("mod_macro_1") == 1,
                "FREQ internal preset omitted Macro parameters");

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        editor->setVisible(true);
        juce::Timer::callPendingTimersSynchronously();
        auto* macrosButton = dynamic_cast<juce::Button*>(findById(*editor, "macros-panel-button"));
        auto* matrixButton = dynamic_cast<juce::Button*>(findById(*editor, "matrix-workspace-button"));
        auto* workspace = findById(*editor, "auxiliary-workspace");
        auto* harmControl = findById(*editor, "harm-control");
        auto* shadowControl = findById(*editor, "shadow-control");
        auto* legacySidechainButton = findById(*editor, "macros-sidechain-toggle");
        require(macrosButton != nullptr && matrixButton != nullptr
                    && workspace != nullptr && !workspace->isVisible(),
                "FREQ MACROS/MATRIX controls are missing");
        require(harmControl != nullptr && shadowControl != nullptr,
                "FREQ HARM/SHADOW contextual controls are missing");
        require(legacySidechainButton == nullptr || !legacySidechainButton->isShowing(),
                "FREQ exposed the retired product-local sidechain control");
        juce::Timer::callPendingTimersSynchronously();
        require(!harmControl->isShowing() && shadowControl->isShowing(),
                "FREQ carrier route did not switch contextual HARM to SHADOW");
        const auto productSize = juce::Point<int> { editor->getWidth(), editor->getHeight() };
        TR::Modulation::Tests::clickButton(*macrosButton);
        auto* compactPanel = findById(*editor, "macro-panel");
        require(compactPanel != nullptr && compactPanel->isShowing()
                    && !workspace->isVisible()
                    && editor->getWidth() == productSize.x + 200
                    && editor->getHeight() == productSize.y,
                "First MACROS click did not open the compact Macro panel");
        TR::Modulation::Tests::clickButton(*matrixButton);
        require(workspace->isVisible() && matrixButton->getToggleState(), "FREQ MATRIX workspace did not open");
        require(editor->getWidth() == 1040 && editor->getHeight() == 680,
                "FREQ MATRIX workspace did not request its canonical size");
        const auto journey = TR::Modulation::Tests::auditMacroJourney(workspace);
        require(journey.workspaceFound && journey.visible && journey.hasAllMacroCards
                    && journey.hasFocusTargets && journey.containerHasNoFocusRing
                    && journey.nameEditingContract,
                "FREQ MATRIX journey has complete cards and control-local focus");
        TR::Modulation::Tests::clickButton(*matrixButton);
        require(compactPanel->isShowing()
                    && editor->getWidth() == productSize.x + 200
                    && editor->getHeight() == productSize.y,
                "FREQ MATRIX did not restore the originating MACROS panel");

        process(*processor, true);
        require(FreqNativeSidechainTestAccess::routedCarrierIsValid(*processor),
                "FREQ Matrix carrier route did not reach the audio-rate DSP input");
        for (int block = 0; block < 32; ++block) process(*processor, false);
        float base = 0.0f, effective = 0.0f;
        require(processor->modulationDestinationValues("core:frequency", base, effective),
                "FREQ destination telemetry unavailable");
        require(processor->modulationTelemetry().destinationCount > 0,
                "FREQ workspace telemetry snapshot is empty");
        require(effective > base + 100.0f, "FREQ MIDI Macro route did not reach DSP destination");
        for (int source = 1; source <= 3; ++source)
        {
            process(*processor, false, 0.5f);
            const auto telemetry = processor->modulationTelemetry();
            require(telemetry.sources[source].available
                        && telemetry.sources[source].signalState
                            == TR::Modulation::Runtime::SourceSignalState::active,
                    "FREQ shared Sidechain source did not become active");
        }
        auto jitterRecipe = TR::FreqModulation::makeJitterParityRecipe(
            TR::Modulation::makeDefaultState());
        require(jitterRecipe.routes.size() == 5
                    && std::none_of(jitterRecipe.routes.begin(), jitterRecipe.routes.end(),
                        [](const auto& route) { return route.destinationId == "core:feedback"; }),
                "FREQ adaptive recipe topology is incomplete or targets feedback");
        {
            auto parity = std::make_unique<FREQTRAudioProcessor>();
            parity->prepareToPlay(48000.0, 512);
            using TR::Modulation::Tests::setNativeBaselineParameter;
            require(setNativeBaselineParameter(parity->apvts, FREQTRAudioProcessor::kParamFreq, 500.0f)
                        && setNativeBaselineParameter(parity->apvts, FREQTRAudioProcessor::kParamComb, 180.0f)
                        && setNativeBaselineParameter(parity->apvts, FREQTRAudioProcessor::kParamStyle, 3.0f)
                        && setNativeBaselineParameter(parity->apvts, FREQTRAudioProcessor::kParamJitter, 0.0f)
                        && setNativeBaselineParameter(parity->apvts, "mod_macro_1", 0.0f)
                        && parity->setModulationState(jitterRecipe),
                    "FREQ adaptive BIAS/COMB parity fixture setup failed");
            for (int block = 0; block < 96; ++block) process(*parity, false);
            require(setNativeBaselineParameter(parity->apvts, FREQTRAudioProcessor::kParamJitter, 1.0f)
                        && setNativeBaselineParameter(parity->apvts, "mod_macro_1", 1.0f),
                    "FREQ adaptive BIAS/COMB parity activation failed");
            process(*parity, false);
            constexpr int lastSample = 511;
            require(std::abs(FreqNativeSidechainTestAccess::nativeBiasJitter(*parity)
                             - FreqNativeSidechainTestAccess::matrixBiasJitter(
                                 *parity, lastSample)) <= 2.0e-5f,
                    "FREQ MATRIX BIAS Jitter does not reproduce the native raw composite law");
            require(std::abs(FreqNativeSidechainTestAccess::nativeCombOctave(*parity)
                             - FreqNativeSidechainTestAccess::matrixCombOctave(
                                 *parity, lastSample)) <= 2.0e-5f,
                    "FREQ MATRIX COMB Jitter does not reproduce the native octave law");
        }
        require(TR::Modulation::Tests::setNativeBaselineParameter(
                    processor->apvts, "mod_macro_1", 1.0f)
                    && processor->setModulationState(jitterRecipe),
                "FREQ adaptive Jitter recipe was rejected");
        float maximumCarrierMotion = 0.0f;
        for (int block = 0; block < 32; ++block)
        {
            process(*processor, false);
            require(processor->modulationDestinationValues(
                        "motion:carrier-deviation-l", base, effective),
                    "FREQ carrier-motion telemetry unavailable");
            maximumCarrierMotion = juce::jmax(maximumCarrierMotion, std::abs(effective));
        }
        auto motionTelemetry = processor->modulationTelemetry();
        const auto firstMotion = TR::Modulation::Runtime::additionalMotionSourceFirstIndex;
        require(maximumCarrierMotion > 1.0e-4f
                    && motionTelemetry.sources[firstMotion].motionReferenceAvailable
                    && motionTelemetry.sources[firstMotion].effectiveMotionRateHz > 0.0f,
                "FREQ adaptive sources did not consume the carrier-period reference");
        const auto lowFrequencyReference = motionTelemetry.sources[firstMotion].effectiveMotionReference;
        require(TR::Modulation::Tests::setNativeBaselineParameter(
                    processor->apvts, FREQTRAudioProcessor::kParamFreq, 4000.0f),
                "FREQ reference sweep parameter rejected");
        for (int block = 0; block < 32; ++block) process(*processor, false);
        motionTelemetry = processor->modulationTelemetry();
        require(motionTelemetry.sources[firstMotion].effectiveMotionReference
                    > lowFrequencyReference + 0.05f,
                "FREQ carrier frequency did not alter the procedural-rate reference");
        require(processor->modulationDestinationValues("core:feedback", base, effective)
                    && std::abs(effective - base) <= 1.0e-7f,
                "FREQ adaptive Jitter recipe changed feedback");
        require(processor->setModulationState(state),
                "FREQ could not restore its main smoke state after adaptive recipe proof");
        require(TR::Testing::writePluginCpuComparison (std::cout, "FREQ", *processor),
                "FREQ CPU comparison could not restore modulation state");

        juce::MemoryBlock preset; processor->getStateInformation(preset); editor.reset();
        auto restored = std::make_unique<FREQTRAudioProcessor>();
        restored->setStateInformation(preset.getData(), static_cast<int>(preset.getSize()));
        require(restored->modulationState().routes.size() == 6, "FREQ routes did not survive preset round-trip");
        require(restored->routedCarrierConfigured(),
                "FREQ carrier route did not survive preset round-trip");
        std::cout << "FREQ modulation smoke probe passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FREQ modulation smoke probe failed: " << error.what() << '\n'; return 1;
    }
}
