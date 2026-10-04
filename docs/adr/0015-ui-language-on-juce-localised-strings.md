# UI Language on JUCE LocalisedStrings, chosen at launch

Resamper's UI text is hard-coded English in `Source/UI`. We want Turkish beside English without closing the door on more languages. JUCE already ships the mechanism: `juce::LocalisedStrings` maps English source text to a translation, and `TRANS ("...")` looks it up. We decided: **the UI Language is a JUCE `LocalisedStrings` mapping, embedded with the app and chosen once at launch.**

- English is the source language: every key is the English text, and an untranslated key shows as English.
- One file per language (`UI/translations/tr.txt`), in JUCE's own format, embedded through `resamper_resources` like the themes and fonts.
- `Preferences` holds `language`: `"system"` (default, read from `SystemStats::getUserLanguage()`) or a language code. `Source/App` installs the mapping before any component is created. A change takes effect on relaunch; the Language menu offers to relaunch now.
- The Language submenu is titled "Language / Dil" in every language, and each language is named in itself ("English", "Türkçe"), so a user who picked the wrong one can find the way back.
- Only `Source/UI` and `Source/App` translate. Commands and the Engine stay language-free: a Command's name is an English key, translated where the UI shows it (`TRANS (command->getName())`).
- Text with values is a template, never a concatenation: `"Track %1"` plus a small helper in `Source/UI/Localisation.h` that fills `%1`, `%2`. Plurals are two keys (singular and plural), enough for English and Turkish.
- Domain terms from CONTEXT.md (Clip, Send, Insert, Track, Rack...) stay English in every language; `docs/i18n/tr-glossary.md` lists what is translated and the tone.
- Numbers and units are not localised: `-6.0 dB` everywhere. Value fields are typed and parsed, and a comma decimal separator would make parsing depend on the language.
- Tests run in English. The Turkish snapshot is taken on request, as PR evidence for overflowing text.

Enforcement: a Semgrep rule fails on a string literal passed to `setText`, `setButtonText`, `setTooltip` or `addItem` in `Source/UI` (text that is never translated carries `// nosemgrep: <rule-id> -- <why>`). A unit test checks that every `TRANS` key in the source and every Command name has an entry in `tr.txt`.

**Considered options:**
- gettext `.po` files. Rejected for now: they bring plural rules, context and translator tools (Poedit, Weblate), but need a parser or a dependency, and JUCE covers two languages. If community translation starts, a build step can convert `.po` to JUCE's format without touching the code.
- Switching language live. Rejected: `TRANS` is read when a component builds its text, so every component would need a "rebuild your text" path. Relaunching is accepted practice in DAWs.
- Translating inside Commands. Rejected: Commands and Engine Undo would carry language, and tests would depend on it.
- Translation files beside the app, editable by users. Deferred: embedding means they can't go missing. A user override file can be added later.

**Consequences:** Every user-visible literal in `Source/UI` moves into `TRANS` or a template, area by area; the Semgrep rule checks changed lines only, so the migration doesn't break CI. Layouts must allow for Turkish text, which runs longer than English. The embedded fonts (Inter, IBM Plex Mono) cover Turkish.
