# Recordings live in the Project folder; the Application Model owns take switching

Tracktion Engine records armed inputs and, when loop recording, cuts the recording into one file per pass and lists them as takes of one clip. Two parts of that machinery assume a `te::Project` (the engine's own project database), which Resamper doesn't use (ADR-0002):

- `WaveAudioClip::setCurrentTake` looks each take up as a Project item. A take that is a plain file isn't found, so the engine *deletes* it rather than switching to it. Without a Project, the engine also lists the first pass twice.
- A recording's path is stored relative to the Edit file. JUCE only measures from the file's folder if the file exists, but the engine resolves the path next to the file either way. An untitled Project's Edit file was never written, so its recordings resolved one folder too high.

We decided:

- Recordings are written to the Project's `Audio/` folder, named `<track> Recording <n>`, and referred to relative to the Edit.
- A New Project is written to its temporary folder straight away, so paths are stored as they are in a saved Project. Save As copies the `Audio/` folder into the new Project, which keeps those relative paths valid.
- The Application Model switches takes itself. It sets the clip's source to the take's source in one undo step. The current take is the one whose file the clip plays. After a recording it removes duplicate takes, inside the recording's undo step.
- The whole recording is one undo step, opened just before the transport stops. Every way of ending a recording (Stop, Return to Start) goes through `ApplicationModel::stop()`. The loop can't change while recording, because the engine cuts the takes by the loop it finds at stop. Inputs and arming stay out of the UndoManager, as the engine keeps them, the same as mute and solo.
- The model checks the engine's two recording preconditions (an armed track, and a loop of at least 2 s when looping) and returns them as errors. The engine would only tell its `UIBehaviour`, and Resamper shows nothing from that.

**Considered options:**
- Using a `te::Project` for media. Rejected: it would bring the engine's project database into the Project format, against ADR-0002.
- Absolute paths for recordings, like imported clips. Rejected: a Project folder must stay movable, and recordings made while untitled would keep pointing into the temporary folder.
- Deleting the takes that aren't current after each recording, so there is nothing to switch. Rejected: switching takes is the Phase 4 feature.

**Consequences:** Take compositing (the engine's `WaveCompManager`) is out of scope. Any other engine feature that reaches for Project items needs the same check. The temporary folder of an untitled Project is a real Project on disk until the app quits.
