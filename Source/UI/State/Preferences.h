#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace resamper
{

/** The user's preferences: app-wide, not saved with a Project.

    Held in a ValueTree, so a view can listen to it. With a file set (the app
    sets one in the user's application data folder), the file is read then and
    rewritten on every change; without one (tests), preferences live in memory
    only. An absent entry reads as its default. */
class Preferences : private juce::ValueTree::Listener
{
public:
    Preferences();
    ~Preferences() override;

    /** Reads the file (if it exists) and saves every later change to it. */
    void setFile (const juce::File&);

    /** The tree, for listeners. Change it through the setters. */
    juce::ValueTree getState() const   { return state; }

    /** A plug-in's window opens when it is added (PRD §9.6). On by default. */
    bool getAutoOpenPluginWindows() const;
    void setAutoOpenPluginWindows (bool);

    /** Unpinned plug-in windows show only while their track is selected. On by default. */
    bool getPluginWindowsForSelectedTrackOnly() const;
    void setPluginWindowsForSelectedTrackOnly (bool);

    /** The release the user chose Later for. Empty until then. A newer release still asks. */
    juce::String getSkippedUpdateVersion() const;
    void setSkippedUpdateVersion (const juce::String&);

    /** The version that opened last time. Empty until a launch has been recorded. */
    juce::String getLastLaunchedVersion() const;
    void setLastLaunchedVersion (const juce::String&);

    /** The UI Language (ADR-0015): systemLanguage, the default, follows the OS;
        otherwise a language code ("en", "tr"). The app reads it at launch. */
    static constexpr const char* systemLanguage = "system";
    juce::String getLanguage() const;
    void setLanguage (const juce::String&);

    /** True when setFile read an existing preferences file. */
    bool hadSavedPreferences() const noexcept   { return loadedFromFile; }

private:
    juce::ValueTree state { "Preferences" };
    juce::File file;
    bool loadedFromFile = false;

    void save() const;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    JUCE_DECLARE_NON_COPYABLE (Preferences)
};

} // namespace resamper
