# Changelog

All notable changes to Resamper are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Each PRD milestone (PRD §22) ships as a minor release until 1.0.0, and a defect fix is a
patch release; until then every release is published on GitHub as an alpha pre-release.

## [Unreleased]

### Added

- On launch, a newer release shows an Update button and downloads in place. Restart swaps in the
  signed app. A development build opens the release in the browser.
- The first launch of a version opens a dialog of what was added and changed since the version
  opened last time.

## [0.2.2] - 2026-10-04

Fixes and layout internals on top of **M1.1 — Devices & Plug-ins**.

### Changed

- Layouts are C++ components. Saving a theme file still re-styles the app. Developer Mode opens
  melatonin_inspector instead of the in-house inspector.
- A plug-in saved in the Project but not installed is **Missing**. Plug-in Hosting starts it once a
  scan finds it.
- The per-instance sandbox switch is named **Run in-process**.

### Fixed

- Menus, the Save Preset dialog, tooltips and toasts stay above a sandboxed plug-in's own UI.
- A clip's context menu belongs to its lane, so it is dismissed when the lane goes.
- The selected clip's outline comes from the Theme.
- Undoing a plug-in insert drops that plug-in's pending sandbox load with the Edit. A load one Edit
  leaves pending is not taken up by the next Edit.

## [0.2.1] - 2026-10-02

Release fix on top of **M1.1 — Devices & Plug-ins**. v0.2.0 was tagged but never published: its
release build failed on a timing assertion in the tests.

### Fixed

- The test that a slow plug-in loads in the background bounds the insert by the plug-in's load time
  instead of a fixed 300 ms, which a CI runner can exceed (#138).

## [0.2.0] - 2026-10-02

Milestone **M1.1 — Devices & Plug-ins**: native devices v2, plug-in hosting in a sandbox, plug-in
windows and lucide icons.

### Added

- **EQ Eight v2 and Compressor v2** native devices, edited on their graphs (#67). EQ Eight: drag a
  node for frequency and gain, wheel for Q, double-click to switch a band; eight bands with type
  and on/off, pre/post spectrum, Q-width shading, St / L-R / M-S, Adaptive Q, audition. Compressor:
  drag the threshold on the Transfer graph, Transfer / Activity views, IN / GR meters, Lookahead,
  Peak / RMS / Expand, Makeup Auto / Mix / Out. Each gesture is one undo step.
- **Device cards** for native devices and plug-ins (#66). A native card can be Folded, Compact or
  Expanded. A plug-in card shows vendor, format, up to 4 pinned parameters edited inline, and CPU,
  latency and sandbox state; a missing plug-in offers **Locate** and **Replace**. The chain ends in
  a drop zone.
- **Plug-in windows** (#68) with host chrome: Bypass, presets, A/B compare, latency, CPU, sandbox
  state and UI scale 100 / 150 / 200 %. A plug-in's window opens when it is inserted, with a toast
  offering **Undo** and an **Auto-open window on insert** switch. One window per instance; position, pin and scale
  are saved with the Project. `Esc` / `Mod+W` close the focused window, `Mod+Alt+P` shows or hides
  them all.
- **Plug-in sandbox** (#69): each plug-in runs in its own process by default. A crashing plug-in
  bypasses only its own device, playback keeps going, and **Reload** brings it back with its last
  saved state. **Run in-process** can be set per plug-in and is saved with the Project.
- **Plug-in scanning out of process** (#78): a plug-in that crashes or hangs the scan is listed as
  *Failed to scan* with **Retry**, and the rest load. Browser rows show a VST3 / AU / CLAP badge.
- A native Mixer Insert opens its device in a floating window, like a plug-in insert; plug-in
  inserts look different from native ones (#70).
- The lucide icon set, sized and coloured by role (#79).

### Changed

- EQ Eight v2 and Compressor v2 replace the v1 EQ and compressor in the device list. Projects that
  use the v1 devices still load them.

### Fixed

- Inserting a sandboxed plug-in no longer blocks the app while it loads; export, bounce and freeze
  wait for plug-ins that are still loading (#134).
- A sandboxed plug-in's CPU % is the plug-in's own time in its host (#136).
- Undo right after a new track or a plug-in insert keeps Redo (#133).
- A plug-in insert that is refused leaves no partial undo step.
- Shortcuts typed in a plug-in window reach Resamper.

### Known issues

- AUv3 and other plug-ins that load asynchronously run in-process, outside the sandbox.
- A plug-in that finishes loading during playback can cause a short gap.
- Moving a plug-in's controls doesn't record automation yet (M3).

## [0.1.3] - 2026-10-01

Arrangement and control fixes on top of **M1 — Core**.

### Changed

- A warped Audio Clip (a tempo-tagged loop, or one played faster or slower) stretches in real
  time: a trim, split, Clip Loop or tempo change is heard at once and no longer renders a new
  file or stops the transport.
- Adding a FLAC, OGG or MP3 as a clip decodes it into the Project's `Audio` folder as a WAV, and
  the clip plays that copy. Projects saved by 0.1.x that use such a file directly are not
  migrated: they read it as it is, which can cost more CPU.
- Waveforms are read on a normal-priority thread, so an Audio Clip's waveform appears sooner.

### Fixed

- An Audio Clip's waveform no longer hangs on "Preparing audio", stays put when the clip is split or
  moved (to the same or another track), and is not left generating when its file can't be read.
- A clip's title no longer glitches or follows the Playhead during playback.
- Clicking a track header selects the track instead of focusing its Record Arm button, and the
  header's menu can't act on a track that was deleted while it was open.
- A Knob's focus ring is no longer clipped, and the value tag of a Knob or Fader sits beside the
  control instead of following the pointer.
- Clicking a Mixer fader away from its cap jumps the cap to that dB, in one undo step with any drag
  that follows.

## [0.1.2] - 2026-09-30

Mixer Bus Strips and release automation on top of **M1 — Core**.

### Added

- The Mixer draws a Strip for each Bus, after its last child: tinted in the Bus colour, with its
  input count, Output, Mixer Inserts, Sends, pan, mute/solo and meter. A Bus gets **Add Send** from
  the strip menu, and its fader, pan, mute and solo undo like a track's.
- Each GitHub release ships the macOS app (Apple Silicon, signed and notarized) as a DMG, and its
  notes list every change with links to the commits and pull requests.

### Changed

- Mixer track numbers count tracks only, and Returns are ordered A–D.
- A track's Output in the Mixer is the nearest Bus above it, following nested Buses, else the Master.
- Internal: one Track Kind rule and track lookup for the Engine, and the Mixer reads a Strip model
  in signal-flow order instead of re-deriving Bus and Return membership in the view.

## [0.1.1] - 2026-09-29

Fixes and groundwork on top of **M1 — Core**.

### Changed

- The product is renamed from Papercut to Resamper. Projects saved by 0.1.0 are not
  migrated: re-create them in 0.1.1.
- A JSON layout button that names a Command needing arguments fails to load with a
  layout error, as a button naming an unknown Command already did.
- Internal: the app, tests and snapshots are built from one composition root; each
  Command is declared once, invoked through a typed handle whose arguments the
  compiler checks, and menu items get their IDs from the menus' order.

### Fixed

- Undo: a continued drag no longer merges into an undo step another part of the app
  started, so one Undo reverts only one change (for example, a volume drag and a
  plug-in bypass are now two steps).
- Plug-in scans: stopping a scan (including on quit) no longer blocks for up to
  two minutes or crashes afterwards.
- A missing value no longer zeroes a plug-in parameter or picks the first clip colour.
- A theme file whose JSON isn't an object is reported instead of crashing, and
  theme colours reject non-hex digits.

## [0.1.0] - 2026-09-28

Milestone **M1 — Core**: shell, transport, Arrangement, device chain with plug-ins,
basic Mixer, and the Resamper design system.

### Added

- Engine vertical slice: Project, Commands, JSON UI, Arrangement and playback.
- Clip editing: select, move, resize and split clips; seek the playhead from the timeline.
- Track channel controls: volume, pan, mute and solo.
- Audio recording with input selection, live waveform and takes.
- MIDI tracks and clips, piano roll note editing and quantize.
- Plug-ins, mixer, shapers, session and recovery foundations.
- Design tokens: Resamper DS colours, type and scales (#17).
- Shared control library and continuous-control interaction model (#18).
- Top bar with menu, transport, status readouts and view switcher (#19).
- Library Browser with search, categories, drag to track and sample preview (#20).
- Detail view with clip panel and horizontal device chain of DeviceCards (#21).
- Arrangement clip operations: loop-extend, duplicate, consolidate and clip menu (#24).
- Arrangement zoom limits, lane height and Follow (#25).
- Mixer toolbar: section chips, meter modes, reset peaks, signal-flow indicator (#27).
- Mixer inserts: 8 slots, bypass, reorder, copy, picker and context menu (#28).
- Global interaction layer: selection, Esc, context menus, tooltips, toasts, drag-and-drop (#29).
- Per-view keyboard shortcut map kept in data (PRD §17) (#30).
- Record count-in; Shift-click Rec skips it (#61).
- MIDI recording from a MIDI input on armed MIDI tracks (#63).

### Changed

- Track device chain separated from mixer Inserts (#22).
- Arrangement ruler, lanes, track headers and clips restyled to the M1 design (#23).
- Mixer channel strip rebuilt: head, fader dB mapping, stereo meter, buttons (#26).

### Removed

- Pre-redesign UI from the M1 shell.

### Fixed

- Silent tempo-tagged loops with no waveform.
- M1 review findings and design parity in lanes, devices and the mixer.

[Unreleased]: https://github.com/leizzo/resamper/compare/v0.2.2...HEAD
[0.2.2]: https://github.com/leizzo/resamper/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/leizzo/resamper/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/leizzo/resamper/compare/v0.1.3...v0.2.0
[0.1.3]: https://github.com/leizzo/resamper/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/leizzo/resamper/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/leizzo/resamper/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/leizzo/resamper/releases/tag/v0.1.0
