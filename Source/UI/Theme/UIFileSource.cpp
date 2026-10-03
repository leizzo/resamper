#include "UIFileSource.h"

#include <ResamperResources.h>

namespace resamper
{

UIFileSource::UIFileSource()
{
   #ifdef RESAMPER_DEV_UI_DIR
    if (juce::File dir (RESAMPER_DEV_UI_DIR); dir.isDirectory())
        devDirectory = dir;
   #endif
}

juce::Result UIFileSource::read (const juce::String& relativePath, juce::String& text) const
{
    juce::MemoryBlock data;

    if (auto r = readData (relativePath, data); r.failed())
        return r;

    text = data.toString();
    return juce::Result::ok();
}

juce::Result UIFileSource::readData (const juce::String& relativePath, juce::MemoryBlock& data) const
{
    if (isDevMode())
    {
        auto file = devDirectory.getChildFile (relativePath);

        if (! file.existsAsFile() || ! file.loadFileAsData (data))
            return juce::Result::fail ("UI file not found: " + file.getFullPathName());

        return juce::Result::ok();
    }

    const auto fileName = relativePath.fromLastOccurrenceOf ("/", false, false);

    for (int i = 0; i < ResamperResources::namedResourceListSize; ++i)
    {
        auto* name = ResamperResources::namedResourceList[i];

        if (fileName == ResamperResources::getNamedResourceOriginalFilename (name))
        {
            int size = 0;
            auto* bytes = ResamperResources::getNamedResource (name, size);
            data.replaceAll (bytes, (size_t) size);
            return juce::Result::ok();
        }
    }

    return juce::Result::fail ("UI file not embedded: " + relativePath);
}

} // namespace resamper
