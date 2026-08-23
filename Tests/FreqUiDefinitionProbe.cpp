#include "../Source/UIV2/FreqUiDefinition.h"

#include <iostream>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace V2 = TR::SimpleUIV2;

namespace
{
void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

const V2::SimplePageSpec& page(const V2::SimplePluginDefinition& definition, V2::TaskId task)
{
    for (const auto& candidate : definition.pages)
        if (candidate.taskId == task) return candidate;
    throw std::runtime_error("Missing task page");
}

const V2::SimpleGroupSpec& group(const V2::SimplePageSpec& source, const std::string& id)
{
    for (const auto& candidate : source.groups)
        if (candidate.groupId == id) return candidate;
    throw std::runtime_error("Missing group: " + id);
}

const V2::SimplePromptSpec& prompt(const V2::SimplePluginDefinition& definition,
                                   const std::string& id)
{
    for (const auto& candidate : definition.prompts)
        if (candidate.promptId == id) return candidate;
    throw std::runtime_error("Missing prompt: " + id);
}

void requireIds(const std::vector<V2::SimpleControlSpec>& controls,
                std::initializer_list<const char*> expected,
                const std::string& message)
{
    require(controls.size() == expected.size(), message);
    std::size_t index = 0;
    for (const auto* id : expected) require(controls[index++].controlId == id, message);
}
}

int main()
{
    try
    {
        const auto& definition = TR::FreqUIV2::definition();
        const auto issues = V2::validateDefinition(definition);
        if (V2::hasValidationErrors(issues))
        {
            for (const auto& issue : issues)
                std::cerr << issue.code << " at " << issue.path << " - " << issue.message << '\n';
            throw std::runtime_error("FREQ definition validation failed");
        }

        std::set<std::string> apvts, musicalState, preset, presetState, retired;
        for (const auto& item : definition.parameters)
        {
            if (item.domain == V2::StateDomain::musicalParameter) apvts.insert(item.parameterId);
            if (item.domain == V2::StateDomain::musicalState) musicalState.insert(item.parameterId);
        }
        preset.insert(definition.preset.parameterWhitelist.begin(), definition.preset.parameterWhitelist.end());
        presetState.insert(definition.preset.musicalStateWhitelist.begin(), definition.preset.musicalStateWhitelist.end());
        retired.insert(TR::FreqUIV2::retiredUiParameterIds().begin(), TR::FreqUIV2::retiredUiParameterIds().end());

		require(apvts.size() == 70, "Expected 70 musical APVTS parameters; got "
                    + std::to_string(apvts.size()));
		require(musicalState == std::set<std::string> { "midiDelayMs", "midiPort", "modulation_v1" },
				"Only MIDI session and modulation graph values should remain outside APVTS");
        require(preset == apvts, "Preset APVTS whitelist differs from musical definition");
        require(presetState == musicalState, "Preset musical-state whitelist differs from definition");
        require(retired.size() == 9, "Expected nine retired UI parameter IDs");

        requireIds(definition.macros,
                   { "macro-frequency", "macro-engine", "macro-feedback", "macro-mix" },
                   "Macro order must remain FREQ, ENGINE, FEEDBACK, MIX");
        require(definition.macros.front().parameterAlternatives == std::vector<std::string> { "freq_sync" },
                "FREQ macro must substitute synchronized divisions");
        require(definition.macros[1].promptId.empty() && definition.macros[1].inspectorId.empty(),
                "ENGINE macro must remain a direct gesture without a duplicate route");

        require(definition.pages.size() == 2, "FREQ must expose exactly MAIN and I/O");
        const auto& main = page(definition, V2::TaskId::core);
        require(main.label == "MAIN", "The primary FREQ workspace must be named MAIN");
        requireIds(group(main, "main-controls").controls,
                   { "mod-control", "harm-control", "shadow-control", "engine-bias-control",
                     "engine-rectify-control", "rm-spread-control", "rm-modulator-control",
                     "comb-control", "window-control",
                     "hilbert-mode-control", "polarity-control",
                     "style-control", "chaos-filter-control", "chaos-filter-amount",
                     "chaos-filter-speed", "chaos-delay-control", "chaos-delay-amount",
                     "chaos-delay-speed", "sync-control", "retrig-control", "midi-control" },
                   "MAIN causal order changed");
        require(definition.auxiliaryControls.empty(),
                "FREQ must not expose a parallel legacy SIDECHAIN control");
        const auto& mod = group(main, "main-controls").controls.front();
        require(mod.inlineToggle.has_value()
                    && mod.inlineToggle->parameterId == "mod_harm"
                    && mod.inlineToggle->label == "HARM",
                "MOD harmonic law must be an inline accessory of MOD");
        require(main.fixedActions.empty(), "FREQ MAIN must not retain a parameter footer");
        const auto& window = group(main, "main-controls").controls[8];
        require(window.enabledWhen.size() == 1
                    && window.enabledWhen.front().parameterId == "hilbert_mode"
                    && window.enabledWhen.front().comparison == V2::Comparison::equal
                    && window.enabledWhen.front().value == 0.0
                    && window.unavailableReason.find("Allpass") != std::string::npos,
                "WINDOW must be disabled, not removed, in Allpass mode");
        const auto& io = page(definition, V2::TaskId::io);
        requireIds(io.fixedActions,
                   { "filter-options-action", "routing-options-action", "latency-options-action" },
                   "I/O fixed utilities must remain FILTER, ROUTING, ALIGNMENT");
        require(definition.prompts.front().promptId == "latency-options"
                    && definition.prompts.front().parameterIds
                           == std::vector<std::string> { "align", "pdc", "max_window" },
                "DRY/WET ALIGN, HOST COMP and MAX WINDOW must remain in ALIGNMENT");
        requireIds(definition.prompts.front().controls,
                   { "compensated-alignment-control", "align-control", "pdc-control",
                     "max-window-control" },
                   "ALIGNMENT must lead with the shared mode before its advanced controls");
        const auto& alignmentMode = definition.prompts.front().controls.front();
        require(alignmentMode.compositePair.has_value()
                    && alignmentMode.choiceLabels
                           == std::vector<std::string> { "LIVE", "COMPENSATED" },
                "FREQ lost the shared compensated-alignment contract");
        const auto& filter = prompt(definition, "filter-options");
        requireIds(filter.controls,
                   { "prompt-filter-hp", "prompt-filter-lp", "prompt-filter-tilt",
                     "prompt-filter-position" },
                   "Filter prompt must use two composed band rows");
        require(filter.controls[0].inlineToggle->parameterId == "filter_hp_on"
                    && filter.controls[0].inlineChoice->parameterId == "filter_hp_slope"
                    && filter.controls[1].inlineToggle->parameterId == "filter_lp_on"
                    && filter.controls[1].inlineChoice->parameterId == "filter_lp_slope",
                "Filter bands lost enable or slope access");
        require(std::none_of(definition.prompts.begin(), definition.prompts.end(), [](const auto& item)
                { return item.promptId == "sidechain-options"; }),
                "FREQ must edit sidechain conditioning in Matrix, not a legacy prompt");
        for (const auto* id : { "sidechain", "sidechain_gain", "sidechain_smooth",
                               "sidechain_hp", "sidechain_lp", "sidechain_hp_on",
                               "sidechain_lp_on", "sidechain_hp_slope", "sidechain_lp_slope" })
        {
            const auto found = std::find_if(definition.parameters.begin(), definition.parameters.end(),
                [id](const auto& item) { return item.parameterId == id; });
            require(found != definition.parameters.end()
                        && found->access == V2::ParameterAccess::backendOnly,
                    "Legacy FREQ sidechain parameter escaped backend-only compatibility");
        }
        requireIds(group(io, "io-levels").controls,
                   { "input-control", "output-control" },
                   "INPUT and OUTPUT must remain one consecutive LEVELS pair");
        requireIds(group(io, "io-image").controls, { "pan-control" }, "I/O image group changed");
        requireIds(group(io, "io-mix").controls, { "mix-mode-control", "dry-level-control" },
                   "I/O mix group changed");
        requireIds(group(io, "io-limiter").controls,
                   { "lim-mode-control", "lim-quality-control", "lim-threshold-control" },
                   "I/O limiter group changed");

		std::cout << "FREQ human MAIN definition passed: 70 APVTS + 3 musical state, 2 workspaces.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FREQ UI V2 definition failed: " << exception.what() << '\n';
        return 1;
    }
}
