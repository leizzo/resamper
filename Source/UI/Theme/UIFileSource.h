#pragma once

#include <juce_core/juce_core.h>

namespace resamper
{

/** Reads UI files (themes, fonts) by path relative to the UI/ folder.

    Production builds read the copies embedded as binary resources. In dev
    mode (Debug builds) the files are read from the source tree instead, so an
    edit plus Reload Theme shows up without recompiling.
*/
class UIFileSource
{
public:
    UIFileSource();

    /** Returns the file's text, or fails if it doesn't exist. */
    juce::Result read (const juce::String& relativePath, juce::String& text) const;

    /** Returns the file's bytes (fonts), or fails if it doesn't exist. */
    juce::Result readData (const juce::String& relativePath, juce::MemoryBlock& data) const;

    bool isDevMode() const noexcept             { return devDirectory != juce::File(); }
    juce::File getDevDirectory() const noexcept { return devDirectory; }

private:
    juce::File devDirectory;
};

} // namespace resamper
