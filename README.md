# Resamper

**English** · [Türkçe](README.tr.md)

[![Release build](https://img.shields.io/github/actions/workflow/status/leizzo/resamper/release.yml?label=release%20build)](https://github.com/leizzo/resamper/actions/workflows/release.yml)
[![Commits](https://img.shields.io/github/actions/workflow/status/leizzo/resamper/commits.yml?label=commits)](https://github.com/leizzo/resamper/actions/workflows/commits.yml)
[![Version](https://img.shields.io/github/v/release/leizzo/resamper?include_prereleases&label=version)](https://github.com/leizzo/resamper/releases)
[![Sponsors](https://img.shields.io/github/sponsors/leizzo?label=sponsors)](https://github.com/sponsors/leizzo)

<p align="center">
  <img src="docs/images/hero.png" alt="Resamper — sound design in racks, mixing on a real console. The arrangement view with the sidechain source picker and the send editor." width="100%">
</p>

A dark, dense, keyboard-friendly desktop DAW for electronic producers and mix engineers.

> **Status: alpha.** Resamper is in early development. The current release is
> [v0.2.1 — M1.1 Devices & Plug-ins](https://github.com/leizzo/resamper/releases/tag/v0.2.1),
> published as an alpha pre-release. Expect missing features, rough edges and project-format
> changes — don't trust it with your only copy of a song yet.

## What is Resamper?

Resamper takes you from idea to structure to mix in a single window. Its core principle:
**sound design lives in the device chain, mixing lives in the mixer** — two separate chains, with a
signal path you can always see.

## Design preview

> These images come from the product design ([`design/design.pen`](design/design.pen)) and show
> where Resamper is heading. Several features in them belong to later milestones — see the
> [roadmap](#roadmap) for what's in v0.2.1 today.

### A console, not a list

<img src="docs/images/mixer.png" alt="Resamper mixer: rack chain link, post-chain mixer inserts, sends with FX / PRE / POST taps, send pan and polarity, returns A–D and a loudness-metered master." width="100%">

### Native devices inline. Plug-ins in a window.

<img src="docs/images/devices.png" alt="Native EQ Eight and Compressor cards edited inline, a third-party plug-in opened in its own floating window, and a compressor sidechained from the kick." width="100%">

### From the first loop to the final bounce

<img src="docs/images/workflow.png" alt="Scale-aware piano roll, audio clip envelopes with their own loop, folders and bus channels, and arrangement automation." width="100%">

## What you can do today (v0.2.1)

- **Arrange** — audio and MIDI tracks, clips you can move, resize, split, duplicate, loop-extend and
  consolidate; zoom, lane height and Follow.
- **Record** — audio recording with input selection, live waveform and takes; MIDI recording from a
  MIDI input; count-in (Shift-click Rec to skip it).
- **Edit MIDI** — piano roll note editing and quantize.
- **Shape sound** — a per-track device chain in the detail view: EQ Eight and Compressor edited on
  their graphs, and VST3 / AU / CLAP plug-ins in their own windows.
- **Host plug-ins safely** — each plug-in runs in its own process: a crash bypasses only that device
  and **Reload** brings it back; a plug-in that fails to scan can be retried.
- **Mix** — volume, pan, mute, solo, stereo meters, 8 insert slots per channel with bypass and reorder;
  Bus Strips with their own inserts and Sends.
- **Browse** — a library browser with search, categories, sample preview and drag-to-track.
- **Stay safe** — autosave and crash recovery.

See [CHANGELOG.md](CHANGELOG.md) for the full list.

## Roadmap

| Milestone | What it brings |
|---|---|
| **M1 — Core** ✅ | Shell, transport, Arrangement, device chain with plug-ins, basic Mixer, design system (v0.1.0) |
| **M1.1 — Devices & Plug-ins** ✅ | Native devices (EQ Eight, Compressor), VST3 / AU / CLAP hosting with crash isolation, plug-in window on insert (v0.2.1) |
| **M2 — Mix** | Pre-FX / Pre / Post sends, returns, master loudness, folders & buses, sidechain inputs |
| **M3 — Automation** | Arrangement lanes, clip overlays, Read / Touch / Latch / Write |
| **M4 — Editors** | Scale-aware piano roll with chords and velocity, audio editor with warp and fades, clip envelopes |
| **M5 — Racks & Session** | Instrument / Drum / Audio Effect racks, macros, Session view with scenes, crossfader |

Target platforms: macOS 13+ (Apple Silicon) and Windows 11 x64. The alpha currently builds on macOS.

## Getting started

Download `Resamper-<version>-macOS.dmg` (Apple Silicon) from
[Releases](https://github.com/leizzo/resamper/releases), open it and drag Resamper to Applications. Or
build from source (see [For developers](#for-developers)).

### Keyboard shortcuts

`Mod` = Cmd on macOS, Ctrl on Windows.

| Action | Shortcut |
|---|---|
| Play / Stop · Play from selection | `Space` · `Shift+Space` |
| Record | `F9` (count-in; Shift-click Rec to skip) |
| Loop selection · Return to start | `Mod+L` · `Home` |
| Metronome · Tap tempo | `C` · `T` |
| Session ↔ Arrange · Mixer | `Tab` · `Mod+Alt+M` |
| Toggle detail view · browser | `Mod+Alt+L` · `Mod+Alt+B` |
| Undo · Redo | `Mod+Z` · `Mod+Shift+Z` |
| Duplicate · Split · Consolidate | `Mod+D` · `Mod+E` · `Mod+J` |
| New audio · MIDI track · return | `Mod+T` · `Mod+Shift+T` · `Mod+Alt+T` |
| Mute track 1–8 · Solo selected | `F1`–`F8` · `S` |
| Zoom in / out · to selection · to song | `+` / `−` · `Z` · `Shift+Z` |
| Piano roll: quantize · transpose | `Q` · `↑↓` (semitone), `Shift+↑↓` (octave) |
| New · Open · Save project | `Mod+N` · `Mod+O` · `Mod+S` |
| Export mix | `Mod+Shift+E` |
| Plug-in windows: close focused · show / hide all | `Esc` or `Mod+W` · `Mod+Alt+P` |

## Feedback

Found a bug or have an idea? [Open an issue](https://github.com/leizzo/resamper/issues).

If Resamper is useful to you, you can [sponsor the project on GitHub](https://github.com/sponsors/leizzo).

### 🍺 Beer

$20 a month, or $25 once. Names land here, and once in the release notes.

### ☕️ Coffee

$5 a month, or $10 once. Names land here.

## License

Resamper's own source code is released under the [MIT License](LICENSE). That grant covers those files on their own.

A build of the app also contains [JUCE](https://juce.com/legal/juce-8-licence/) (AGPLv3, or a commercial JUCE licence) and [Tracktion Engine](https://engine.tracktion.com/agreement) (GPLv3, or a commercial Tracktion licence). GIN is BSD-3-Clause and melatonin_inspector is MIT. The MIT licence does not sublicense JUCE or Tracktion.

You may run the app, including for paid work. You may also share and sell a build when that distribution stays under the AGPL and the GPL and the corresponding source is offered. A closed-source build needs your own JUCE licence and your own Tracktion Engine licence. Each is bought from that vendor, and one does not include the other. Both can be taken out before any sale.

This is a summary of those texts, not legal advice.

---

<a id="for-developers"></a>

# For developers · Geliştiriciler için

*Teknik bölüm, komut ve kod terimleri ortak olduğu için İngilizce tutulmuştur.*

Resamper is built on [JUCE](https://juce.com) + [Tracktion Engine](https://github.com/Tracktion/tracktion_engine)
+ [GIN](https://github.com/FigBug/Gin). [CONTEXT.md](CONTEXT.md) defines the domain language.

## Dependencies (pinned git submodules)

| Path | Pin |
|---|---|
| `external/tracktion_engine` | Tracktion Engine 3.5.0, commit `964583ee` (3.5.0 plus an upstream fix for a null ProjectItem crash when saving an Edit outside a Tracktion project) |
| `external/tracktion_engine/modules/juce` | JUCE 8.0.13 (`8.0.13-7-g37c894f8`), pinned by Tracktion |
| `external/gin` | GIN, commit `ea795541`; only the `gin` module is built (other modules are added when code needs them) |
| `external/melatonin_inspector` | melatonin_inspector, commit `9c483f86` (module 1.4.0); the Developer Mode component inspector |

## Build (macOS)

Requires CMake ≥ 3.22, Ninja and Xcode command-line tools (C++20).

```sh
git submodule update --init external/gin external/melatonin_inspector external/tracktion_engine
# Tracktion's .gitmodules points JUCE at an SSH URL; use HTTPS unless you have GitHub SSH keys:
git -C external/tracktion_engine config submodule.modules/juce.url https://github.com/juce-framework/JUCE.git
git -C external/tracktion_engine submodule update --init modules/juce

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

- App: `build/Resamper_artefacts/Debug/Resamper.app`
- Tests (headless, no audio device): `build/ResamperTests_artefacts/Debug/ResamperTests [suite-name-filter]`, or `ctest --test-dir build`

## Layout

```
Source/Engine/    EngineManager, ProjectManager, ApplicationModel facade — the only code that sees Tracktion
Source/Commands/  Command registry, model Commands, ApplicationCommand ↔ Command ID table
Source/UI/        Theme, UI State, Arrangement, Mixer, Detail View, MainWindow, Developer Mode tools
Source/App/       Application entry point
UI/themes, UI/fonts   Theme and font files (embedded in Release; read from the source tree in Debug)
Tests/            Headless tests over the Command registry / Application Model seam
```

`Source/UI`, `Source/Commands` and `Source/App` are compiled without Tracktion on the include path, so
"UI never includes Tracktion headers" is a compile error rather than a convention.

## Developer Mode

In Debug builds, the theme is read from `UI/` in the source tree. Saving a theme file re-styles the app; so does:

- **Reload Theme** — Cmd+Alt+Shift+T: re-styles in place
- **Developer Overlay** — Cmd+Alt+Shift+D: shows the status bar and opens [melatonin_inspector](https://github.com/sudara/melatonin_inspector) in its own window (hidden by default; the design has neither)

## Contributing

Issues are tracked on [GitHub](https://github.com/leizzo/resamper/issues). Releases follow
[Semantic Versioning](https://semver.org); each PRD milestone ships as a minor release until 1.0.0 —
see [CHANGELOG.md](CHANGELOG.md).

Commit subjects are `<gitmoji> <type>(<scope>): <summary>`, e.g. `🐛 fix(undo): …`; CI checks every
commit a pull request adds. Release notes group commits by `<type>`.

### Releasing

1. Bump `project(Resamper VERSION …)` in `CMakeLists.txt`, move `[Unreleased]` in `CHANGELOG.md` to a
   dated `[x.y.z]` section with its compare link, and commit `🔖 chore(release): vx.y.z`.
2. Tag it with a title and push: `git tag -a vx.y.z -m "vx.y.z — <title>" && git push origin main vx.y.z`.

The [Release workflow](.github/workflows/release.yml) checks the tag against the CMake version, builds
and tests the app, and publishes the release: the CHANGELOG section, the app signed and notarized in a
DMG, and the commit list from [git-cliff](https://git-cliff.org) (`cliff.toml`; preview it with
`git cliff --latest`). Tags are plain `vX.Y.Z`; 0.x releases are published as alpha pre-releases.

## License

Resamper's own code is [MIT](LICENSE). A binary also contains JUCE (AGPLv3 or a commercial JUCE licence) and Tracktion Engine (GPLv3 or a commercial Tracktion licence); GIN is BSD-3-Clause and melatonin_inspector is MIT. The embedded [lucide](https://lucide.dev) icons are ISC / MIT (`Source/UI/Controls/Lucide-LICENSE.txt`). What that means for distribution is in the public [License](#license) section.
