#include "Localisation.h"

namespace resamper
{

juce::String fillPlaceholders (const juce::String& text, std::initializer_list<juce::String> values)
{
    juce::String result;
    result.preallocateBytes (text.getNumBytesAsUTF8());

    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        const auto index = (int) (*p) - '1';

        if (c == '%' && index >= 0 && index < 9 && index < (int) values.size())
        {
            result << *(values.begin() + index);
            ++p;
        }
        else
        {
            result << juce::String::charToString (c);
        }
    }

    return result;
}

juce::String toUpperCaseInUILanguage (const juce::String& text)
{
    const auto* mappings = juce::LocalisedStrings::getCurrentMappings();

    if (mappings == nullptr || ! mappings->getCountryCodes().contains ("tr"))
        return text.toUpperCase();

    return text.replace ("i", juce::String::charToString (0x130))
               .replace (juce::String::charToString (0x131), "I")
               .toUpperCase();
}

} // namespace resamper
