## C++ project

### Toolchain

- C++20 (`CMAKE_CXX_STANDARD 20`, extensions off), CMake ≥ 3.22, Ninja.
- macOS: Apple Clang from the Xcode command-line tools; deployment target 10.15.
- Dependencies are pinned git submodules under `external/` (JUCE, Tracktion Engine, GIN). Never edit them; see README "For developers" for the init commands.

### Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # once
cmake --build build
```

Source and test files are listed explicitly in `CMakeLists.txt` (no globbing) — add every new `.cpp` to its target there.

### Test

```sh
cmake --build build --target ResamperTests
build/ResamperTests_artefacts/Debug/ResamperTests "Mixer"   # implementing: each UnitTest name the change touches
build/ResamperTests_artefacts/Debug/ResamperTests           # opening a PR: no filter
```

Tests are headless `juce::UnitTest` suites in category `"Resamper"`, built on `Tests/TestFixture.h`, and drive the app through Commands / the Application Model. The run prints `ALL TESTS PASSED` or `N FAILURE(S)` and exits non-zero on failure. While implementing, pass each `UnitTest` name the change touches. The unfiltered binary waits until the user asks to open the pull request.

### Architecture rule

Only `resamper_engine` (`Source/Engine/`) sees Tracktion headers. `Source/UI`, `Source/Commands` and `Source/App` are compiled without Tracktion on the include path, so including one there is a compile error — add a facade method in `Source/Engine` instead. Tests may include Tracktion.

JUCE and GIN modules are INTERFACE targets that compile their sources into every target that links them, so only `resamper_engine` links them — each module is built once. Other targets get the headers through `resamper_use_engine_without_tracktion`; never add a `juce::juce_*` or `gin*` module to their `target_link_libraries`. GIN has no Tracktion dependency, so `Source/UI` may include `<gin/...>` directly. Register only the GIN modules the code uses: a new one goes in both `juce_add_module(...)` and `resamper_engine`'s link list. GIN must build against the JUCE that Tracktion pins; recheck that whenever either submodule moves.

### Style

There is no `.clang-format`: no clang-format setting reproduces the JUCE lambda braces and hand-aligned lists below, so match the surrounding code, which follows JUCE style:

- 4-space indent, Allman braces, space before the parenthesis of calls and declarations: `foo (a, b)`, `if (! x)`.
- `namespace resamper`; file-local helpers in an anonymous namespace; `namespace te = tracktion;` in `.cpp` files.
- camelCase functions and variables, PascalCase types, no member prefixes; `juce::String` / `juce::Result` at API boundaries.
- A level that crosses the Engine, Commands or UI boundary is a `Decibels` (`Source/Engine/Decibels.h`), never a bare `double`; DSP code reads `.value` where it does the maths.
- `/** ... */` doc comments on public types and methods; `#pragma once` in headers.
- Warnings come from `juce::juce_recommended_warning_flags`; keep builds warning-free.

### Deterministic lint

```sh
scripts/lint.sh               # lines changed since origin/main; run before perch
```

It needs `brew install llvm` and `uv tool install semgrep`, and a configured `build/` (for `compile_commands.json`). CI runs it on every pull request.

- **Semgrep** (`.semgrep/resamper.yml`) fails on literal colours in `Source/UI` (use a Theme entry), literal sample rates, a deferred callback (`callAsync`, `callAfterDelay`) capturing `this`, `&` or `=`, and explicit `delete`. A justified exception carries its reason on the line: `// nosemgrep: <rule-id> -- <why>`.
- **clang-tidy** (`.clang-tidy`) fails on bugprone and performance findings in the lines you changed; stage a new file (`git add`) so it is checked.

A rule a pattern can decide belongs here, not in perch: it is exact, free and needs no model. perch keeps the rules that need judgement.

### Semantic lint (perch)

`perch` reads methods with a model and flags defects, security issues and lint a compiler can't see. It needs `PERCH_API_KEY` (env or a `.env` beside the repo); `perch doctor` checks setup. Use the `perch` skill for the full workflow.

```sh
perch check Source/Engine/Mixer.cpp::setSendGain   # one method, uncommitted work — run after editing it
perch scan --since origin/main                     # everything changed on the branch — run before a PR
perch issues [issue-id]                            # list findings worst first, or show one
perch close <issue-id> --reason "..."              # set aside a false positive, with the reason
```

`check` and `scan` exit 3 while something is still wrong. Results live in `.perch/`; only `closed.jsonl` and `rules/` there are committed. Custom rules go in `perch.yaml` or `.perch/rules/*.yaml` (`perch rules add ...`).

When the user asks to open a pull request, run the unfiltered `ResamperTests` before creating it. On `N FAILURE(S)`, explain each failed test from the log and stop. On `ALL TESTS PASSED`, continue in this order: `scripts/lint.sh` exits 0 → `perch scan --since origin/main` exits 0 (fix or `close` every finding) → run the `code-review` skill against `main` → attach test evidence. perch covers method-level defects, code-review covers repo standards and the spec; neither replaces the other, and perch goes first so the review sees final code. Evidence is a screenshot when one frame shows the result, a video when the result is motion or a sequence, or one sentence when the change never draws. See `docs/agents/pr-evidence.md`.

## Agent skills

### Issue tracker

Issues are tracked as GitHub issues on this repo's GitHub remote, using the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Default label vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout — one `CONTEXT.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.
