Status: accepted. ADR-0008 reversed this decision and is itself superseded by this one.

# Plugin UI is built on JUCE, not gin_plugin

The brief (§22, §23) listed `gin_plugin` among the initial GIN modules for plugin/synth UI. `gin_plugin` hard-depends on `gin_dsp` and `gin_graphics`, which the brief defers, so adopting it would pull two more GIN modules in ahead of any need. JUCE already provides plugin hosting and editor windows (`AudioProcessorEditor`, `AudioPluginFormatManager`), and Tracktion hosts plugins through JUCE. We decided: **plugin-related UI is built on JUCE; GIN modules are added one at a time, when a concrete need appears.**

**Considered options:** Building `gin_plugin` together with `gin_dsp` and `gin_graphics` — rejected because it enlarges the dependency surface for a feature (Phase 6) that JUCE already covers, against brief §23's goal of keeping dependency count low. `gin_plugin`'s widgets (`Knob`, `ModMatrix`, `LFOComponent`, `MSEGComponent`) are built for `gin::Processor` and `gin::Parameter` and bring their own modulation system; using them would wire a second modulation system next to Tracktion's `MacroParameter` and modifiers (ADR-0012, ADR-0014).

**Consequences:** Brief §22's "Plugin / Synth UI → gin_plugin" row and §23's initial module list are amended. Today the build uses only the `gin` core module. If a later need for GIN's plugin widgets appears, it is a new decision, and it brings `gin_dsp` and `gin_graphics` with it.
