#pragma once

#include "UI/Theme/ThemeManager.h"

namespace resamper
{

/** The Developer Mode status bar: the project, the audio device and the UI
    file source, in muted text. The parent sets the text; this component does
    not query the engine. */
class StatusBar : public juce::Component,
                  private ThemeManager::Listener
{
public:
    explicit StatusBar (ThemeManager&);
    ~StatusBar() override;

    void setProject (const juce::String&);
    void setDevice (const juce::String&);
    void setMode (const juce::String&);

    const juce::String& getProject() const noexcept   { return project; }
    const juce::String& getDevice() const noexcept    { return device; }
    const juce::String& getMode() const noexcept      { return mode; }

    void paint (juce::Graphics&) override;

private:
    ThemeManager& themes;
    juce::String project, device, mode;

    void setField (juce::String& field, const juce::String& text);
    void themeChanged() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusBar)
};

} // namespace resamper
