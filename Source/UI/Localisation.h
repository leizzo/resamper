#pragma once

#include <juce_core/juce_core.h>

#include <initializer_list>

namespace resamper
{

/** The template with %1, %2... replaced by values[0], values[1]... in one
    pass, so a value that itself holds "%2" is left as it is. A placeholder
    without a value stays in the text. */
juce::String fillPlaceholders (const juce::String& text, std::initializer_list<juce::String> values);

/** Text in the UI Language (ADR-0015): the English template is the key in
    UI/translations/<code>.txt, and its translation is filled with args, so
    a language may put them in another order. Values are never concatenated
    onto translated text: "Track %1", not "Track " + n. */
template <typename... Args>
juce::String tr (const juce::String& englishTemplate, const Args&... args)
{
    return fillPlaceholders (juce::translate (englishTemplate), { juce::String (args)... });
}

/** The singular template for one and the plural one otherwise (two keys:
    enough for English and Turkish), with n as %1 and args as %2... */
template <typename... Args>
juce::String trPlural (int n, const juce::String& singular, const juce::String& plural, const Args&... args)
{
    return tr (n == 1 ? singular : plural, n, args...);
}

} // namespace resamper
