#!/usr/bin/env python3
import json
import re
from pathlib import Path


plugin_root = Path(__file__).resolve().parents[1]
workspace = plugin_root.parent
shared_root = workspace / "TR-Shared" / "SimpleUIV2"

processor_h = (plugin_root / "Source" / "PluginProcessor.h").read_text(encoding="utf-8")
processor_cpp = (plugin_root / "Source" / "PluginProcessor.cpp").read_text(encoding="utf-8")
definition = (plugin_root / "Source" / "UIV2" / "FreqUiDefinition.cpp").read_text(encoding="utf-8")
bindings = (plugin_root / "Source" / "UIV2" / "FreqBackendBindings.cpp").read_text(encoding="utf-8")
renderer = (shared_root / "Components" / "SimpleVisualSignature.cpp").read_text(encoding="utf-8")
descriptor = (shared_root / "Components" / "SimpleDescriptorControl.cpp").read_text(encoding="utf-8")
contract = (shared_root / "Contract" / "FREQ_CARRIER_SIGNATURE_ACCEPTANCE.md").read_text(encoding="utf-8")
surface_contract = (shared_root / "Contract" / "FREQ_CONTROL_SURFACE_ACCEPTANCE.md").read_text(encoding="utf-8")
manifest = json.loads((shared_root / "Baseline" / "simple_parameter_manifest.json").read_text(encoding="utf-8"))

for fragment in (
    "telemetryHarmonicAmount_",
    "telemetryHarmonicCapacity_",
    "float harmonicAmount",
    "float harmonicCapacity",
):
    assert fragment in processor_h + processor_cpp, f"Missing split harmonic telemetry: {fragment}"

for obsolete in ("telemetryHarmonicShape_", '"harmonicShape"'):
    assert obsolete not in processor_h + processor_cpp + definition + bindings + renderer, (
        f"Obsolete collapsed harmonic role remains: {obsolete}"
    )

for role in (
    "carrierFrequency",
    "enginePosition",
    "harmonicAmount",
    "harmonicCapacity",
    "feedbackMagnitude",
    "feedbackPolarity",
    "feedbackSpacing",
    "engineBias",
    "engineFocus",
    "stereoTopology",
):
    assert f'"{role}"' in definition, f"FREQ definition is missing role: {role}"
    assert f'role == "{role}"' in bindings, f"FREQ backend is missing role: {role}"

assert 'addParameter(d, "mod_harm", V2::ParameterAccess::direct)' in definition
assert 'modulation.inlineToggle' in definition
assert '"mod_harm", "HARM", "Use harmonic steps for MOD"' in definition
assert '"mod-harmonic-control"' not in definition, "MOD HARM must not consume an independent row"
assert '"modulation-options"' not in definition, "Single-toggle MOD prompt must not return"
assert '"latency-options-action"' in definition
assert '"LATENCY / PDC"' in definition
for parameter in ("align", "pdc", "max_window"):
    assert f'addParameter(d, "{parameter}", V2::ParameterAccess::prompt, "latency-options")' in definition
assert 'maxWindow.visibleWhen' not in definition, "MAX WINDOW must remain preconfigurable"
assert 'align.visibleWhen' not in definition and 'pdc.visibleWhen' not in definition
for fragment in (
    '"filter_hp_on", "ON", "Enable the wet-path high-pass band"',
    '"filter_hp_slope", { "6", "12", "24" }',
    '"filter_lp_on", "ON", "Enable the wet-path low-pass band"',
    '"sidechain_hp_on", "ON", "Enable the sidechain high-pass band"',
    '"sidechain_lp_slope", { "6", "12", "24" }',
):
    assert fragment in definition, f"Missing composed filter-band route: {fragment}"
for obsolete_control in (
    "prompt-filter-hp-on", "prompt-filter-lp-on", "prompt-filter-hp-slope",
    "prompt-filter-lp-slope", "sidechain-hp-on-control", "sidechain-lp-on-control",
    "sidechain-hp-slope-control", "sidechain-lp-slope-control",
):
    assert obsolete_control not in definition, f"Redundant filter row remains: {obsolete_control}"

freq_manifest = next(item for item in manifest["plugins"] if item["plugin"] == "FREQ-TR")
musical_apvts = {
    item["id"] for item in freq_manifest["parameters"]
    if item["classification"] == "musical_apvts"
}
musical_state = set(freq_manifest["nonApvtsMusicalState"])
declared = set(re.findall(r'addParameter\(d,\s*"([^"]+)"', definition))
for block in re.findall(r'for \(const auto\* id : \{([^}]+)\}\)\s*\n\s*addParameter', definition):
    declared.update(re.findall(r'"([^"]+)"', block))
assert len(musical_apvts) == 59 and len(musical_state) == 3
assert musical_apvts | musical_state == declared, (
    f"FREQ route inventory mismatch: missing={sorted((musical_apvts | musical_state) - declared)}, "
    f"unexpected={sorted(declared - (musical_apvts | musical_state))}"
)
for fragment in ("62 / 62", "mod + harm", "align + pdc + max window", "no musical parameter may disappear"):
    assert fragment in surface_contract.lower(), f"Missing control-surface rule: {fragment}"
assert 'make_unique<juce::TextButton>("+")' in descriptor
assert 'ControlPresentation::compact ? ">" : "+"' not in descriptor

carrier_start = renderer.rindex("void paintCarrierTopologySignature")
carrier_end = renderer.index("void paintAfterimageSignature", carrier_start)
carrier_renderer = renderer[carrier_start:carrier_end]

for fragment in (
    "1.0f + 6.0f * std::sqrt(density)",
    "partialCount",
    "sameNegativeRegion",
    "focusContribution = shapedBias * shapedFocus",
    "direction * cycles * t",
    "0.94f * (1.0f - identityFade)",
):
    assert fragment in carrier_renderer, f"Missing static carrier rule: {fragment}"

for forbidden in ("visualPhase", "getMillisecondCounter", "responseEnvelope"):
    assert forbidden not in carrier_renderer, f"Autonomous carrier motion leaked into renderer: {forbidden}"

for forbidden in ("createDashedStroke", "for (int bridge"):
    assert forbidden not in carrier_renderer, f"Forbidden carrier artefact path remains: {forbidden}"

for fragment in (
    "exactly `0 Hz`",
    "one to seven",
    "no autonomous phase",
    "Baseline weight and alpha remain identical",
    "effect is exactly zero",
):
    assert fragment in contract, f"Missing revision 06 acceptance rule: {fragment}"

print("FREQ audit passed: 62/62 routes, inline MOD law and static carrier revision 06.")
