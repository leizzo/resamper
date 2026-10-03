# Facade over te::Edit, no parallel track model

The development brief proposed a separate "Track Model" (name/mute/solo/volume/pan/colour) kept apart from the engine. Tracktion Engine already owns all of that state on `te::AudioTrack`, inside an Edit whose entire state is an undoable ValueTree. We decided the Application Model is a **facade** over `te::Edit`: it exposes app-level operations (`project.addAudioTrack()`) and hides Tracktion headers from the UI, but state lives only in the engine. App-specific extras are stored as custom properties on the Edit's ValueTree, not in a shadow model.

**Considered options:** A parallel synced model was rejected because it creates two sources of truth and a permanent sync-bug class. Its main benefit — testability without the engine — is already available via Tracktion's headless engine test pattern (`EngineBehaviour::autoInitialiseDeviceManager() = false`).

**Consequences:** Undo/redo comes from the engine's UndoManager (see CONTEXT.md, "Engine Undo"), so Commands never implement their own `undo()`. UI components must never include Tracktion headers; all access goes through the facade.
