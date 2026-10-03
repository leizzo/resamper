# A MIDI track plays through Tracktion's built-in FourOsc until Phase 6

Plugin hosting is Phase 6, but a MIDI track has to be audible now. Tracktion has no MIDI-track type: an `AudioTrack` holds MIDI clips and sends them to the instrument plugins on that track. We decided a MIDI track is an `AudioTrack` with a custom `resamperKind` property of `midi` on its ValueTree (ADR-0001: app-specific extras live on the Edit, not in a shadow model). Each new MIDI track gets Tracktion's built-in `FourOscPlugin`, inserted ahead of the volume plugin, in the same undo step as the track. Its default voice is audible, so a MIDI clip that contains notes plays without a preset.

Phase 6 replaces that plugin with the instrument the user chooses. That phase removes the built-in synth when it inserts the replacement, so a MIDI track has one instrument. The kind property stays: it is not inferred from whichever plugin is loaded, and the track remains a MIDI track after the swap. Audio clips stay off MIDI tracks and MIDI clips stay off audio tracks, because the kind belongs to the track.

**Considered options:**
- Inferring the kind from the presence of FourOsc. Rejected: Phase 6 swaps the instrument, and the track must stay a MIDI track.
- Leaving MIDI tracks silent until Phase 6. Rejected: a clip with notes has to play in this phase.
- A one-shot sampler or a tone generator instead of FourOsc. Rejected: FourOsc is the engine's built-in synth, already registered in the plugin cache.

**Consequences:** MIDI recording and external instruments are out of scope. The header of a MIDI track shows its kind and hides the audio input and arm controls, which record audio. Any engine feature that assumes a `te::Project` still needs the check from ADR-0010; this decision doesn't add one.
