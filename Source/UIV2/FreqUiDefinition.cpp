#include "FreqUiDefinition.h"

#include <utility>

namespace TR::FreqUIV2
{
namespace V2 = SimpleUIV2;

namespace
{
std::string tooltipFor(const std::string& parameter, const std::string& label)
{
    const std::pair<const char*, const char*> descriptions[] {
        { "freq", "Carrier frequency" }, { "engine", "Blend continuously between AM, RM and frequency shift" },
        { "feedback", "Recursive processed-signal feedback" }, { "mix", "Dry and processed signal balance" },
        { "mod", "Carrier frequency multiplier" },
        { "mod_harm", "Quantize MOD to harmonic and subharmonic ratios" },
        { "jitter", "Carrier and comb-delay frequency variation; feedback gain is unchanged" },
        { "harm", "Internal carrier harmonic density" }, { "sidechain_shadow", "Internal carrier retained with sidechain" },
        { "polarity", "Carrier frequency magnitude and direction" }, { "style", "Stereo processing topology" },
        { "comb", "Feedback delay frequency" },
        { "window", "Linear FIR window: 128/256 legacy, 512 fast, 1024 production recommended, 2048 high quality" },
        { "sync", "Use host-relative carrier frequency" }, { "retrig", "Lock synchronized carrier phase to transport" },
        { "midi", "Control carrier frequency from MIDI notes" }, { "align", "Align frequency-shift processing paths" },
        { "pdc", "Report Hilbert latency to the host" }, { "sidechain", "Use external sidechain modulation" },
		{ "rm_spread", "Stereo phase spread for the internal ring-modulator" },
		{ "rm_modulator", "Choose the internal ring-modulator tone or filtered noise source" },
        { "input", "Level entering the modulation engine" }, { "output", "Final plugin output level" },
        { "pan", "Wet-signal stereo position" }, { "mix_mode", "Choose insert or send signal flow" },
        { "lim_mode", "Limiter position in the signal path" },
        { "lim_quality", "Limiter processing quality" }, { "filter_pos", "Filter and tilt position around processing" }
    };
    for (const auto& [id, text] : descriptions)
        if (parameter == id) return text;
    if (label == "ENGINE OPTIONS") return "Open engine bias and rectify controls";
    if (label == "ALIGNMENT") return "Open alignment, host compensation and maximum window controls";
    if (label == "FILTER OPTIONS") return "Open wet-path filter and tilt controls";
    if (label == "ROUTING") return "Open input, output and polarity routing";
    return label;
}

void addParameter(V2::SimplePluginDefinition& d, std::string id, V2::ParameterAccess access,
                  std::string target = {}, V2::StateDomain domain = V2::StateDomain::musicalParameter,
                  std::string backendJustification = {})
{
    const auto stableId = id;
    d.parameters.push_back({ std::move(id), domain, access, std::move(target),
                             std::move(backendJustification) });
    if (domain == V2::StateDomain::musicalParameter)
        d.preset.parameterWhitelist.push_back(stableId);
    else if (domain == V2::StateDomain::musicalState)
        d.preset.musicalStateWhitelist.push_back(stableId);
}

V2::SimpleControlSpec control(std::string id, std::string parameter, std::string label,
                              V2::ControlRole role = V2::ControlRole::knob)
{
    V2::SimpleControlSpec result;
    result.controlId = std::move(id);
    result.parameterId = std::move(parameter);
    result.label = std::move(label);
    result.role = role;
    result.tooltip = tooltipFor(result.parameterId, result.label);
    return result;
}

V2::SimpleControlSpec formatted(V2::SimpleControlSpec result, int decimals, double scale,
                                std::string suffix, double offset = 0.0)
{
    result.valueFormat = { true, decimals, scale, std::move(suffix), offset };
    return result;
}

V2::SimpleControlSpec frequency(V2::SimpleControlSpec result)
{
    result.valueFormat = { true, 1, 1.0, "", 0.0, V2::ValueStyle::frequency };
    return result;
}

V2::SimpleGroupSpec hiddenGroup(std::string id, std::vector<V2::SimpleControlSpec> controls,
                                unsigned depth = 0)
{
    return { std::move(id), {}, std::move(controls), {}, depth, V2::GroupLabelVisibility::hidden };
}

V2::SimpleGroupSpec group(std::string id, std::string label, std::vector<V2::SimpleControlSpec> controls)
{
    return { std::move(id), std::move(label), std::move(controls), {}, 0,
             V2::GroupLabelVisibility::automatic };
}

void addCommonIoParameters(V2::SimplePluginDefinition& d)
{
    for (const auto* id : { "input", "output", "pan", "mix_mode", "dry_level", "wet_level",
                            "lim_mode", "lim_quality", "lim_threshold" })
        addParameter(d, id, V2::ParameterAccess::direct);
    for (const auto* id : { "filter_hp_on", "filter_hp_freq", "filter_hp_slope", "filter_lp_on",
                            "filter_lp_freq", "filter_lp_slope", "tilt", "filter_pos" })
        addParameter(d, id, V2::ParameterAccess::prompt, "filter-options");
    for (const auto* id : { "mode_in", "mode_out", "sum_bus", "inv_pol", "inv_str" })
        addParameter(d, id, V2::ParameterAccess::prompt, "routing-options");
}

V2::SimplePluginDefinition buildDefinition()
{
    V2::SimplePluginDefinition d;
	d.product = { "com.tr.audio.freq", "FREQ-TR", "1.4.0", "https://github.com/lmaser/FREQ-TR/issues" };
	d.capabilities = { false, true, true, true };
	for (int macro = 1; macro <= 8; ++macro)
	{
		const auto id = "mod_macro_" + std::to_string(macro);
		addParameter(d, id, V2::ParameterAccess::backendOnly, {},
		             V2::StateDomain::musicalParameter,
		             "Automatable Macro value exposed by the shared MACROS workspace.");
		d.preset.missingParameterDefaults.push_back({ id, 0.0 });
	}
	addParameter(d, "modulation_v1", V2::ParameterAccess::backendOnly, {},
	             V2::StateDomain::musicalState,
	             "Macro names, routes, source settings and transfer curves.");
	addParameter(d, "carrier_route_active", V2::ParameterAccess::backendOnly, {},
	             V2::StateDomain::runtimeDerived,
	             "Non-persistent UI snapshot of the shared carrier-route state.");
	d.preset.missingMusicalStateDefaults.push_back({ "modulation_v1", 0.0 });

	for (const auto* id : { "freq", "freq_sync", "engine", "feedback", "mix" })
        addParameter(d, id, V2::ParameterAccess::direct);
    auto freq = frequency(control("macro-frequency", "freq", "FREQ", V2::ControlRole::macro));
    freq.parameterAlternatives = { "freq_sync" };
    auto engine = formatted(control("macro-engine", "engine", "ENGINE", V2::ControlRole::macro),
                            1, 100.0, "%");
    auto mixMacro = formatted(control("macro-mix", "mix", "MIX", V2::ControlRole::macro),
                              1, 100.0, "%");
    mixMacro.parameterAlternatives = { "wet_level" };
    d.macros = {
        std::move(freq),
        std::move(engine),
        formatted(control("macro-feedback", "feedback", "FEEDBACK", V2::ControlRole::macro), 1, 100.0, "%"),
        std::move(mixMacro)
    };

    for (const auto* id : { "mod", "harm", "sidechain_shadow", "polarity", "style",
                            "engine_bias", "engine_focus", "rm_spread", "rm_modulator" })
        addParameter(d, id, V2::ParameterAccess::direct);
    addParameter(d, "jitter", V2::ParameterAccess::backendOnly, {},
                 V2::StateDomain::musicalParameter,
                 "Legacy Jitter parameter retained for presets and host automation; new editing uses a MACROS motion recipe.");
    addParameter(d, "mod_harm", V2::ParameterAccess::direct);
    auto modulation = control("mod-control", "mod", "MOD");
    modulation.inlineToggle = V2::SimpleControlSpec::InlineToggleSpec {
        "mod_harm", "HARM", "Use harmonic steps for MOD",
        V2::StateDomain::musicalParameter
    };
    auto harm = formatted(control("harm-control", "harm", "HARM"), 1, 100.0, "%");
    harm.visibleWhen.push_back({ "carrier_route_active", V2::Comparison::falsy, 0.0 });
    auto shadow = formatted(control("shadow-control", "sidechain_shadow", "SHADOW"), 1, 100.0, "%");
    shadow.visibleWhen.push_back({ "carrier_route_active", V2::Comparison::truthy, 0.0 });
    auto style = control("style-control", "style", "STYLE", V2::ControlRole::choice);
    style.choiceLabels = { "MONO", "STEREO", "WIDE", "DUAL" };
    style.choicePresentation = V2::ChoicePresentation::rail;

    for (const auto* id : { "comb", "window", "hilbert_mode", "chaos", "chaos_d" })
        addParameter(d, id, V2::ParameterAccess::direct);
    addParameter(d, "max_window", V2::ParameterAccess::prompt, "latency-options");
    for (const auto* id : { "chaos_amt_filter", "chaos_spd_filter" })
        addParameter(d, id, V2::ParameterAccess::direct);
    for (const auto* id : { "chaos_amt", "chaos_spd" })
        addParameter(d, id, V2::ParameterAccess::direct);

    auto window = formatted(control("window-control", "window", "WINDOW"), 0, 1.0, "");
    window.visibleWhen.push_back({ "engine", V2::Comparison::greater, 0.5 });
    window.enabledWhen.push_back({ "hilbert_mode", V2::Comparison::equal, 0.0 });
    window.unavailableReason =
        "Window controls the Linear FIR path; Allpass uses its sample-rate-designed IIR core";
    auto chaosFilter = control("chaos-filter-control", "chaos", "CHAOS FILTER", V2::ControlRole::toggle);
    auto chaosDelay = control("chaos-delay-control", "chaos_d", "CHAOS DELAY", V2::ControlRole::toggle);
    for (const auto* id : { "sync", "retrig", "midi" })
        addParameter(d, id, V2::ParameterAccess::direct);
    addParameter(d, "sidechain", V2::ParameterAccess::backendOnly, {},
                 V2::StateDomain::musicalParameter,
                 "Legacy enable flag read only during preset migration to a shared carrier route.");
    addParameter(d, "align", V2::ParameterAccess::prompt, "latency-options");
    addParameter(d, "pdc", V2::ParameterAccess::prompt, "latency-options");
    addParameter(d, "midiPort", V2::ParameterAccess::prompt, "midi-options", V2::StateDomain::musicalState);
    addParameter(d, "midiDelayMs", V2::ParameterAccess::prompt, "midi-options", V2::StateDomain::musicalState);
    for (const auto* id : { "sidechain_gain", "sidechain_smooth", "sidechain_hp", "sidechain_lp",
                            "sidechain_hp_on", "sidechain_lp_on", "sidechain_hp_slope", "sidechain_lp_slope" })
        addParameter(d, id, V2::ParameterAccess::backendOnly, {},
                     V2::StateDomain::musicalParameter,
                     "Legacy sidechain sub-parameter retained for preset and automation compatibility; active control migrated to MACROS workspace.");
    auto retrig = control("retrig-control", "retrig", "RETRIG", V2::ControlRole::toggle);
    retrig.enabledWhen.push_back({ "sync", V2::Comparison::truthy, 0.0 });
    retrig.unavailableReason = "Enable SYNC first";
    auto midi = control("midi-control", "midi", "MIDI", V2::ControlRole::toggle);
    midi.promptId = "midi-options";
    auto alignmentMode = V2::makeCompensatedAlignmentControl();
    auto align = control("align-control", "align", "DRY/WET ALIGN", V2::ControlRole::toggle);
    auto pdc = control("pdc-control", "pdc", "HOST COMP", V2::ControlRole::toggle);

    auto engineBias = formatted(control("engine-bias-control", "engine_bias", "BIAS"), 1, 1.0, "%");
    auto engineRectify = formatted(control("engine-rectify-control", "engine_focus", "RECTIFY"), 1, 1.0, "%");
	auto rmSpread = formatted(control("rm-spread-control", "rm_spread", "SPREAD"), 1, 1.0, "%");
	rmSpread.visibleWhen.push_back({ "engine", V2::Comparison::greater, 0.25 });
	rmSpread.visibleWhen.push_back({ "engine", V2::Comparison::less, 0.75 });
	rmSpread.visibleWhen.push_back({ "carrier_route_active", V2::Comparison::falsy, 0.0 });
	auto rmModulator = control("rm-modulator-control", "rm_modulator", "MODULATOR",
	                          V2::ControlRole::choice);
	rmModulator.choiceLabels = { "TONE", "NOISE" };
	rmModulator.choicePresentation = V2::ChoicePresentation::rail;
	rmModulator.visibleWhen = rmSpread.visibleWhen;
    auto hilbertMode = control("hilbert-mode-control", "hilbert_mode", "HILBERT MODE",
                               V2::ControlRole::choice);
    hilbertMode.choiceLabels = { "LINEAR", "ALLPASS" };
    hilbertMode.choicePresentation = V2::ChoicePresentation::rail;
    hilbertMode.visibleWhen.push_back({ "engine", V2::Comparison::greater, 0.5 });
    auto maxWindow = formatted(control("max-window-control", "max_window", "MAX WINDOW"), 0, 1.0, "");

    auto chaosFilterAmount = formatted(
        control("chaos-filter-amount", "chaos_amt_filter", "FILTER AMOUNT"), 1, 1.0, "%");
    chaosFilterAmount.visibleWhen.push_back({ "chaos", V2::Comparison::truthy, 0.0 });
    auto chaosFilterSpeed = frequency(
        control("chaos-filter-speed", "chaos_spd_filter", "FILTER SPEED"));
    chaosFilterSpeed.visibleWhen.push_back({ "chaos", V2::Comparison::truthy, 0.0 });
    auto chaosDelayAmount = formatted(
        control("chaos-delay-amount", "chaos_amt", "DELAY AMOUNT"), 1, 1.0, "%");
    chaosDelayAmount.visibleWhen.push_back({ "chaos_d", V2::Comparison::truthy, 0.0 });
    auto chaosDelaySpeed = frequency(
        control("chaos-delay-speed", "chaos_spd", "DELAY SPEED"));
    chaosDelaySpeed.visibleWhen.push_back({ "chaos_d", V2::Comparison::truthy, 0.0 });

    V2::SimplePageSpec main { V2::TaskId::core, "MAIN", {
        hiddenGroup("main-controls", {
            modulation,
            harm,
            shadow,
            engineBias,
            engineRectify,
			rmSpread,
			rmModulator,
            frequency(control("comb-control", "comb", "COMB")),
            window,
            hilbertMode,
            formatted(control("polarity-control", "polarity", "POLARITY"), 2, 1.0, ""),
            style,
            chaosFilter,
            chaosFilterAmount,
            chaosFilterSpeed,
            chaosDelay,
            chaosDelayAmount,
            chaosDelaySpeed,
            control("sync-control", "sync", "SYNC", V2::ControlRole::toggle),
            retrig,
            midi
        })
    } };

    addCommonIoParameters(d);
    auto filterAction = control("filter-options-action", {}, "FILTER OPTIONS", V2::ControlRole::action);
    filterAction.domain = V2::StateDomain::uiInstance;
    filterAction.promptId = "filter-options";
    auto routingAction = V2::makeCanonicalRoutingAction();
    auto latencyAction = control("latency-options-action", {}, "ALIGNMENT", V2::ControlRole::action);
    latencyAction.domain = V2::StateDomain::uiInstance;
    latencyAction.promptId = "latency-options";
    auto input = formatted(control("input-control", "input", "INPUT", V2::ControlRole::fader), 1, 1.0, " dB");
    input.meterSource = V2::MeterSource::input;
    auto output = formatted(control("output-control", "output", "OUTPUT", V2::ControlRole::fader), 1, 1.0, " dB");
    output.meterSource = V2::MeterSource::output;
    V2::SimplePageSpec io { V2::TaskId::io, "I/O", V2::makeCommonIoGroups(input, output) };
    io.fixedActions = { filterAction, routingAction, latencyAction };
    d.pages = { std::move(main), std::move(io) };

    auto filterControls = V2::makeCanonicalFilterStageControls("prompt-filter", {
        "filter_hp_on", "filter_hp_freq", "filter_hp_slope", "filter_lp_on",
        "filter_lp_freq", "filter_lp_slope", "tilt" });
    auto filterPosition = control("prompt-filter-position", "filter_pos", "F / T POSITION", V2::ControlRole::choice);
    filterPosition.choiceLabels = { "POST/POST", "PRE/PRE", "PRE/POST", "POST/PRE" };
    filterControls.push_back(filterPosition);

    d.prompts = {
        { "latency-options", "Compensated Alignment", { "align", "pdc", "max_window" },
          { alignmentMode, align, pdc, maxWindow } },
        V2::makeCanonicalMidiSessionPrompt(),
        { "filter-options", "Filter / Wet Path", {
            "filter_hp_on", "filter_hp_freq", "filter_hp_slope", "filter_lp_on", "filter_lp_freq",
            "filter_lp_slope", "tilt", "filter_pos" }, std::move(filterControls) },
        V2::makeCanonicalRoutingPrompt()
    };

    d.signatureModel = V2::SignatureModel::carrierTopology;
    d.processedSignalOverlay = true;
    d.signature = {
        { "carrierFrequency", "freq" }, { "enginePosition", "engine" },
        { "harmonicAmount", "harm" }, { "harmonicCapacity", "harm" },
        { "feedbackMagnitude", "feedback" },
        { "feedbackPolarity", "feedback" }, { "feedbackSpacing", "comb" },
        { "engineBias", "engine_bias" }, { "engineRectify", "engine_focus" },
        { "stereoTopology", "style" }
    };
    d.hiddenCompatibilityInputs = { "sidechain" };
    return d;
}
}

const V2::SimplePluginDefinition& definition()
{
    static const auto value = buildDefinition();
    return value;
}

const std::vector<std::string>& retiredUiParameterIds()
{
    static const std::vector<std::string> ids {
        "ui_width", "ui_height", "ui_palette", "ui_fx_tail", "ui_io_fx",
        "ui_color0", "ui_color1", "ui_color2", "ui_color3"
    };
    return ids;
}
}
