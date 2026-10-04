#include "App/AppUpdate.h"
#include "UI/State/Preferences.h"

namespace resamper::test
{

struct AppUpdateTests : juce::UnitTest
{
    AppUpdateTests() : juce::UnitTest ("App Update", "Resamper") {}

    void runTest() override
    {
        beginTest ("versions compare as numbers");
        {
            expectEquals (compareVersions ("v0.2.2", "0.2.2"), 0);
            expectEquals (compareVersions ("0.2", "0.2.0"), 0);
            expect (compareVersions ("0.2.10", "0.2.2") > 0);
            expect (compareVersions ("0.10.0", "0.9.9") > 0);
            expect (compareVersions ("1.0.0", "0.9.9") > 0);
            expect (compareVersions ("0.2.2", "0.2.3") < 0);
        }

        beginTest ("the newest official pre-release wins, drafts and foreign assets do not");
        {
            const auto json = juce::JSON::parse (R"json(
            [
              {
                "tag_name": "v0.8.0",
                "draft": false,
                "assets": [{
                  "name": "Resamper-0.8.0-macOS.dmg",
                  "browser_download_url": "http://github.com/leizzo/resamper/releases/download/v0.8.0/Resamper-0.8.0-macOS.dmg"
                }]
              },
              {
                "tag_name": "v0.3.0",
                "draft": false,
                "assets": [{
                  "name": "Resamper-0.3.0-macOS.dmg",
                  "browser_download_url": "https://github.com/other/resamper/releases/download/v0.3.0/Resamper-0.3.0-macOS.dmg"
                }]
              },
              {
                "tag_name": "v0.9.0",
                "draft": true,
                "assets": [{
                  "name": "Resamper-0.9.0-macOS.dmg",
                  "browser_download_url": "https://github.com/leizzo/resamper/releases/download/v0.9.0/Resamper-0.9.0-macOS.dmg"
                }]
              },
              {
                "tag_name": "v0.2.4",
                "draft": false,
                "assets": [{
                  "name": "Resamper-0.2.4-macOS.dmg",
                  "browser_download_url": "https://github.com/leizzo/resamper/releases/download/v0.2.4/Resamper-0.2.4-macOS.dmg"
                }]
              },
              {
                "tag_name": "v0.2.10",
                "draft": false,
                "prerelease": true,
                "assets": [{
                  "name": "notes.txt",
                  "browser_download_url": "https://github.com/leizzo/resamper/releases/download/v0.2.10/notes.txt"
                }, {
                  "name": "Resamper-0.2.10-macOS.dmg",
                  "browser_download_url": "https://github.com/leizzo/resamper/releases/download/v0.2.10/Resamper-0.2.10-macOS.dmg"
                }]
              },
              {
                "tag_name": "v0.2.2",
                "draft": false,
                "assets": [{
                  "name": "Resamper-0.2.2-macOS.dmg",
                  "browser_download_url": "https://evil.example/Resamper-0.2.2-macOS.dmg"
                }]
              }
            ]
            )json");

            const auto newer = newerRelease (json, "0.2.2");
            expect (newer.has_value());

            if (newer.has_value())
            {
                expectEquals (newer->version, juce::String ("0.2.10"));
                expectEquals (newer->tag, juce::String ("v0.2.10"));
                expectEquals (newer->assetName, juce::String ("Resamper-0.2.10-macOS.dmg"));
                expect (newer->downloadUrl.getDomain() == "github.com");
            }

            const auto abovePatch = newerRelease (json, "0.2.4");
            expect (abovePatch.has_value());

            if (abovePatch.has_value())
                expectEquals (abovePatch->version, juce::String ("0.2.10"));

            expect (! newerRelease (json, "0.2.10").has_value());
            expect (! newerRelease (json, "1.0.0").has_value());
            expect (! newerRelease (juce::var(), "0.2.2").has_value());
            expect (! newerRelease (juce::JSON::parse ("{}"), "0.2.2").has_value());
            expect (! newerRelease (juce::JSON::parse ("[]"), "0.2.2").has_value());
        }

        beginTest ("only an installed app bundle outside a build tree can be replaced");
        {
            struct TempDir
            {
                juce::File dir { juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("ResamperUpdateTest-" + juce::Uuid().toString()) };

                TempDir() { dir.createDirectory(); }
                ~TempDir() { dir.deleteRecursively(); }

                juce::File bundle (const juce::String& relative, bool withPlist) const
                {
                    auto app = dir.getChildFile (relative);
                    app.getChildFile ("Contents").createDirectory();

                    if (withPlist)
                        app.getChildFile ("Contents/Info.plist").replaceWithText ("ok");

                    return app;
                }
            };

            TempDir temp;
            expect (canReplaceInstalledApp (temp.bundle ("installed/Resamper.app", true)));
            expect (! canReplaceInstalledApp (temp.bundle ("noplist/Resamper.app", false)));
            expect (! canReplaceInstalledApp (temp.bundle ("Other.app", true)));
            expect (! canReplaceInstalledApp (temp.bundle ("build/Resamper.app", true)));
            expect (! canReplaceInstalledApp (temp.bundle ("cmake-build-debug/Resamper.app", true)));
            expect (! canReplaceInstalledApp (temp.bundle ("Resamper_artefacts/Resamper.app", true)));

            auto file = temp.dir.getChildFile ("not-a-bundle/Resamper.app");
            file.getParentDirectory().createDirectory();
            file.replaceWithText ("not a bundle");
            expect (! canReplaceInstalledApp (file));
        }

        beginTest ("Later remembers the skipped release");
        {
            Preferences preferences;
            expectEquals (preferences.getSkippedUpdateVersion(), juce::String());
            preferences.setSkippedUpdateVersion ("0.2.4");
            expectEquals (preferences.getSkippedUpdateVersion(), juce::String ("0.2.4"));
            expectEquals (preferences.getLastLaunchedVersion(), juce::String());
            preferences.setLastLaunchedVersion ("0.2.2");
            expectEquals (preferences.getLastLaunchedVersion(), juce::String ("0.2.2"));
            expect (! preferences.hadSavedPreferences());

            auto saved = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("ResamperPrefs-" + juce::Uuid().toString());
            saved.replaceWithText ("<Preferences/>");
            Preferences returning;
            returning.setFile (saved);
            expect (returning.hadSavedPreferences());
            saved.deleteFile();
        }

        beginTest ("release notes cover versions opened since last time");
        {
            const auto changelog = juce::String (R"md(## [Unreleased]

### Added

- Not shipped yet.

## [0.2.2] - 2026-10-04

Intro line that wraps
  onto the next line.

### Changed

- Mixer automation. Send gain follows the fader.
- Layouts are **components**.

### Fixed

- Menus stay on top.

## [0.2.1] - 2026-10-02

### Fixed

- Slow plug-in timing.

[Unreleased]: https://example.com
[0.2.2]: https://example.com/022
)md");

            const auto one = releaseNotesSince (changelog, "0.2.1", "0.2.2");
            expect (one.contains ("0.2.2 — 2026-10-04"));
            expect (one.contains ("Intro line that wraps onto the next line."));
            expect (one.contains ("Changed"));
            expect (one.contains ("• Layouts are components."));
            expect (one.contains ("• Menus stay on top."));
            expect (! one.contains ("Slow plug-in"));
            expect (! one.contains ("Not shipped"));
            expect (! one.contains ("https://"));

            const auto two = releaseNotesSince (changelog, "0.2.0", "0.2.2");
            expect (two.contains ("Layouts are components."));
            expect (two.contains ("Slow plug-in timing."));
            expect (two.indexOf ("0.2.2") < two.indexOf ("0.2.1"));

            const auto currentOnly = releaseNotesSince (changelog, {}, "0.2.2");
            expect (currentOnly.contains ("Layouts are components."));
            expect (! currentOnly.contains ("Slow plug-in"));

            expect (releaseNotesSince (changelog, "0.2.2", "0.2.2").isEmpty());
            expect (releaseNotesSince (changelog, "0.2.1", {}).isEmpty());

            const auto items = releaseNoteItems (changelog, "0.2.1", "0.2.2");
            expectEquals ((int) items.size(), 3);
            expectEquals (items[0].title, juce::String ("Mixer automation"));
            expectEquals (items[0].description, juce::String ("Send gain follows the fader."));
            expectEquals (items[1].title, juce::String ("Layouts are components."));
            expect (items[1].description.isEmpty());
            expectEquals (items[2].title, juce::String ("Menus stay on top."));
            expect (releaseNoteItems (changelog, "0.2.0", "0.2.2").size() > items.size());
            expectEquals (releaseSummary (changelog, "0.2.2"),
                          juce::String ("Intro line that wraps onto the next line."));
            expect (releaseSummary (changelog, "0.2.1").isEmpty());
        }

        beginTest ("a first install records the version and shows no notes");
        {
            Preferences preferences;
            expect (notesForLaunch (preferences, "0.2.2").empty());
            expectEquals (preferences.getLastLaunchedVersion(), juce::String ("0.2.2"));

            auto saved = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("ResamperPrefs-" + juce::Uuid().toString());
            saved.replaceWithText ("<Preferences/>");
            Preferences returning;
            returning.setFile (saved);
            const auto items = notesForLaunch (returning, "0.2.2");
            expect (! items.empty());
            expectEquals (returning.getLastLaunchedVersion(), juce::String());
            saved.deleteFile();
        }
    }
};

static AppUpdateTests appUpdateTests;

} // namespace resamper::test
