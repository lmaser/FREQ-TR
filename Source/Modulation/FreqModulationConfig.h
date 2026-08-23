#pragma once

#include "../../../TR-Shared/Modulation/Integration/TRParameterModulationBridge.h"

#include <vector>

namespace TR::FreqModulation
{
enum Destination : int
{
    frequency = 0,
    modulation,
    feedback,
    jitter,
    comb,
    engineBias,
    engineRectify,
    rmSpread,
    mix,
    carrier,
    carrierDeviationL,
    carrierDeviationR,
    combOctave,
    biasOffset,
    biasJitterL,
    biasJitterR,
    destinationCount
};

const std::vector<Modulation::Integration::ParameterDestination>& destinations();
Modulation::State makeJitterParityRecipe(Modulation::State, int macroOneBased = 1);
}
