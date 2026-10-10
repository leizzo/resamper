#include "ComponentSearch.h"
#include "TestFixture.h"
#include "App/UILanguage.h"
#include "Commands/ApplicationCommandTable.h"
#include "UI/Localisation.h"
#include "UI/MainWindow/MainComponent.h"
#include "UI/Theme/ThemeManager.h"

#include <regex>

namespace resamper::test
{

namespace
{
    /** Installs a mapping for the scope, and English again after it. */
    struct ScopedMapping
    {
        explicit ScopedMapping (const juce::String& text)
        {
            juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (text, false));
        }

        ~ScopedMapping()   { juce::LocalisedStrings::setCurrentMappings (nullptr); }
    };

    /** The source with its comments blanked out, string literals kept. */
    std::string withoutComments (const std::string& source)
    {
        std::string out;
        out.reserve (source.size());

        for (size_t i = 0; i < source.size(); ++i)
        {
            const auto c = source[i];
            const auto next = i + 1 < source.size() ? source[i + 1] : '\0';

            if (c == '"' || c == '\'')
            {
                out += c;

                for (++i; i < source.size() && source[i] != c; ++i)
                {
                    if (source[i] == '\\' && i + 1 < source.size())
                        out += source[i++];

                    out += source[i];
                }

                if (i < source.size())
                    out += c;
            }
            else if (c == '/' && next == '/')
            {
                while (i < source.size() && source[i] != '\n')
                    ++i;

                out += '\n';
            }
            else if (c == '/' && next == '*')
            {
                for (i += 2; i + 1 < source.size() && ! (source[i] == '*' && source[i + 1] == '/'); ++i)
                    if (source[i] == '\n')
                        out += '\n';

                ++i;
            }
            else
            {
                out += c;
            }
        }

        return out;
    }

    /** A C++ literal's text as LocalisedStrings reads the same escapes in tr.txt. */
    juce::String unescape (const std::string& literal)
    {
        return juce::String (juce::CharPointer_UTF8 (literal.c_str()))
                   .replace ("\\\"", "\"").replace ("\\'", "'")
                   .replace ("\\t", "\t").replace ("\\n", "\n").replace ("\\\\", "\\");
    }

    /** Every key the source translates: TRANS ("..."), tr ("...", ...), both of trPlural's,
        and NEEDS_TRANS ("..."), which marks a key translated later through a variable. */
    juce::StringArray translationKeys (const juce::String& source)
    {
        static const std::string literal = R"#("((?:[^"\\]|\\.)*)")#";
        static const std::regex single (R"#(\b(?:TRANS|NEEDS_TRANS|tr)\s*\(\s*)#" + literal + R"#(\s*[,)])#");
        static const std::regex plural (R"#(\btrPlural\s*\([^,;]+,\s*)#" + literal + R"#(\s*,\s*)#" + literal);

        const auto text = withoutComments (source.toStdString());
        juce::StringArray keys;

        for (std::sregex_iterator it (text.begin(), text.end(), single), end; it != end; ++it)
            keys.addIfNotAlreadyThere (unescape ((*it)[1].str()));

        for (std::sregex_iterator it (text.begin(), text.end(), plural), end; it != end; ++it)
        {
            keys.addIfNotAlreadyThere (unescape ((*it)[1].str()));
            keys.addIfNotAlreadyThere (unescape ((*it)[2].str()));
        }

        return keys;
    }

    /** The keys the source translates that the language file has no entry for. */
    juce::StringArray missingTranslations (const juce::String& source, const juce::String& languageFile)
    {
        const juce::LocalisedStrings strings (languageFile, false);
        juce::StringArray missing;

        for (auto& key : translationKeys (source))
            if (! strings.getMappings().containsKey (key))
                missing.add (key);

        return missing;
    }

    const juce::String emptyTurkish = "language: Turkish\ncountries: tr\n";
}

/** The UI Language (ADR-0015): the mapping installed at launch, the Language
    menu, the template helper, and every translated key present in tr.txt. */
struct LocalisationTests : juce::UnitTest
{
    LocalisationTests() : juce::UnitTest ("UI Language", "Resamper") {}

    void runTest() override
    {
        beginTest ("tr fills placeholders in the translated order; an untranslated key shows its English");
        {
            expectEquals (tr ("Track %1 of %2", 3, 8), juce::String ("Track 3 of 8"));

            ScopedMapping mapping (emptyTurkish + "\"Move %1 to %2\" = \"%2 hedefine %1 taşı\"\n");
            expectEquals (tr ("Move %1 to %2", "Clip", "Track"), juce::String::fromUTF8 ("Track hedefine Clip taşı"));
            expectEquals (tr ("Not in the file %1", 5), juce::String ("Not in the file 5"));
            expectEquals (TRANS ("Not in the file either"), juce::String ("Not in the file either"));
        }

        beginTest ("tr fills each placeholder once: a value that holds %2 stays as it is");
        {
            expectEquals (tr ("%1 and %2", "%2", "b"), juce::String ("%2 and b"));
            expectEquals (tr ("%1 of %3", 1), juce::String ("1 of %3"));
        }

        beginTest ("trPlural picks the singular key for one and the plural key otherwise, with n as %1");
        {
            ScopedMapping mapping (emptyTurkish + "\"%1 track\" = \"%1 Track (tekil)\"\n\"%1 tracks\" = \"%1 Track\"\n");
            expectEquals (trPlural (1, "%1 track", "%1 tracks"), juce::String ("1 Track (tekil)"));
            expectEquals (trPlural (0, "%1 track", "%1 tracks"), juce::String ("0 Track"));
            expectEquals (trPlural (4, "%1 track", "%1 tracks"), juce::String ("4 Track"));
            expectEquals (trPlural (2, "%1 clips on %2", "%1 clips on %2", "Bass"), juce::String ("2 clips on Bass"));
        }

        beginTest ("An uppercase style follows the UI Language's rules: Turkish i is İ and ı is I");
        {
            const TypeStyle caps { 8.0f, false, 600, true };
            expectEquals (caps.apply ("Mixer inserts"), juce::String ("MIXER INSERTS"));

            ScopedMapping mapping (emptyTurkish);
            expectEquals (caps.apply (juce::String::fromUTF8 ("Mixer insert'leri ılık")),
                          juce::String::fromUTF8 ("MİXER İNSERT'LERİ ILIK"));
            expectEquals (TypeStyle {}.apply ("insert"), juce::String ("insert"));
        }

        beginTest ("The language preference defaults to system and round-trips through the file");
        {
            Fixture f;
            const auto file = f.scratchDir().getChildFile ("preferences.xml");
            {
                Preferences prefs;
                expectEquals (prefs.getLanguage(), juce::String (Preferences::systemLanguage));
                prefs.setFile (file);
                prefs.setLanguage ("tr");
                expectEquals (prefs.getLanguage(), juce::String ("tr"));
            }

            Preferences reread;
            reread.setFile (file);
            expectEquals (reread.getLanguage(), juce::String ("tr"));
        }

        beginTest ("system resolves through the OS language; a chosen code is used as it is");
        {
            expectEquals (resolveUILanguage ("system", "tr"), juce::String ("tr"));
            expectEquals (resolveUILanguage ("system", "tr-TR"), juce::String ("tr"));
            expectEquals (resolveUILanguage ("system", "tr_TR"), juce::String ("tr"));
            expectEquals (resolveUILanguage ("system", "en"), juce::String ("en"));
            expectEquals (resolveUILanguage ("system", {}), juce::String ("en"));
            expectEquals (resolveUILanguage ("en", "tr"), juce::String ("en"));
            expectEquals (resolveUILanguage ("tr", "en"), juce::String ("tr"));
            expectEquals (resolveUILanguage ({}, "tr"), juce::String ("tr"));
        }

        beginTest ("A Turkish OS installs the Turkish mapping; English or a language without a file installs none");
        {
            UIFileSource files;
            expect (installUILanguage (files, resolveUILanguage ("system", "tr-TR")));
            auto* mappings = juce::LocalisedStrings::getCurrentMappings();
            expect (mappings != nullptr && mappings->getLanguageName() == "Turkish");
            expect (mappings != nullptr && mappings->getCountryCodes().contains ("tr"));
            expectEquals (TRANS ("Later"), juce::String ("Sonra"));
            expectEquals (getInstalledUILanguage(), juce::String ("tr"));

            expect (! installUILanguage (files, "en"));
            expect (juce::LocalisedStrings::getCurrentMappings() == nullptr);
            expectEquals (getInstalledUILanguage(), juce::String ("en"));

            expect (! installUILanguage (files, "de"));
            expect (juce::LocalisedStrings::getCurrentMappings() == nullptr);
            expectEquals (getInstalledUILanguage(), juce::String ("en"));
        }

        beginTest ("The Fixture runs in English whatever was installed before it");
        {
            UIFileSource files;
            installUILanguage (files, "tr");
            Fixture f;
            expect (juce::LocalisedStrings::getCurrentMappings() == nullptr);
            expectEquals (TRANS ("Later"), juce::String ("Later"));
        }

        beginTest ("The Language / Dil submenu lists System, English and Turkish named in itself, the current one ticked");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            juce::ApplicationCommandManager commandManager;
            MainComponent main (f.app, commandManager);
            commandManager.registerAllCommandsForTarget (&main);

            juce::StringArray inSubmenu;

            for (auto& entry : getApplicationCommandTable())
                if (entry.submenu != nullptr && juce::String (entry.submenu) == "Language / Dil")
                    inSubmenu.add (entry.commandId);

            expectEquals (inSubmenu.joinIntoString (" "), juce::String ("ui.language.system ui.language.en ui.language.tr"));

            expectEquals (f.commands.find ("ui.language.system")->getName(), juce::String ("System"));
            expectEquals (f.commands.find ("ui.language.en")->getName(), juce::String ("English"));
            expectEquals (f.commands.find ("ui.language.tr")->getName(), juce::String::fromUTF8 ("Türkçe"));

            expect (f.commands.find ("ui.language.system")->isTicked());
            expect (! f.commands.find ("ui.language.tr")->isTicked());

            auto menu = createCommandMenu (commandManager, "Options");
            bool found = false;

            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().text == "Language / Dil" && it.getItem().subMenu != nullptr)
                    found = it.getItem().subMenu->getNumItems() == 3;

            expect (found, "the Options menu has no Language / Dil submenu of three items");
        }

        beginTest ("Choosing another language saves it and offers Relaunch now or Later");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            juce::ApplicationCommandManager commandManager;
            MainComponent main (f.app, commandManager);
            main.setSize (1600, 1000);

            int relaunches = 0;
            main.onRelaunch = [&relaunches] { ++relaunches; };

            expect (f.invoke (cmd::uiLanguageTurkish));
            expectEquals (f.app.preferences.getLanguage(), juce::String ("tr"));
            expect (f.commands.find ("ui.language.tr")->isTicked());
            expect (! f.commands.find ("ui.language.system")->isTicked());

            auto* toasts = findTypeOrOnDesktop<Toasts> (main);
            expect (toasts != nullptr);
            const auto messages = toasts != nullptr ? toasts->getMessages() : juce::StringArray();
            expectEquals (messages.size(), 1);
            const auto message = messages[0];

            expect (toasts != nullptr && toasts->runAction (message, "Later"));
            expectEquals (relaunches, 0);
            expect (toasts != nullptr && ! toasts->getMessages().contains (message));

            // Back to the language running now: nothing to relaunch for.
            expect (f.invoke (cmd::uiLanguageEnglish));
            expectEquals (f.app.preferences.getLanguage(), juce::String ("en"));
            expect (toasts != nullptr && toasts->getMessages().isEmpty());

            // Back before answering: the offer goes, as there is nothing left to relaunch for.
            expect (f.invoke (cmd::uiLanguageTurkish));
            expect (toasts != nullptr && toasts->getMessages().contains (message));
            expect (f.invoke (cmd::uiLanguageEnglish));
            expect (toasts != nullptr && toasts->getMessages().isEmpty());

            // Choosing again replaces the offer rather than stacking a second one.
            expect (f.invoke (cmd::uiLanguageTurkish));
            expect (f.invoke (cmd::uiLanguageSystem));
            expect (toasts != nullptr && toasts->getMessages().size() <= 1);

            expect (f.invoke (cmd::uiLanguageTurkish));
            expect (toasts != nullptr && toasts->runAction (message, "Relaunch now"));
            expectEquals (relaunches, 1);
        }

        beginTest ("The missing-translation check passes with an empty file and names each key without an entry");
        {
            expect (missingTranslations ({}, emptyTurkish).isEmpty());
            expect (missingTranslations ("label.setText (name, juce::dontSendNotification);", emptyTurkish).isEmpty());

            const juce::String source = "button.setButtonText (TRANS (\"Save\"));\n"
                                        "// TRANS (\"In a comment\")\n"
                                        "label.setText (tr (\"Track %1\", n), juce::dontSendNotification);\n"
                                        "auto s = trPlural (count, \"%1 clip\", \"%1 clips\");\n"
                                        "auto q = TRANS (\"Say \\\"hi\\\"\");\n"
                                        "const char* names[] = { NEEDS_TRANS (\"Drums\") };\n";

            expectEquals (missingTranslations (source, emptyTurkish).joinIntoString (" | "),
                          juce::String ("Save | Track %1 | Say \"hi\" | Drums | %1 clip | %1 clips"));

            const auto file = emptyTurkish + "\"Save\" = \"Kaydet\"\n\"Track %1\" = \"Track %1\"\n\"%1 clip\" = \"%1 Clip\"\n"
                                             "\"%1 clips\" = \"%1 Clip\"\n\"Say \\\"hi\\\"\" = \"Selam\"\n\"Drums\" = \"Davul\"\n";
            expect (missingTranslations (source, file).isEmpty(), missingTranslations (source, file).joinIntoString (" | "));
        }

        beginTest ("Every TRANS / tr key under Source/UI and Source/App has an entry in tr.txt");
        {
            const juce::File root (RESAMPER_SOURCE_DIR);
            const auto languageFile = root.getChildFile ("UI/translations/tr.txt").loadFileAsString();
            expect (languageFile.contains ("language: Turkish"));

            juce::StringArray missing;

            for (auto* area : { "Source/UI", "Source/App" })
                for (auto& file : root.getChildFile (area).findChildFiles (juce::File::findFiles, true, "*.cpp;*.h;*.mm"))
                    for (auto& key : missingTranslations (file.loadFileAsString(), languageFile))
                        missing.add (file.getRelativePathFrom (root) + ": \"" + key + "\"");

            expect (missing.isEmpty(), "no entry in UI/translations/tr.txt for:\n" + missing.joinIntoString ("\n"));
        }
    }
};

static LocalisationTests localisationTests;

} // namespace resamper::test
