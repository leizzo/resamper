# project.json owns only app-level metadata

Tracktion's `.tracktionedit` file already serializes tempo, time signature, and audio-file references (via `te::SourceFileReference`), and `te::EditFileOperations` covers save/save-as. Duplicating any of that into `project.json` would create a "which value wins on load?" bug class. We decided `project.json` holds only what the engine does not: a format `version` field and UI session state (zoom, scroll, selection references). Rule of thumb: **if the engine serializes it, project.json doesn't.**

**Considered options:** (a) No project.json at all — rejected because the format version field is required from day one (brief constraint #7) and earns the file's existence. (c) Full parallel metadata per the original brief §27 — rejected as duplication of engine state.

**Consequences:** A Project is exactly one folder containing one `.tracktionedit` Edit, one `project.json`, and media/cache subfolders. Migration logic, when needed later, keys off `project.json`'s `version`.
