#include "UILanguage.h"
#include "UI/State/Preferences.h"
#include "UI/Theme/UIFileSource.h"

namespace resamper
{

namespace
{
    constexpr const char* english = "en";

    juce::String& installedLanguage()
    {
        static juce::String code { english };
        return code;
    }
}

juce::String resolveUILanguage (const juce::String& preference, const juce::String& systemLanguage)
{
    const auto followSystem = preference.isEmpty() || preference == Preferences::systemLanguage;
    const auto code = (followSystem ? systemLanguage : preference).upToFirstOccurrenceOf ("-", false, false)
                                                                  .upToFirstOccurrenceOf ("_", false, false)
                                                                  .trim()
                                                                  .toLowerCase();

    return code.isEmpty() ? juce::String (english) : code;
}

bool installUILanguage (const UIFileSource& files, const juce::String& code)
{
    juce::String text;
    const auto found = code != english && code.containsOnly ("abcdefghijklmnopqrstuvwxyz")
                       && files.read ("translations/" + code + ".txt", text).wasOk();

    juce::LocalisedStrings::setCurrentMappings (found ? new juce::LocalisedStrings (text, false) : nullptr);
    installedLanguage() = found ? code : juce::String (english);
    return found;
}

juce::String getInstalledUILanguage()
{
    return installedLanguage();
}

} // namespace resamper
