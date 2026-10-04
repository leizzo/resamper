Status: superseded by ADR-0007.

# Plugin UI uses gin_plugin

ADR-0007 kept plugin UI on JUCE so the build would not pull `gin_dsp` and `gin_graphics`. That decision is reversed: plugin and synth UI is built with `gin_plugin`. The modules it requires — `gin_dsp`, `gin_graphics`, and `gin_simd` — are part of the build.

**Consequences:** Brief §22 lists plugin/synth UI under `gin_plugin` again. Brief §23's initial module set includes `gin_plugin` and those three dependencies; they are no longer deferred.

**Why it was superseded:** No code was ever built on `gin_plugin`, and the modules were removed from the build in `fe61215`. Device cards, knobs and the Plug-in Window are JUCE components, and modulation belongs to Tracktion's modifiers, which `gin_plugin`'s own modulation system would duplicate. ADR-0007 is the decision in force.
