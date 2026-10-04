#include "AppUpdate.h"

#include "UI/State/Preferences.h"

#include <ResamperResources.h>

#if JUCE_MAC
 #include <unistd.h>
#endif

namespace resamper
{

namespace
{
    constexpr juce::int64 minimumDiskImageBytes = 1024 * 1024;

    juce::Array<int> versionParts (const juce::String& version)
    {
        auto text = version.trim();

        if (text.startsWithChar ('v') || text.startsWithChar ('V'))
            text = text.substring (1);

        juce::StringArray tokens;
        tokens.addTokens (text, ".", "");
        tokens.removeEmptyStrings();

        juce::Array<int> parts;

        for (const auto& token : tokens)
            parts.add (token.getIntValue());

        return parts;
    }

    bool isSafeTag (const juce::String& tag)
    {
        if (tag.isEmpty() || tag.length() > 32)
            return false;

        auto body = tag;

        if (body.startsWithChar ('v') || body.startsWithChar ('V'))
            body = body.substring (1);

        if (body.isEmpty() || body.startsWithChar ('.') || body.endsWithChar ('.') || body.contains (".."))
            return false;

        return body.containsOnly ("0123456789.");
    }

    juce::String versionOfTag (const juce::String& tag)
    {
        if (tag.startsWithChar ('v') || tag.startsWithChar ('V'))
            return tag.substring (1);

        return tag;
    }

    std::optional<AppRelease> releaseAsset (const juce::var& release, const juce::String& tag, const juce::String& version)
    {
        auto* assets = release["assets"].getArray();

        if (assets == nullptr)
            return std::nullopt;

        const auto assetName = "Resamper-" + version + "-macOS.dmg";
        const auto expectedPath = "leizzo/resamper/releases/download/" + tag + "/" + assetName;

        for (const auto& asset : *assets)
        {
            if (asset["name"].toString() != assetName)
                continue;

            const juce::URL url (asset["browser_download_url"].toString());
            auto path = url.getSubPath();

            if (path.startsWithChar ('/'))
                path = path.substring (1);

            if (url.getScheme() != "https" || url.getDomain() != "github.com" || path != expectedPath)
                continue;

            AppRelease parsed;
            parsed.version = version;
            parsed.tag = tag;
            parsed.downloadUrl = url;
            parsed.assetName = assetName;
            return parsed;
        }

        return std::nullopt;
    }

    std::optional<AppRelease> fetchNewerRelease (const juce::String& current)
    {
        const juce::URL url ("https://api.github.com/repos/leizzo/resamper/releases?per_page=20");
        int status = 0;
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withExtraHeaders ("Accept: application/vnd.github+json\r\n"
                                                    "User-Agent: Resamper\r\n"
                                                    "X-GitHub-Api-Version: 2022-11-28\r\n")
                                 .withConnectionTimeoutMs (15000)
                                 .withStatusCode (&status)
                                 .withNumRedirectsToFollow (5);

        const auto stream = url.createInputStream (options);

        if (stream == nullptr || status != 200)
            return std::nullopt;

        return newerRelease (juce::JSON::parse (stream->readEntireStreamAsString()), current);
    }

    struct CommandOutput
    {
        int exitCode = -1;
        juce::String text;
    };

    CommandOutput runCommand (const juce::StringArray& args, int timeoutMs,
                              const std::atomic<bool>& cancel, juce::Thread& thread)
    {
        CommandOutput result;
        juce::ChildProcess process;

        if (! process.start (args))
            return result;

        const auto start = juce::Time::getMillisecondCounter();

        while (! process.waitForProcessToFinish (200))
        {
            if (cancel.load() || thread.threadShouldExit())
            {
                process.kill();
                return result;
            }

            if (juce::Time::getMillisecondCounter() - start > (juce::uint32) timeoutMs)
            {
                process.kill();
                return result;
            }
        }

        result.text = process.readAllProcessOutput();
        result.exitCode = static_cast<int> (process.getExitCode());
        return result;
    }

   #if JUCE_MAC
    // The running bundle can't be replaced in place. This script waits until the
    // app has quit, then swaps the new copy in and opens it.
    constexpr const char* swapScript = R"sh(#!/bin/sh
pid="$1"
dest="$2"
src="$3"
dmg="$4"
while /bin/kill -0 "$pid" 2>/dev/null; do
    /bin/sleep 0.2
done
/bin/sleep 0.5
/usr/bin/osascript -e 'display notification "Installing the update…" with title "Resamper"' >/dev/null 2>&1
updated="$(/usr/bin/dirname "$dest")/Resamper.app.new"
/bin/rm -rf "$updated"
/bin/mkdir "$updated"
if /usr/bin/ditto "$src" "$updated/Resamper.app"; then
    /bin/rm -rf "$dest"
    if /bin/mv "$updated/Resamper.app" "$dest"; then
        /bin/rm -rf "$updated"
        /usr/bin/xattr -dr com.apple.quarantine "$dest" 2>/dev/null
        /usr/bin/open "$dest"
        /bin/rm -rf "$(/usr/bin/dirname "$0")"
        /bin/rm -f "$dmg"
        exit 0
    fi
    /usr/bin/open "$updated/Resamper.app"
fi
/usr/bin/open "$dmg"
/usr/bin/osascript -e 'display dialog "Resamper downloaded the update but could not replace the installed app. The disk image is open so you can drag Resamper to Applications." buttons {"OK"} default button 1' >/dev/null 2>&1
)sh";

    juce::Result verifySignedApp (const juce::File& app, const std::atomic<bool>& cancel, juce::Thread& thread)
    {
        runCommand ({ "/usr/bin/xattr", "-dr", "com.apple.quarantine", app.getFullPathName() }, 30000, cancel, thread);

        const auto verified = runCommand ({ "/usr/bin/codesign", "--verify", "--strict", app.getFullPathName() },
                                          60000, cancel, thread);

        if (verified.exitCode != 0)
            return juce::Result::fail ("The downloaded app did not pass macOS signature checks.");

        const auto identified = runCommand ({ "/usr/bin/codesign", "-d", "--verbose=2", app.getFullPathName() },
                                            30000, cancel, thread);

        if (identified.exitCode != 0 || ! identified.text.contains ("Identifier=com.resamper.Resamper"))
            return juce::Result::fail ("The downloaded app did not pass macOS signature checks.");

        const auto assessed = runCommand ({ "/usr/sbin/spctl", "--assess", "--type", "execute", app.getFullPathName() },
                                          60000, cancel, thread);

        if (assessed.exitCode != 0)
            return juce::Result::fail ("The downloaded app did not pass macOS signature checks.");

        return juce::Result::ok();
    }

    struct InstallOutcome
    {
        juce::Result result = juce::Result::ok();
        bool openDiskImage = false;
    };

    InstallOutcome spawnSwap (const juce::File& script, const juce::File& destination,
                              const juce::File& stagedApp, const juce::File& dmg)
    {
        juce::ChildProcess process;

        if (! process.start ({ "/usr/bin/nohup", "/bin/sh", script.getFullPathName(),
                               juce::String (static_cast<juce::int64> (getpid())),
                               destination.getFullPathName(),
                               stagedApp.getFullPathName(),
                               dmg.getFullPathName() },
                             0))
            return { juce::Result::fail ("The update could not replace the installed app."), true };

        return {};
    }

    InstallOutcome installDiskImage (const juce::File& dmg, const juce::File& destination,
                                     const std::atomic<bool>& cancel, juce::Thread& thread)
    {
        if (! canReplaceInstalledApp (destination))
            return { juce::Result::fail ("This copy of Resamper can't be replaced."), false };

        runCommand ({ "/usr/bin/xattr", "-d", "com.apple.quarantine", dmg.getFullPathName() }, 30000, cancel, thread);

        if (cancel.load() || thread.threadShouldExit())
            return { juce::Result::fail ("The update was cancelled."), false };

        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ResamperUpdate");

        struct MountedDisk
        {
            juce::File point;
            bool attached = false;
            const std::atomic<bool>& cancel;
            juce::Thread& thread;

            ~MountedDisk()
            {
                if (attached)
                    runCommand ({ "/usr/bin/hdiutil", "detach", point.getFullPathName(), "-force" }, 30000, cancel, thread);

                point.deleteRecursively();
            }
        };

        MountedDisk mount { root.getChildFile ("mount-" + juce::Uuid().toString()), false, cancel, thread };

        if (mount.point.createDirectory().failed())
            return { juce::Result::fail ("The disk image could not be opened."), false };

        const auto attached = runCommand ({ "/usr/bin/hdiutil", "attach", "-nobrowse", "-readonly",
                                            "-mountpoint", mount.point.getFullPathName(), dmg.getFullPathName() },
                                          60000, cancel, thread);

        if (attached.exitCode != 0)
            return { juce::Result::fail ("The disk image could not be opened."), false };

        mount.attached = true;
        const auto mountedApp = mount.point.getChildFile ("Resamper.app");

        if (! mountedApp.isDirectory())
            return { juce::Result::fail ("The disk image does not contain Resamper."), false };

        struct StagedCopy
        {
            juce::File dir;
            bool keep = false;

            ~StagedCopy()
            {
                if (! keep)
                    dir.deleteRecursively();
            }
        };

        StagedCopy staging { root.getChildFile ("stage-" + juce::Uuid().toString()), false };

        if (staging.dir.createDirectory().failed())
            return { juce::Result::fail ("The update could not replace the installed app."), false };

        const auto stagedApp = staging.dir.getChildFile ("Resamper.app");
        const auto copied = runCommand ({ "/usr/bin/ditto", mountedApp.getFullPathName(), stagedApp.getFullPathName() },
                                        120000, cancel, thread);

        if (copied.exitCode != 0)
            return { juce::Result::fail ("The update could not replace the installed app."), false };

        runCommand ({ "/usr/bin/hdiutil", "detach", mount.point.getFullPathName(), "-force" }, 30000, cancel, thread);
        mount.attached = false;

        if (cancel.load() || thread.threadShouldExit())
            return { juce::Result::fail ("The update was cancelled."), false };

        if (auto verified = verifySignedApp (stagedApp, cancel, thread); verified.failed())
            return { verified, false };

        const auto script = staging.dir.getChildFile ("swap.sh");

        if (! script.replaceWithText (swapScript))
            return { juce::Result::fail ("The update could not replace the installed app."), true };

        if (auto spawned = spawnSwap (script, destination, stagedApp, dmg); spawned.result.failed())
            return spawned;

        staging.keep = true;
        return {};
    }
   #else
    struct InstallOutcome
    {
        juce::Result result = juce::Result::ok();
        bool openDiskImage = false;
    };

    InstallOutcome installDiskImage (const juce::File& dmg, const juce::File& destination,
                                     const std::atomic<bool>& cancel, juce::Thread& thread)
    {
        juce::ignoreUnused (dmg, destination, cancel, thread);
        return { juce::Result::fail ("Updates install on macOS."), false };
    }
   #endif

    struct NoteSection
    {
        juce::String version;
        juce::String date;
        juce::String body;
    };

    bool isReleaseVersion (const juce::String& text)
    {
        return text.isNotEmpty()
            && text.containsOnly ("0123456789.")
            && ! text.startsWithChar ('.')
            && ! text.endsWithChar ('.')
            && ! text.contains ("..");
    }

    bool isLinkReference (const juce::String& trimmed)
    {
        return trimmed.startsWithChar ('[') && trimmed.contains ("]:");
    }

    juce::String stripEmphasis (juce::String text)
    {
        return text.replace ("**", {});
    }

    juce::String formatBody (const juce::String& raw)
    {
        juce::StringArray lines;
        lines.addLines (raw);
        juce::String out;

        for (const auto& line : lines)
        {
            if (line.trim().isEmpty())
            {
                if (out.isNotEmpty() && ! out.endsWith ("\n\n"))
                    out << "\n";

                continue;
            }

            if (isLinkReference (line.trim()))
                continue;

            if (line.startsWith (" ") || line.startsWith ("\t"))
            {
                if (out.endsWithChar ('\n'))
                    out = out.dropLastCharacters (1);

                out << " " << stripEmphasis (line.trim()) << "\n";
                continue;
            }

            const auto trimmed = line.trim();

            if (trimmed.startsWith ("### "))
            {
                if (out.isNotEmpty() && ! out.endsWith ("\n\n"))
                    out << "\n";

                out << stripEmphasis (trimmed.substring (4)) << "\n";
                continue;
            }

            if (trimmed.startsWith ("- "))
            {
                out << "• " << stripEmphasis (trimmed.substring (2)) << "\n";
                continue;
            }

            out << stripEmphasis (trimmed) << "\n";
        }

        return out.trim();
    }

    juce::Array<NoteSection> changelogSections (const juce::String& changelog)
    {
        juce::Array<NoteSection> sections;
        NoteSection current;
        auto collecting = false;
        juce::StringArray lines;
        lines.addLines (changelog);

        auto flush = [&]
        {
            if (collecting && isReleaseVersion (current.version))
                sections.add (current);

            current.version = {};
            current.date = {};
            current.body = {};
            collecting = false;
        };

        for (const auto& line : lines)
        {
            if (line.startsWith ("## ["))
            {
                flush();
                current.version = line.fromFirstOccurrenceOf ("[", false, false)
                                      .upToFirstOccurrenceOf ("]", false, false)
                                      .trim();
                current.date = line.fromFirstOccurrenceOf ("]", false, false)
                                   .fromFirstOccurrenceOf ("-", false, false)
                                   .trim();
                collecting = isReleaseVersion (current.version);
                continue;
            }

            if (collecting)
                current.body << line << "\n";
        }

        flush();
        return sections;
    }

    juce::String loadChangelog()
    {
       #ifdef RESAMPER_DEV_UI_DIR
        const auto file = juce::File (RESAMPER_DEV_UI_DIR).getParentDirectory().getChildFile ("CHANGELOG.md");

        if (file.existsAsFile())
            return file.loadFileAsString();
       #endif

        for (int i = 0; i < ResamperResources::namedResourceListSize; ++i)
        {
            auto* name = ResamperResources::namedResourceList[i];

            if (juce::String (ResamperResources::getNamedResourceOriginalFilename (name)) != "CHANGELOG.md")
                continue;

            int size = 0;
            auto* bytes = ResamperResources::getNamedResource (name, size);
            return juce::String::fromUTF8 (bytes, size);
        }

        return {};
    }

    constexpr int noteTitleLimit = 72;

    ReleaseNote splitNote (juce::String text)
    {
        ReleaseNote note;
        text = text.trim();
        const auto cut = text.indexOf (". ");

        if (cut > 0 && cut < noteTitleLimit && cut + 2 < text.length())
        {
            note.title = text.substring (0, cut);
            note.description = text.substring (cut + 2).trim();
            return note;
        }

        note.title = text;
        return note;
    }

    std::vector<ReleaseNote> notesInBody (const juce::String& body)
    {
        std::vector<ReleaseNote> notes;
        juce::StringArray lines;
        lines.addLines (body);
        juce::String current;

        auto flush = [&]
        {
            if (current.isEmpty())
                return;

            notes.push_back (splitNote (current));
            current.clear();
        };

        for (const auto& line : lines)
        {
            if (line.startsWith (" ") || line.startsWith ("\t"))
            {
                if (current.isNotEmpty())
                    current << " " << stripEmphasis (line.trim());

                continue;
            }

            const auto trimmed = line.trim();

            if (trimmed.isEmpty() || isLinkReference (trimmed) || trimmed.startsWith ("### "))
                continue;

            if (trimmed.startsWith ("- "))
            {
                flush();
                current = stripEmphasis (trimmed.substring (2));
            }
        }

        flush();
        return notes;
    }

    juce::String introOf (const juce::String& body)
    {
        juce::StringArray lines;
        lines.addLines (body);
        juce::String intro;

        for (const auto& line : lines)
        {
            const auto trimmed = line.trim();

            if (trimmed.startsWith ("### ") || trimmed.startsWith ("- "))
                break;

            if (trimmed.isEmpty())
            {
                if (intro.isNotEmpty())
                    break;

                continue;
            }

            if (isLinkReference (trimmed))
                continue;

            if (intro.isNotEmpty())
                intro << " ";

            intro << stripEmphasis (trimmed);
        }

        return intro;
    }
}

int compareVersions (const juce::String& a, const juce::String& b)
{
    const auto left = versionParts (a);
    const auto right = versionParts (b);
    const auto count = juce::jmax (left.size(), right.size());

    for (int i = 0; i < count; ++i)
    {
        const auto x = i < left.size() ? left[i] : 0;
        const auto y = i < right.size() ? right[i] : 0;

        if (x != y)
            return x < y ? -1 : 1;
    }

    return 0;
}

std::optional<AppRelease> newerRelease (const juce::var& releases, const juce::String& current)
{
    auto* list = releases.getArray();

    if (list == nullptr)
        return std::nullopt;

    std::optional<AppRelease> best;

    for (const auto& item : *list)
    {
        if ((bool) item["draft"])
            continue;

        const auto tag = item["tag_name"].toString().trim();

        if (! isSafeTag (tag))
            continue;

        const auto version = versionOfTag (tag);

        if (compareVersions (version, current) <= 0)
            continue;

        if (best.has_value() && compareVersions (version, best->version) <= 0)
            continue;

        if (auto parsed = releaseAsset (item, tag, version))
            best = std::move (parsed);
    }

    return best;
}

bool canReplaceInstalledApp (const juce::File& bundle)
{
    if (! bundle.isDirectory())
        return false;

    const auto path = bundle.getFullPathName();

    if (! path.startsWithChar ('/') || bundle.getFileName() != "Resamper.app")
        return false;

    if (path.contains ("/build/") || path.contains ("cmake-build") || path.contains ("Resamper_artefacts"))
        return false;

    return bundle.getChildFile ("Contents/Info.plist").existsAsFile();
}

juce::String releaseNotesSince (const juce::String& changelog, const juce::String& previous, const juce::String& current)
{
    if (current.isEmpty())
        return {};

    juce::String notes;

    for (const auto& section : changelogSections (changelog))
    {
        const auto afterPrevious = previous.isEmpty()
                                       ? compareVersions (section.version, current) == 0
                                       : compareVersions (section.version, previous) > 0;

        if (! afterPrevious || compareVersions (section.version, current) > 0)
            continue;

        const auto body = formatBody (section.body);

        if (body.isEmpty())
            continue;

        if (notes.isNotEmpty())
            notes << "\n\n";

        notes << section.version;

        if (section.date.isNotEmpty())
            notes << " — " << section.date;

        notes << "\n\n" << body;
    }

    return notes;
}

namespace
{
    bool sectionInRange (const juce::String& version, const juce::String& previous, const juce::String& current)
    {
        const auto afterPrevious = previous.isEmpty() ? compareVersions (version, current) == 0
                                                      : compareVersions (version, previous) > 0;

        return afterPrevious && compareVersions (version, current) <= 0;
    }
}

std::vector<ReleaseNote> releaseNoteItems (const juce::String& changelog, const juce::String& previous, const juce::String& current)
{
    std::vector<ReleaseNote> notes;

    if (current.isEmpty())
        return notes;

    for (const auto& section : changelogSections (changelog))
    {
        if (! sectionInRange (section.version, previous, current))
            continue;

        for (auto& note : notesInBody (section.body))
            notes.push_back (std::move (note));
    }

    return notes;
}

juce::String releaseSummary (const juce::String& changelog, const juce::String& version)
{
    if (version.isEmpty())
        return {};

    for (const auto& section : changelogSections (changelog))
        if (compareVersions (section.version, version) == 0)
            return introOf (section.body);

    return {};
}

std::vector<ReleaseNote> notesForLaunch (Preferences& preferences, const juce::String& current)
{
    const auto previous = preferences.getLastLaunchedVersion();
    auto notes = releaseNoteItems (loadChangelog(), previous, current);
    const auto upgraded = previous.isNotEmpty() && compareVersions (current, previous) > 0;
    const auto existingInstall = previous.isEmpty() && preferences.hadSavedPreferences();

    if (notes.empty() || (! upgraded && ! existingInstall))
    {
        if (previous != current)
            preferences.setLastLaunchedVersion (current);

        return {};
    }

    return notes;
}

void openReleasePage (const juce::String& tagOrVersion)
{
    auto tag = tagOrVersion.trim();

    if (tag.isEmpty())
        return;

    if (! tag.startsWithChar ('v') && ! tag.startsWithChar ('V'))
        tag = "v" + tag;

    juce::URL ("https://github.com/leizzo/resamper/releases/tag/" + tag).launchInDefaultBrowser();
}

void scheduleRelaunch()
{
   #if JUCE_MAC
    constexpr const char* scriptText = R"sh(#!/bin/sh
pid="$1"
dest="$2"
while /bin/kill -0 "$pid" 2>/dev/null; do
    /bin/sleep 0.2
done
/bin/sleep 0.3
/usr/bin/open "$dest"
/bin/rm -f "$0"
)sh";

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ResamperUpdate");

    if (dir.createDirectory().failed())
        return;

    const auto script = dir.getChildFile ("relaunch.sh");

    if (! script.replaceWithText (scriptText))
        return;

    const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    juce::ChildProcess process;
    process.start ({ "/usr/bin/nohup", "/bin/sh", script.getFullPathName(),
                     juce::String (static_cast<juce::int64> (getpid())),
                     app.getFullPathName() },
                   0);
   #endif
}

struct UpdateCheck::Worker : juce::Thread
{
    explicit Worker (UpdateCheck& o) : juce::Thread ("Resamper Update"), owner (o) {}

    void run() override
    {
        owner.perform();
    }

    UpdateCheck& owner;
};

UpdateCheck::UpdateCheck (juce::String version, std::function<void()> quit)
    : currentVersion (std::move (version)),
      onQuit (std::move (quit))
{
}

UpdateCheck::~UpdateCheck()
{
    listener = nullptr;
    cancel.store (true);

    if (worker != nullptr)
        worker->stopThread (120000);

    cancelPendingUpdate();
}

void UpdateCheck::start()
{
    if (worker != nullptr || currentVersion.isEmpty())
        return;

    startJob (Job::check);
}

void UpdateCheck::perform()
{
    switch (job)
    {
        case Job::check:    checkForRelease(); break;
        case Job::download: downloadRelease(); break;
        case Job::install:  installRelease(); break;
    }
}

void UpdateCheck::startJob (Job next)
{
    if (worker == nullptr)
        worker = std::make_unique<Worker> (*this);
    else if (worker->isThreadRunning())
        worker->waitForThreadToExit (120000);

    if (cancel.load() && next != Job::check)
        return;

    job = next;

    if (! worker->startThread() && next != Job::check)
        post (Notice::failed, "The update could not be downloaded.");
}

void UpdateCheck::checkForRelease()
{
    auto release = fetchNewerRelease (currentVersion);

    if (! release.has_value() || cancel.load() || (worker != nullptr && worker->threadShouldExit()))
        return;

    {
        const juce::ScopedLock sl (lock);
        pendingRelease = *release;
    }

    post (Notice::releaseFound);
}

void UpdateCheck::downloadRelease()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ResamperUpdate");

    if (dir.createDirectory().failed())
    {
        post (Notice::failed, "The update could not be downloaded.");
        return;
    }

    downloadedFile = dir.getChildFile (offered.assetName);
    downloadedFile.deleteFile();

    int status = 0;
    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                             .withExtraHeaders ("User-Agent: Resamper\r\n")
                             .withConnectionTimeoutMs (20000)
                             .withStatusCode (&status)
                             .withNumRedirectsToFollow (8);
    const auto stream = offered.downloadUrl.createInputStream (options);

    if (cancel.load() || (worker != nullptr && worker->threadShouldExit()))
    {
        downloadedFile.deleteFile();
        return;
    }

    if (stream == nullptr || status != 200)
    {
        post (Notice::failed, "The update could not be downloaded.");
        return;
    }

    const auto total = stream->getTotalLength();
    juce::FileOutputStream out (downloadedFile);

    if (out.failedToOpen())
    {
        post (Notice::failed, "The update could not be downloaded.");
        return;
    }

    constexpr int chunk = 64 * 1024;
    juce::HeapBlock<char> buffer ((size_t) chunk);
    juce::int64 got = 0;

    while (! cancel.load() && ! worker->threadShouldExit())
    {
        const auto n = stream->read (buffer.get(), chunk);

        if (n < 0)
        {
            downloadedFile.deleteFile();
            post (Notice::failed, "The update could not be downloaded.");
            return;
        }

        if (n == 0)
            break;

        if (! out.write (buffer.get(), (size_t) n))
        {
            downloadedFile.deleteFile();
            post (Notice::failed, "The update could not be downloaded.");
            return;
        }

        got += n;

        if (total > 0)
        {
            progress.store ((double) got / (double) total);
            post (Notice::progress);
        }
    }

    out.flush();

    if (cancel.load() || worker->threadShouldExit())
    {
        downloadedFile.deleteFile();
        return;
    }

    if (got < minimumDiskImageBytes || (total > 0 && got != total))
    {
        downloadedFile.deleteFile();
        post (Notice::failed, "The update could not be downloaded.");
        return;
    }

    post (Notice::readyToInstall);
}

void UpdateCheck::installRelease()
{
    const auto outcome = installDiskImage (downloadedFile, destinationApp, cancel, *worker);

    if (cancel.load() || (worker != nullptr && worker->threadShouldExit()))
        return;

    if (outcome.result.wasOk())
        post (Notice::installed);
    else
        post (Notice::failed, outcome.result.getErrorMessage(), outcome.openDiskImage);
}

void UpdateCheck::offer (const AppRelease& release)
{
    if (compareVersions (release.version, currentVersion) <= 0)
        return;

    destinationApp = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    replaceInstalled = canReplaceInstalledApp (destinationApp);
    offered = release;

    if (listener != nullptr)
        listener->updateAvailable (release, releaseSummary (loadChangelog(), release.version), replaceInstalled);
}

void UpdateCheck::accept()
{
    if (offered.version.isEmpty())
        return;

    if (! replaceInstalled)
    {
        openReleasePage (offered.tag);
        return;
    }

    shownProgress = 0;
    progress.store (0);
    cancel.store (false);
    startJob (Job::download);
}

void UpdateCheck::installAndQuit()
{
    if (! downloadedFile.existsAsFile())
        return;

    cancel.store (false);
    startJob (Job::install);
}

void UpdateCheck::post (Notice next, juce::String error, bool openImage)
{
    if (cancel.load())
        return;

    const juce::ScopedLock sl (lock);

    if (next == Notice::progress && notice != Notice::none && notice != Notice::progress)
        return;

    notice = next;

    if (error.isNotEmpty())
        pendingError = std::move (error);

    if (openImage)
        openDiskImage = true;

    triggerAsyncUpdate();
}

void UpdateCheck::handleAsyncUpdate()
{
    Notice next = Notice::none;
    juce::String error;
    auto openImage = false;
    AppRelease found;

    {
        const juce::ScopedLock sl (lock);
        next = notice;
        notice = Notice::none;
        error = pendingError;
        openImage = openDiskImage;
        pendingError.clear();
        openDiskImage = false;

        if (next == Notice::releaseFound)
            found = pendingRelease;
    }

    shownProgress = progress.load();

    if (cancel.load() && next != Notice::progress)
        return;

    switch (next)
    {
        case Notice::releaseFound:
            offer (found);
            break;

        case Notice::failed:
            if (openImage && downloadedFile.existsAsFile())
                downloadedFile.startAsProcess();

            if (listener != nullptr)
                listener->updateFailed (error.isNotEmpty() ? error : "The update could not be installed.");

            break;

        case Notice::readyToInstall:
            if (listener != nullptr)
                listener->updateReady (offered, releaseNoteItems (loadChangelog(), currentVersion, offered.version));

            break;

        case Notice::installed:
            if (onQuit != nullptr)
                onQuit();

            break;

        case Notice::progress:
            if (listener != nullptr)
                listener->updateProgress (shownProgress);

            break;

        case Notice::none:
            break;
    }
}

} // namespace resamper
