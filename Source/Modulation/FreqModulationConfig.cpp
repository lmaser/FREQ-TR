#include "FreqModulationConfig.h"

#include "../PluginProcessor.h"
#include "../../../TR-Shared/Modulation/Recipes/TRAdaptiveMotionRecipe.h"
#include "../../../TR-Shared/Modulation/Recipes/TRMotionRecipeUtilities.h"

namespace TR::FreqModulation
{
const std::vector<Modulation::Integration::ParameterDestination>& destinations()
{
    static const std::vector<Modulation::Integration::ParameterDestination> result {
        { "core:frequency", "CORE", "FREQUENCY", FREQTRAudioProcessor::kParamFreq,
          FREQTRAudioProcessor::kFreqMin, FREQTRAudioProcessor::kFreqMax, true, 0.01f },
        { "core:mod", "CORE", "MOD", FREQTRAudioProcessor::kParamMod,
          FREQTRAudioProcessor::kModMin, FREQTRAudioProcessor::kModMax, false, 0.02f },
        { "core:feedback", "CORE", "FEEDBACK", FREQTRAudioProcessor::kParamFeedback,
          FREQTRAudioProcessor::kFeedbackMin, FREQTRAudioProcessor::kFeedbackMax, false, 0.01f },
        { "core:jitter", "CORE", "JITTER", FREQTRAudioProcessor::kParamJitter,
          FREQTRAudioProcessor::kJitterMin, FREQTRAudioProcessor::kJitterMax, false, 0.02f },
        { "core:comb", "CORE", "COMB", FREQTRAudioProcessor::kParamComb,
          FREQTRAudioProcessor::kCombEffectiveMin, FREQTRAudioProcessor::kCombMax, true, 0.02f },
        { "engine:bias", "ENGINE", "BIAS", FREQTRAudioProcessor::kParamEngineBias,
          FREQTRAudioProcessor::kEngineBiasMin, FREQTRAudioProcessor::kEngineBiasMax, false, 0.02f },
        { "engine:rectify", "ENGINE", "RECTIFY", FREQTRAudioProcessor::kParamEngineRectify,
          FREQTRAudioProcessor::kEngineRectifyMin, FREQTRAudioProcessor::kEngineRectifyMax, false, 0.02f },
        { "engine:spread", "ENGINE", "SPREAD", FREQTRAudioProcessor::kParamRmSpread,
          FREQTRAudioProcessor::kRmSpreadMin, FREQTRAudioProcessor::kRmSpreadMax, false, 0.02f },
        { "core:mix", "CORE", "MIX", FREQTRAudioProcessor::kParamMix,
          FREQTRAudioProcessor::kMixMin, FREQTRAudioProcessor::kMixMax, false, 0.01f },
        { "signal:carrier", "SIGNAL", "DIRECT", "",
          0.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::audioSignal,
          Modulation::Runtime::SignalRepresentation::conditionedAudio }
        ,{ "motion:carrier-deviation-l", "MOTION", "CARRIER L", "",
          -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 0 }
        ,{ "motion:carrier-deviation-r", "MOTION", "CARRIER R", "",
          -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 1 }
        ,{ "motion:comb-octave", "MOTION", "COMB", "",
          -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 2 }
        ,{ "motion:bias-offset", "MOTION", "BIAS", "",
          -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 3 }
        ,{ "motion:bias-jitter-l", "MOTION", "BIAS JITTER L", "",
          -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 0 }
        ,{ "motion:bias-jitter-r", "MOTION", "BIAS JITTER R", "",
          -1.0f, 1.0f, false, 0.0f,
          Modulation::Runtime::ModulationDomain::sampleControl,
          Modulation::Runtime::SignalRepresentation::control, true, 0.0f, 1 }
    };
    return result;
}

Modulation::State makeJitterParityRecipe(Modulation::State state, int macroOneBased)
{
    using namespace Modulation::Recipes;
    macroOneBased = juce::jlimit(1, Modulation::macroCount, macroOneBased);
    removeRoutesTo(state, { "motion:carrier-deviation-l", "motion:carrier-deviation-r",
                            "motion:comb-octave", "motion:bias-jitter-l",
                            "motion:bias-jitter-r" });
    state.macros[static_cast<std::size_t>(macroOneBased - 1)].name = "JITTER DEPTH";
    AdaptiveResponseSourceConfig carrier;
    carrier.seed = 0x465245514a495431ull;
    carrier.lanePolicy = Modulation::MotionLanePolicy::stereo;
    carrier.initialPhase = 0.113f;
    carrier.lanePhaseOffset = 0.504f;
    carrier.referenceMaximumMs = 100000.0f;
    carrier.rateMinimumMs = 4.0f;
    carrier.rateMaximumMs = 500.0f;
    carrier.rateCompression = 0.80f;
    carrier.outputScale = 4.0f;
    configureAdaptiveResponseSource(state, 2, macroOneBased, carrier);

    auto bias = carrier;
    bias.outputScale = 0.040f;
    bias.applyDelayDepth = false;
    bias.outputAmountPower = 1.0f;
    configureAdaptiveResponseSource(state, 3, macroOneBased, bias);

    AdaptiveResponseSourceConfig comb;
    comb.reference = Modulation::MotionRateReference::secondaryPeriod;
    comb.seed = 0x465245514a495443ull;
    comb.lanePolicy = Modulation::MotionLanePolicy::linked;
    comb.initialPhase = 0.827f;
    comb.laneOffset = 3;
    configureAdaptiveResponseSource(state, 5, macroOneBased, comb);

    appendAdaptiveResponseRoute(state, 2, "motion:carrier-deviation-l");
    appendAdaptiveResponseRoute(state, 2, "motion:carrier-deviation-r");
    appendAdaptiveResponseRoute(state, 5, "motion:comb-octave");
    appendAdaptiveResponseRoute(state, 3, "motion:bias-jitter-l");
    appendAdaptiveResponseRoute(state, 3, "motion:bias-jitter-r");
    return state;
}
}
