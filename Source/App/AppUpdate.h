#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace resamper
{

class Preferences;

/** A published release the app can download. */
struct AppRelease
{
    juce::String version;
    juce::String tag;
    juce::URL downloadUrl;
    juce::String assetName;
};

/** Compares two version strings as numbers, so 0.2.10 is newer than 0.2.2.
    A leading v is ignored and a missing part counts as 0. Returns negative
    when a is older, 0 when they match, positive when a is newer. */
int compareVersions (const juce::String& a, const juce::String& b);

/** The newest published GitHub release newer than current.
    Drafts are skipped. Pre-releases count: 0.x releases are published as
    pre-releases, so the "latest" release would miss them. The macOS disk
    image has to be the official Resamper asset on this repo. */
std::optional<AppRelease> newerRelease (const juce::var& releases, const juce::String& current);

/** True when this process is an installed Resamper.app the updater may
    replace. A copy inside a build tree is left alone. */
bool canReplaceInstalledApp (const juce::File& applicationBundle);

/** One changelog bullet. The title is the first sentence when another follows. */
struct ReleaseNote
{
    juce::String title;
    juce::String description;
};

/** Notes for released versions newer than previous and not newer than current.
    Unreleased notes are left out. An empty previous selects only current. */
juce::String releaseNotesSince (const juce::String& changelog, const juce::String& previous, const juce::String& current);

/** The same notes as rows: a title, and the rest of the bullet as the description. */
std::vector<ReleaseNote> releaseNoteItems (const juce::String& changelog, const juce::String& previous, const juce::String& current);

/** The paragraph under a version heading, before the first category. */
juce::String releaseSummary (const juce::String& changelog, const juce::String& version);

/** Notes to show on this launch. Empty when there is nothing to show; the
    launched version is recorded in that case. When notes are returned, the
    caller records the version once the dialog closes. A first install records
    the version and returns nothing. */
std::vector<ReleaseNote> notesForLaunch (Preferences&, const juce::String& current);

/** Opens the GitHub page for a release tag or version. */
void openReleasePage (const juce::String& tagOrVersion);

/** After this process quits, opens the app that is running now. */
void scheduleRelaunch();

/** Asks GitHub once per launch whether a newer Resamper is published.

    A newer release is handed to the listener, which shows the update button.
    accept() downloads the disk image. installAndQuit() checks its signature
    and restarts into it. Nothing is shown when the check fails or this copy
    is already current. A build-tree copy opens the release in the browser. */
class UpdateCheck : private juce::AsyncUpdater
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void updateAvailable (const AppRelease&, const juce::String& summary, bool canReplace) = 0;
        virtual void updateProgress (double fraction) = 0;
        virtual void updateReady (const AppRelease&, const std::vector<ReleaseNote>&) = 0;
        virtual void updateFailed (const juce::String& message) = 0;
    };

    UpdateCheck (juce::String currentVersion, std::function<void()> quit);
    ~UpdateCheck() override;

    void setListener (Listener* next) noexcept   { listener = next; }

    void start();

    /** Download the offered release, or open its page when this copy cannot be replaced. */
    void accept();

    /** Check the downloaded disk image and restart into it. */
    void installAndQuit();

private:
    enum class Job { check, download, install };
    enum class Notice { none, releaseFound, progress, failed, readyToInstall, installed };

    struct Worker;

    void perform();
    void startJob (Job);
    void checkForRelease();
    void downloadRelease();
    void installRelease();
    void offer (const AppRelease&);
    void post (Notice, juce::String error = {}, bool openImage = false);
    void handleAsyncUpdate() override;

    juce::String currentVersion;
    std::function<void()> onQuit;
    Listener* listener = nullptr;

    AppRelease offered;
    AppRelease pendingRelease;
    juce::File destinationApp;
    juce::File downloadedFile;
    bool replaceInstalled = false;

    juce::CriticalSection lock;
    Notice notice = Notice::none;
    juce::String pendingError;
    bool openDiskImage = false;

    std::atomic<bool> cancel { false };
    std::atomic<double> progress { 0 };
    double shownProgress = 0;

    Job job = Job::check;
    std::unique_ptr<Worker> worker;

    JUCE_DECLARE_WEAK_REFERENCEABLE (UpdateCheck)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdateCheck)
};

} // namespace resamper
