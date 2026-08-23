#include "FreqV2EditorFactory.h"
#include "FreqBackendBindings.h"
#include "FreqUiDefinition.h"
#include "../Modulation/FreqModulationConfig.h"
#include "../../../TR-Shared/Modulation/UI/TRSimpleModulationWorkspace.h"
#include "../../../TR-Shared/SimpleUIV2/Runtime/SimpleEditorHost.h"

namespace TR::FreqUIV2
{
juce::AudioProcessorEditor* createEditor(FREQTRAudioProcessor& processor)
{
	std::vector<Modulation::UI::DestinationOption> destinations;
	int telemetryIndex = 0;
	for (const auto& descriptor : FreqModulation::destinations())
		destinations.push_back({ descriptor.id, descriptor.group, descriptor.label,
		                         true, {}, telemetryIndex++, descriptor.domain });
	auto backend = std::make_unique<FreqBackendBindings>(processor);
	auto& modulationBackend = *backend;
	auto modulation = std::make_unique<Modulation::UI::SimpleModulationWorkspace>(
		Modulation::UI::workspaceCallbacks(modulationBackend), std::move(destinations),
        modulationBackend.sidechainWorkspaceCallbacks());
	return new SimpleUIV2::SimpleEditorHost(
		processor, definition(), std::move(backend),
		std::move(modulation));
}
}
