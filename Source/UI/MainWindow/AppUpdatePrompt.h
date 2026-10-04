#pragma once

#include "App/AppUpdate.h"

#include <vector>

namespace resamper
{

class ThemeManager;

/** The update button, its hint, and the update-complete dialog
    (design/app-update). A newer release shows the button under the top bar.
    When the download is ready, or a new version has just opened, the dialog
    lists the changelog. */
class AppUpdatePrompt : public juce::Component,
                        private UpdateCheck::Listener
{
public:
    explicit AppUpdatePrompt (ThemeManager&);
    ~AppUpdatePrompt() override;

    /** The check this prompt shows and starts. */
    void bind (UpdateCheck&);

    /** A newer release is available. `canReplace` is false for a build-tree copy. */
    void showOffer (const AppRelease&, const juce::String& summary, bool canReplace);

    /** The first launch of a version. Close runs onClose; Restart runs onRestart. */
    void showWelcome (const juce::String& version, std::vector<ReleaseNote> items,
                      std::function<void()> onClose, std::function<void()> onRestart);

    /** Closes the dialog when it is open. */
    bool dismissModal();

    /** A download or install failure, shown by the window as a toast. */
    std::function<void (const juce::String&)> onFailed;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Phase { idle, available, downloading, ready, welcomed };
    enum class Hot { none, pill, close, notes, restart };

    class Notes : public juce::Component
    {
    public:
        explicit Notes (ThemeManager&);

        void setItems (std::vector<ReleaseNote>);
        int preferredHeight (int width) const;
        void paint (juce::Graphics&) override;

    private:
        ThemeManager& themeManager;
        std::vector<ReleaseNote> items;
    };

    struct ClearViewport : juce::Viewport
    {
        void paint (juce::Graphics&) override {}
    };

    void updateAvailable (const AppRelease&, const juce::String& summary, bool canReplace) override;
    void updateProgress (double fraction) override;
    void updateReady (const AppRelease&, const std::vector<ReleaseNote>&) override;
    void updateFailed (const juce::String& message) override;

    void rebuildGeometry();
    void closeModal();
    void activatePill();
    void restart();
    Hot hotAt (juce::Point<int>) const;
    juce::String pillText() const;

    ThemeManager& themeManager;
    juce::WeakReference<UpdateCheck> check;
    Notes noteList;
    ClearViewport noteViewport;

    Phase phase = Phase::idle;
    bool modal = false;
    bool replaceable = true;
    bool installing = false;
    bool pressed = false;
    double fraction = 0;
    juce::String version, summary, page;
    std::vector<ReleaseNote> items;
    std::function<void()> welcomeClose, welcomeRestart;
    Hot hot = Hot::none;

    juce::Rectangle<int> pill, hint, dialog, divider, closeButton, notesButton, restartButton,
                          changelogHeading, changelog;
};

} // namespace resamper
