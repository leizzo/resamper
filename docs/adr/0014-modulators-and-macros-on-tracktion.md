# Modulators and Macros are Tracktion modifiers and macro parameters

The Mods Drawer (#83), Rack Macros (#54) and macro mapping (#122) need modulation sources, routing with a depth, saving with the Edit, and undo. Tracktion already has all of it: `te::Modifier` sources on a track, `te::MacroParameter` on any plug-in or rack, and `AutomatableParameter::addModifier` to route either one to a parameter. Both live in the Edit's ValueTree, so they are saved with the Edit and undone by Engine Undo. The Shaper already works this way (ADR-0012). We decided: **a Modulator is a Tracktion modifier and a Macro is a Tracktion macro parameter; Resamper builds only the facade and the UI.**

| Resamper | Tracktion |
|---|---|
| LFO | `LFOModifier`; an LFO with a drawn shape is a `BreakpointOscillatorModifier` (today's Shaper loop) |
| Env Follower | `EnvelopeFollowerModifier` (today's Shaper audio trigger) |
| Steps | `StepModifier` |
| Random | `RandomModifier` |
| Envelope | No direct equivalent. Start from `BreakpointOscillatorModifier` with `note` sync; a modifier of our own is a new decision |
| Macro in a Mods Drawer | `MacroParameter` in the native device plug-in's own `MacroParameterList` (every `te::Plugin` is a `MacroParameterElement`) |
| Rack Macro | `MacroParameter` in the `te::RackType`'s `MacroParameterList` |
| Routing and depth | `AutomatableParameter::addModifier (source, value)`; the `ModifierAssignment` value is the depth; `removeModifier` unroutes |
| Knob/Modulated live value | `AutomatableParameter::getCurrentValue()` is the modulated value; `getCurrentBaseValue()` is the knob's own value |

- Modifier Modulators go on the track's `ModifierList` (`te::Track::getModifierList`), so they can reach any parameter on the same track (CONTEXT.md, **Modulator**). The Native Device whose drawer shows a Modulator is a custom property on the modifier's ValueTree (ADR-0001), not a second list.
- One facade beside the Application Model owns this (ADR-0012). `Shaper` grows into it under #82; nothing above `Source/Engine` sees a Tracktion type.

**Considered options:**
- `gin_plugin`'s `ModMatrix`, `LFOComponent` and `MSEGComponent`. Rejected: they are built for `gin::Processor` and `gin::Parameter`, and would run a second modulation system next to Tracktion's (ADR-0007).
- Our own audio-thread modulators. Rejected by ADR-0012: the engine already modulates parameters safely, and a second writer would fight it.

**Consequences:** Adding, routing, changing depth and removing are Edit changes, so each is one Engine Undo step and is saved with the Edit. Each of those needs a headless undo test, because undo restores state only: if a modifier's or macro's value doesn't re-read its state after undo, the facade re-syncs it the way ADR-0009 does for volume and pan. The Envelope slot is the only source without a ready-made engine type; #83 decides it.
