#pragma once

#include <juce_core/juce_core.h>

namespace resamper
{

class UIFileSource;

/** The language code a Preferences language means (ADR-0015): "system" (or
    nothing) follows the OS language as SystemStats::getUserLanguage() reports
    it ("tr", "tr-TR"); a code is used as it is. Lower case, without a region;
    English when the OS gives nothing. */
juce::String resolveUILanguage (const juce::String& preference, const juce::String& systemLanguage);

/** Makes the language's translations/<code>.txt JUCE's current
    LocalisedStrings, so TRANS reads it. English, or a language without a
    file, installs none: every key shows its English text. Returns true when a
    mapping was installed. TRANS is read as a component builds its text, so
    the app calls this before it creates any. */
bool installUILanguage (const UIFileSource&, const juce::String& code);

/** The language the last installUILanguage put in place: "en" when none was installed. */
juce::String getInstalledUILanguage();

} // namespace resamper
