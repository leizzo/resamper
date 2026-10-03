# Resamper

A desktop DAW for electronic producers and mix engineers. This glossary pins the project's domain language. Open product decisions are tracked as GitHub issues; implementation details live in the code, not here.

## Language

### Documents & State

**Project**:
The folder the user saves and opens: the Edit, its recorded and imported audio, and its caches. A Project contains exactly one Edit.
_Avoid_: Song, session (Session View is a different concept)

**Edit**:
The one document inside a Project: tracks, Returns, the Master, clips, Scenes, tempo, time signature, transport. The engine owns this state; the application never duplicates it.
_Avoid_: Document, arrangement (the Arrangement is a *view* of the Edit)

**UI State**:
View-only state that survives Hot Reload and is saved with the Edit: scroll position, zoom, focus. Selection is *not* UI State.
_Avoid_: view state

### Views

**Session View**:
The clip-launching view: a grid of Slot Clips, one column per track and one row per Scene. Where ideas are sketched and performed.
_Avoid_: session (unqualified), clip view

**Arrangement**:
The linear timeline view of the Edit: tracks as lanes, clips placed in time, Automation Lanes below their track.
_Avoid_: timeline, song view

**Mixer**:
The console view: one Strip per track, Bus, Return and the Master, in signal-flow order.
_Avoid_: mixing desk, console

**Detail View**:
The bottom panel that edits whatever is selected: the Device Chain of a track, a Folder's inspector, a clip's Piano Roll or Audio Editor.
_Avoid_: device view, inspector (an inspector is one section inside it)

**Piano Roll**:
The editor for a MIDI Clip's notes, velocities and Clip Envelopes.
_Avoid_: MIDI editor, note editor

**Audio Editor**:
The editor for an Audio Clip: waveform, Warp Markers, Transients, Fades, Clip Gain and Clip Envelopes.
_Avoid_: sample editor (that is part of Simpler)

### Operations

**Command**:
A named operation the user triggers from a button, menu or keyboard shortcut. It is the only way the UI changes the Edit.
_Avoid_: Action, operation; JUCE's ApplicationCommand is a different thing and is always spelled in full

**Engine Undo**:
The one undo history of the Edit. Every discrete change and every completed gesture (a fader drag, a velocity drag) is one undo step, including mute, solo and mixer changes during playback. Not undoable: transport (including the Loop), selection, zoom, scroll, a track's Input and arming.

### Tracks & Clips

**Track Kind**:
Audio or MIDI: the kind of clip a track holds. A clip only moves onto a track of its own kind. Folders, Returns and the Master have no Track Kind.
_Avoid_: track type

**Audio Track**:
A track of kind Audio. It holds Audio Clips and can record its Input.

**MIDI Track**:
A track of kind MIDI. It holds MIDI Clips and always has an instrument in its Device Chain.
_Avoid_: instrument track

**Audio Clip**:
A clip that plays a region of an audio file on an Audio Track.
_Avoid_: sample, region

**MIDI Clip**:
A clip of MIDI notes on a MIDI track. It has no audio file.
_Avoid_: pattern, sequence

**Clip Loop**:
The range inside a clip that repeats while the clip is longer than it. Distinct from the transport Loop.

**Output**:
Where a track's audio goes after its fader: the Master, a Bus, or an external output.
_Avoid_: route (in the domain; "route" is the UI chip)

**Monitor**:
A track's In / Auto / Off setting that decides whether its Input is heard live.

**Scale**:
A root and mode set on a MIDI Clip. The Piano Roll highlights its notes; it does not constrain them.

**Warp**:
An Audio Clip's audio follows the Edit's tempo (and its Warp Markers) instead of playing at its recorded speed. A clip is *warped* or *unwarped*.
_Avoid_: time-stretched clip, proxy (an engine detail)

**Warp Marker**:
A pin that ties a point in an Audio Clip's audio to a beat. The audio stretches between neighbouring Warp Markers.

**Transient**:
A detected onset in audio. It can be turned into a Warp Marker or, in Simpler, a slice.

**Fade**:
A gain ramp at the start or end of an Audio Clip, with its own length and curve.

**Clip Gain**:
A level change on an Audio Clip itself, applied before the track's Device Chain.
_Avoid_: clip volume

### Recording

**Input**:
One of the audio interface's inputs, assigned to a track; a track has at most one. **Arming** a track makes it record its Input when the transport records.

**Recording**:
What one press of Record captures on each armed track: an audio file in the Project, which becomes a clip when the transport stops.

**Loop**:
A time range on the transport, played over and over while looping is on. Recording while looping records a Take per pass.
_Avoid_: bare "loop" for a Clip Loop or an envelope loop; always qualify those

**Take**:
One pass of a loop Recording. The passes share one clip; the clip plays one Take at a time and the user switches between them.
_Avoid_: Comp (compositing Takes is not supported)

### Devices

**Device**:
One processor in a Device Chain or a Mixer Insert slot: a Native Device, a Plug-in, or (in a Device Chain only) a Rack.

**Native Device**:
A Device built into Resamper, edited inline on its card with Resamper's own controls, never in a window of its own. Every Native Device follows one contract: the same header, the zones Input → Display → Controls → Output, and three sizes (Folded, Compact, Expanded). Its graphs are controllers: dragging an EQ node or a compressor's threshold line sets the parameter, and the knobs mirror the graph. EQ Eight and Compressor are Resamper's own processors, not wrappers of the engine's equaliser and compressor (those have four bands and no M/S).
_Avoid_: built-in plug-in, internal plugin

**Plug-in**:
A third-party Device (VST3, AU, CLAP). Its own UI opens in its Plug-in Window as soon as it is added.
_Avoid_: using "plug-in" for Native Devices

**Plug-in Window**:
The floating window that shows one Plug-in's own UI inside Resamper's window frame.
_Avoid_: plug-in editor, vendor window

**Native Device Window**:
The floating window that shows one Native Device's card, Expanded: opened from the card's Open in Window or by clicking its Mixer Insert. It follows the Plug-in Window's rules (one per Device, Pin, hidden while its track isn't selected unless pinned).
_Avoid_: native editor popover, device popover

**Sandbox**:
The separate process a Plug-in runs in by default (one per instance), so a crash takes down only that Plug-in: its audio is bypassed, the rest of the session plays on, and **Reload** starts it again from its last saved state. A Plug-in can be set to run in-process instead (**Run in-process**), per instance, saved with the project.
_Avoid_: bridge, out-of-process host (for the concept; fine for the mechanism)

**Plug-in Hosting**:
Where and how each Plug-in instance runs: in its Sandbox or in-process, and whether it is ready to play. The Sandbox is the mechanism; Plug-in Hosting decides which Plug-ins use it, starts and reloads them, and knows each one's Hosting State.
_Avoid_: loader, plug-in host (the sandbox host is the process a sandboxed Plug-in runs in)

**Hosting State**:
The one state a Plug-in is in at a time: **Loading**, **Sandboxed**, **In-process**, **Crashed** (its Sandbox died; its audio is bypassed until Reload), **Failed** (it could not be loaded, and says why; Retry or Run in-process) or **Missing** (saved in the Project but not installed). A Plug-in being reloaded is Loading, even while its old instance still plays or stays bypassed until the new one is ready.
_Avoid_: load state, plug-in status

**Device Chain**:
A track's sound: its instrument (on a MIDI track), Racks and creative effects, in order, edited only in the Detail View. It runs before the Mixer Inserts. A MIDI track has one instrument; adding an instrument replaces the current one. A Bus's chain and the Master's rack are also Device Chains.
_Avoid_: Track chain (only the mixer strip's read-only label for it), bus chain, master rack, insert chain

**Rack**:
A Device that holds Rack Chains and Macros: Instrument, Drum, Audio Effect or MIDI Effect Rack. Racks nest, and live only in a Device Chain.

**Rack Chain**:
One parallel lane of Devices inside a Rack, with its own volume, enable and solo.
_Avoid_: chain (unqualified)

**Pad**:
One cell of a Drum Rack, played by one MIDI note. Each Pad is a Rack Chain. A Drum Rack has 128 Pads in 8 banks of 16.

**Macro**:
One of a Rack's knobs; a Rack has 1 to 16, and the user adds and removes them. It moves any number of parameters inside the Rack at once.

**Modulator**:
A source that moves Device parameters continuously: LFO, Envelope, Env Follower, Steps, Random or Macro. It sits in a Native Device's Mods drawer and is routed with a depth to any parameter on the same track: other Devices, Plug-in parameters, volume, pan, sends.
_Avoid_: mod (unqualified), automation, Shaper

**Simpler**:
The Native Device that plays one sample in one of three modes: **Classic** (loops while held), **1-Shot** (plays through once) or **Slice** (cuts the sample, one slice per note).

**Utility**:
A transparent Native Device with gain and pan. It is where a track's Modulators live when the track has no other Native Device.

**Sampler**:
The Native Device that plays many samples, each in a Zone.

**Zone**:
One sample in a Sampler, with the key range and velocity range that trigger it. Zones with the same velocity range form a velocity layer.

**Plugin Catalogue**:
The scanned list of Devices the user can insert: name, manufacturer, format, category. Native Devices are listed without a scan; engine plumbing (fader, meters, sends and returns) is not listed. A Plug-in whose scan crashed or timed out is listed as *Failed to scan*: it can be retried, never inserted.
_Avoid_: plugin database

**Mixer Insert**:
One Native Device or effect Plug-in in a track's mixer insert slots: console processing (EQ, compression, limiting) after the Device Chain and before the sends and fader, edited in the mixer strip. Effects only, at most 8 per track. Clicking one opens its Plug-in Window or Native Device Window. The mixer never lists Device Chain Devices as Mixer Inserts.
_Avoid_: insert (unqualified), FX slot

**Sidechain**:
A key signal from another source (track, Rack Chain or Pad, Bus, Return, external input) that drives a Device's detector. It is taken at a Tap on the source: Input (the default), Pre-FX or Post-Fader. It is never heard on the destination; only the destination's gain changes.
_Avoid_: key input (the key is the signal; the Sidechain is the link)

### Mixer

**Strip**:
One column in the Mixer for one track, Bus, Return or the Master, laid out in signal-flow order. A **compact strip** leaves out the inserts and full send options.
_Avoid_: channel (unqualified), channel strip

**Tap**:
A point on a track's signal path where a Send or Sidechain takes its signal. **Input**: before the Device Chain. **Pre-FX**: after the Device Chain, before the Mixer Inserts. **Pre-Fader**: after the Mixer Inserts, before the fader. **Post-Fader**: after the fader and pan; follows mute.
_Avoid_: Post-FX, Post-Mixer

**Return**:
A track fed only by Sends from other tracks; it has no clips. A project has at most four, named A–D.

**Send**:
A track's feed into one Return, taken at the Pre-FX, Pre-Fader or Post-Fader (default) Tap. It has its own pan and polarity.

**Polarity**:
Inverting a signal (Ø). A track and each of its Sends have their own polarity switch.
_Avoid_: phase, phase invert

**Folder**:
A track that holds other tracks and has no clips. Its mode is either **Folder only**, which just organises tracks while each child keeps its own Output, or **Folder + Bus**, which makes it a Bus. Folders nest.
_Avoid_: group track

**Bus**:
A Folder in Folder + Bus mode. Its children's output sums through it, and through its own Device Chain, inserts, sends and fader, before the Master.
_Avoid_: calling a Folder only a bus

**Master**:
The Edit's master track, where every Output ends. Its volume is the master fader, separate from any track fader. The master has no pan; it carries the loudness meter and the Mono, Dim and Cue switches.

**Cue**:
A separate output pair for pre-listening (Browser preview, the Master's Cue switch). With no Cue output chosen, it plays on the main output.
_Avoid_: headphone bus, PFL

### Automation

**Automation Lane**:
The breakpoint curve of one parameter (volume, pan, a send, or a Device parameter) drawn against time on the Arrangement. It is either active or Overridden.
_Avoid_: Parameter Lane

**Breakpoint**:
One point on an Automation Lane or Clip Envelope: a time, a value, and the curve to the next point (linear, hold or bezier).
_Avoid_: node, automation point

**Clip Overlay**:
An Automation Lane drawn on top of the clips of its track. It is a view of the lane, not a Clip Envelope.

**Automation Arm**:
The global switch that lets moving a control write automation.
_Avoid_: automation record

**Automation Mode**:
How an Automation Lane behaves while playing with Automation Arm on: **Read**, **Touch** (writes while held, then returns), **Latch** (writes from the first touch until stop) or **Write** (overwrites the lane from play start, touched or not; there is no separate per-parameter arm). Touch is the default.

**Overridden**:
The state of an Automation Lane whose parameter was moved by hand without Automation Arm. It stops following its curve until **Re-enable** puts every Overridden lane back to Read.

**Clip Envelope**:
A breakpoint curve that belongs to one clip and moves with it. **Linked** follows the Clip Loop; **Unlinked** repeats over its own envelope loop. **Absolute** sets the value; **Modulation** multiplies with the track's Automation Lane.
_Avoid_: clip automation

### Session

**Scene**:
One row of clip slots across tracks. Launching a Scene launches every occupied slot in that row.

**Slot Clip**:
A clip that lives in a track's slot, not on the Arrangement timeline. Launching it plays the slot; the Arrangement clips on that track are silent while a slot is playing.

**Launch Quantization**:
The grid a launch waits for before it starts (default 1 bar). Until then the slot is **queued**.

**Crossfader**:
The Session View's A/B fader. Each track is assigned to A, B or neither, and the Crossfader blends the A and B tracks.

**Record into Arrangement**:
Captures the currently playing slot clips onto the Arrangement as ordinary clips, from the playhead.

### Presentation

**Theme**:
Visual style only: colours, corner radii, fonts. Never contains geometry.
_Avoid_: Skin, look-and-feel

**Layout Metrics**:
UI geometry: track height, header widths, toolbar heights. Distinct from Theme: geometry is not style.

### Architecture

**Application Model**:
The facade over the engine that exposes app-level operations. It keeps no copy of track or audio state.
_Avoid_: Track Model, shadow model

**Hot Reload**:
Reloading the Theme file while the app runs. Layouts are C++ components, not files.
_Avoid_: C++ hot reload (explicitly out of scope)

**Vertical Slice**:
The first milestone chain: engine → audio device → empty Edit → audio track → audio clip → waveform → transport → play/stop.
