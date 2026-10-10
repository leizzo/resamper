#include "ComponentSearch.h"
#include "FakePlugin.h"
#include "TestFixture.h"
#include "Commands/EditCommands.h"
#include "Commands/PluginCommands.h"
#include "Commands/ProjectCommands.h"
#include "Commands/TrackCommands.h"
#include "UI/Detail/DeviceCard.h"
#include "UI/MainWindow/MainComponent.h"
#include "UI/Mixer/InsertSlot.h"
#include "UI/Plugins/NativeDeviceWindow.h"

namespace te = tracktion;

namespace resamper::test
{

/** PRD §9.6 (#68): a plug-in's host-chromed window opens on insert, one per
    instance, and follows the window rules. A native device's floating
    Expanded editor follows the same rules (#70). */
struct PluginWindowTests : juce::UnitTest
{
    PluginWindowTests() : juce::UnitTest ("Plug-in Window", "Resamper") {}

    /** The app with its main window's content, one track (selected) and the fake plug-in scanned. */
    struct Windows : Fixture
    {
        Windows()
        {
            theme.load();
            invoke (cmd::trackAdd);
            invoke (cmd::trackSelect, { trackId() });
            main = std::make_unique<MainComponent> (app, commandManager);
            main->setSize (1400, 900);
        }

        ~Windows()
        {
            main.reset();
        }

        juce::String trackId (int index = 0) const   { return model.getTracks()[(size_t) index].id; }

        /** Inserts through the Command, as every view does; returns the new plug-in's id. */
        juce::String insert (const juce::String& type, PluginChain chain = PluginChain::device, int track = 0)
        {
            invoke (cmd::pluginInsert, { trackId (track), type, chain });
            auto list = plugins.getChain (trackId (track), chain);
            return list.empty() ? juce::String() : list.back().id;
        }

        PluginWindows& windows()   { return main->getPluginWindows(); }

        Toasts* toasts()   { return findTypeOrOnDesktop<Toasts> (*main); }

        /** The mixer's slot holding the insert, once the mixer shows it; nullptr if none does. */
        InsertSlot* insertSlot (const juce::String& pluginId)
        {
            InsertSlot* found = nullptr;

            std::function<void (juce::Component&)> search = [&] (juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                {
                    if (auto* slot = dynamic_cast<InsertSlot*> (child); slot != nullptr && slot->getPlugin()
                                                                         && slot->getPlugin()->id == pluginId)
                        found = slot;

                    search (*child);
                }
            };

            dispatchUntil ([&] { search (*main); return found != nullptr; });
            return found;
        }

        /** Clicks a slot away from its power LED, as the user opens an insert. */
        static void clickSlot (InsertSlot& slot)
        {
            const auto centre = slot.getLocalBounds().getCentre();
            slot.mouseDown (mouseEvent (slot, centre, centre, false));
            slot.mouseUp (mouseEvent (slot, centre, centre, false));
            settle();
        }

        /** Lets pending messages (the model's asynchronous notification, a deferred close) land. */
        static void settle()   { juce::MessageManager::getInstance()->runDispatchLoopUntil (60); }

        ScannedPlugin scanned { *this };
        juce::ApplicationCommandManager commandManager;
        std::unique_ptr<MainComponent> main;
    };

    void runTest() override
    {
        const juce::String reverb (te::ReverbPlugin::xmlTypeName), pinboard (FakePlugin::description().fileOrIdentifier);
        const juce::String dot (juce::CharPointer_UTF8 ("\xc2\xb7"));

        beginTest ("Adding a plug-in opens its window at once, with the toast; a native device opens none");
        {
            Windows f;
            const auto start = juce::Time::getMillisecondCounterHiRes();
            const auto id = f.insert (pinboard);
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;

            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expectEquals (visiblePluginWindows(), 1);
            expect (f.windows().isShowing (id));
            expect (elapsed < 300.0, "the window took " + juce::String (elapsed) + " ms");

            // The host chrome and the loading state come first; the vendor UI follows.
            auto* window = f.windows().getWindow (id);
            expect (window != nullptr && window->getStatus() == PluginWindow::Status::loading, juce::String (window != nullptr ? (int) window->getStatus() : -1));

            auto* toasts = f.toasts();
            expect (toasts != nullptr);
            const auto message = "Pinboard added to " + f.model.getTracks().front().name + " " + dot
                               + " Plug-in window opened automatically";
            expect (toasts != nullptr && toasts->getMessages().contains (message),
                    toasts != nullptr ? toasts->getMessages().joinIntoString (" | ") : juce::String());

            f.insert (reverb);
            expectEquals (visiblePluginWindows(), 1, "a native device opened a window");
        }

        beginTest ("Every insert path opens the window: device chain, mixer insert, Replace");
        {
            Windows f;
            const auto device = f.insert (pinboard);
            const auto mixer = f.insert (pinboard, PluginChain::mixer);
            expect (f.windows().isShowing (device) && f.windows().isShowing (mixer));

            const auto native = f.insert (reverb);
            f.invoke (cmd::pluginReplace, { f.trackId(), native, pinboard });
            const auto replaced = f.plugins.getChain (f.trackId(), PluginChain::device).back().id;
            expect (replaced != native && f.windows().isShowing (replaced), "Replace didn't open the window");
            expectEquals (visiblePluginWindows(), 3);
        }

        beginTest ("Auto-open off: the card appears and the window stays closed; the preference persists");
        {
            Windows f;
            const auto first = f.insert (pinboard);
            auto* toasts = f.toasts();
            const auto message = toasts != nullptr ? toasts->getMessages()[toasts->getMessages().size() - 1] : juce::String();
            expect (toasts != nullptr && toasts->runAction (message, "Auto-open window on insert"));
            expect (! f.app.preferences.getAutoOpenPluginWindows());
            expect (toasts != nullptr && toasts->getMessages().contains (message), "flipping the toggle closed the toast");

            f.windows().close (first);
            const auto second = f.insert (pinboard);
            expect (second.isNotEmpty() && ! f.windows().isOpen (second));
            expectEquals (visiblePluginWindows(), 0);
            expect (toasts != nullptr && toasts->getMessages().contains ("Pinboard added to " + f.model.getTracks().front().name));

            // It lasts: the preferences file keeps it for the next run.
            const auto file = f.scratchDir().getChildFile ("preferences.xml");
            {
                Preferences prefs;
                prefs.setFile (file);
                prefs.setAutoOpenPluginWindows (false);
            }

            Preferences reread;
            expect (reread.getAutoOpenPluginWindows(), "on by default");
            reread.setFile (file);
            expect (! reread.getAutoOpenPluginWindows());
        }

        beginTest ("The toast's Undo takes the plug-in back out and closes its window, in one undo step");
        {
            Windows f;
            auto& undo = f.projects.getEdit().getUndoManager();
            const auto history = undo.getUndoDescriptions();
            const auto id = f.insert (pinboard);
            auto* toasts = f.toasts();
            const auto message = toasts != nullptr ? toasts->getMessages()[toasts->getMessages().size() - 1] : juce::String();
            expect (dispatchUntil ([&] { return f.app.engine.getPluginHosting().getState (id).isRunning(); }));
            expect (toasts != nullptr && toasts->runAction (message, "Undo"));

            // It was the insert itself undone: the history is back where it was, the insert waiting to be redone.
            expect (! f.plugins.contains (id));
            expect (undo.getUndoDescriptions() == history && undo.canRedo(), undo.getUndoDescriptions().joinIntoString (" | "));
            expect (dispatchUntil ([] { return visiblePluginWindows() == 0; }), "the window outlived its plug-in");
        }

        beginTest ("One window per instance; opening it again brings it forward");
        {
            Windows f;
            const auto id = f.insert (pinboard);
            f.windows().open (id);
            f.windows().open (id);
            expectEquals (visiblePluginWindows(), 1);
            expectEquals (f.windows().getOpenPluginIds().size(), 1);
        }

        beginTest ("The first window centres, later ones cascade 24 px down-right");
        {
            Windows f;
            const auto a = f.insert (pinboard);
            const auto b = f.insert (pinboard);
            auto* first = f.windows().getWindow (a);
            auto* second = f.windows().getWindow (b);
            expect (first != nullptr && second != nullptr);

            if (first == nullptr || second == nullptr)
                return;

            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            expect (display != nullptr);
            const auto frame = first->getFrameScreenBounds();
            expect (display != nullptr && std::abs (frame.getCentreX() - display->userBounds.toNearestInt().getCentreX()) <= 1);
            const auto step = f.theme.getMetrics().windowCascade;
            expect (second->getFrameScreenBounds().getPosition() - frame.getPosition() == juce::Point<int> (step, step),
                    (second->getFrameScreenBounds().getPosition() - frame.getPosition()).toString());

            // A closed window frees its spot: the next one takes it rather than landing on another.
            const auto firstSpot = frame.getPosition(), secondSpot = second->getFrameScreenBounds().getPosition();
            f.windows().close (a);
            auto* third = f.windows().getWindow (f.insert (pinboard));
            expect (third != nullptr && third->getFrameScreenBounds().getPosition() == firstSpot);
            auto* fourth = f.windows().getWindow (f.insert (pinboard));
            expect (fourth != nullptr && fourth->getFrameScreenBounds().getPosition() == secondSpot + juce::Point<int> (step, step),
                    "a new window landed on an open one");
        }

        beginTest ("The window draws the host chrome; the vendor UI shows at its native size, then scaled");
        {
            Windows f;
            const auto id = f.insert (pinboard);
            auto* window = f.windows().getWindow (id);
            expect (window != nullptr);

            if (window == nullptr)
                return;

            expect (dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::ready; }));
            auto* vendor = window->getVendorComponent();
            expect (vendor != nullptr && vendor->getWidth() == FakePlugin::editorWidth && vendor->getHeight() == FakePlugin::editorHeight);

            auto& metrics = f.theme.getMetrics();
            const auto frame = window->getFrameScreenBounds();
            expectEquals (frame.getHeight(), metrics.pluginTitleBarHeight + metrics.pluginToolbarHeight
                                                 + FakePlugin::editorHeight + metrics.pluginFooterHeight);

            for (auto* part : { "pin", "close", "bypass", "preset", "slotA", "slotB", "copyAToB", "uiScale" })
                expect (findOne (*window, part) != nullptr && findOne (*window, part)->isVisible(), part);

            // Every host control is reachable by keyboard and named for a screen reader (§18).
            for (auto* part : { "pin", "close", "bypass", "preset", "slotA", "slotB", "copyAToB" })
                if (auto* c = findOne (*window, part))
                    expect (c->getWantsKeyboardFocus() && c->getTitle().isNotEmpty(), part);

            juce::String footer;

            for (auto* child : window->getChildren())
                if (child->getTitle().startsWith ("Plug-in UI"))
                    footer = child->getTitle();

            expectEquals (footer, "Plug-in UI " + dot + " rendered by Resamper Tests " + dot + " VST3 1.2.0 " + dot + " in-process");
            expect (! window->hasResizeGrip(), "a fixed-size plug-in got a resize grip");

            // Bypass goes through the Command.
            click (findOne (*window, "bypass"));
            expect (dispatchUntil ([&] { return ! f.plugins.getChain (f.trackId(), PluginChain::device).back().enabled; }),
                    "Bypass didn't bypass the plug-in");

            window->setUiScale (200);
            expectEquals (window->getFrameScreenBounds().getHeight(), metrics.pluginTitleBarHeight + metrics.pluginToolbarHeight
                                                                       + 2 * FakePlugin::editorHeight + metrics.pluginFooterHeight);
        }

        beginTest ("Parameters grows a small vendor area to the readable minimum, and off restores it, in place (#132)");
        {
            Windows f;
            const auto id = f.insert (pinboard);
            auto* window = f.windows().getWindow (id);
            expect (window != nullptr && dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::ready; }));

            if (window == nullptr || window->getStatus() != PluginWindow::Status::ready)
                return;

            auto& metrics = f.theme.getMetrics();
            const auto chrome = metrics.pluginTitleBarHeight + metrics.pluginToolbarHeight + metrics.pluginFooterHeight;
            expect (FakePlugin::editorHeight < metrics.pluginParametersMinHeight, "the vendor UI isn't small enough to test");

            const auto where = window->getFrameScreenBounds().getPosition();
            const auto saved = f.plugins.getWindowState (id);
            auto* parametersButton = findOne (*window, "parameters");
            expect (parametersButton != nullptr);

            click (parametersButton);
            expect (dispatchUntil ([&] { return window->isShowingParameters(); }), "Parameters doesn't show");
            expectEquals (window->getFrameScreenBounds().getHeight(), chrome + metrics.pluginParametersMinHeight);

            if (auto* panel = findType<juce::GenericAudioProcessorEditor> (*window))
                expectEquals (panel->getHeight(), metrics.pluginParametersMinHeight);
            else
                expect (false, "Parameters shows no parameters");

            expect (window->getFrameScreenBounds().getPosition() == where, window->getFrameScreenBounds().getPosition().toString());

            click (parametersButton);
            expect (dispatchUntil ([&] { return ! window->isShowingParameters(); }), "Parameters doesn't go off");
            expectEquals (window->getFrameScreenBounds().getHeight(), chrome + FakePlugin::editorHeight);
            expect (window->getFrameScreenBounds().getPosition() == where, window->getFrameScreenBounds().getPosition().toString());

            expect (f.plugins.getWindowState (id) == saved, "Parameters changed the saved window state");
        }

        beginTest ("Window state, preset name and A/B slot round-trip through the project");
        {
            Windows f;
            const auto id = f.insert (pinboard);
            auto* window = f.windows().getWindow (id);
            expect (window != nullptr);

            if (window == nullptr)
                return;

            const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            const auto where = display != nullptr ? display->userBounds.toNearestInt().getPosition() + juce::Point<int> (40, 60) : juce::Point<int> (40, 60);
            window->setFramePosition (where);
            window->onMoved();
            f.windows().setPinned (id, true);
            window->setUiScale (150);
            window->onUiScaleChanged (150);

            f.plugins.setPresetFolder (f.scratchDir().getChildFile ("Presets"));
            f.invoke (cmd::pluginSavePreset, { id, "Wide" });
            f.invoke (cmd::pluginSelectAB, { id, 1 });
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            f.projectSaveLocation = f.scratchDir().getChildFile ("Windows");
            f.invoke (cmd::projectSaveAs);
            f.main.reset();
            expectEquals (visiblePluginWindows(), 0);

            Windows reopened;
            reopened.projectToOpen = f.projectSaveLocation;
            reopened.invoke (cmd::projectOpen);
            expect (reopened.errors.isEmpty(), reopened.errors.joinIntoString ("; "));

            const auto info = reopened.plugins.getAllPlugins();
            dispatchUntil ([&] { return ! info.empty() && reopened.windows().isOpen (info[0].id); });
            expect (info.size() == 1 && info[0].presetName == "Wide" && info[0].abSlot == 1);

            auto* restored = info.empty() ? nullptr : reopened.windows().getWindow (info[0].id);
            expect (restored != nullptr, "the open window didn't come back");

            if (restored != nullptr)
            {
                expect (restored->getFrameScreenBounds().getPosition() == where,
                        restored->getFrameScreenBounds().getPosition().toString() + " vs " + where.toString());
                expect (restored->isPinned());
                expectEquals (restored->getUiScale(), 150);
            }
        }

        beginTest ("Unpinned windows hide while their track isn't selected; pinned ones stay; views hide nothing");
        {
            Windows f;
            f.invoke (cmd::trackAdd);
            const auto onFirst = f.insert (pinboard, PluginChain::device, 0);
            const auto onSecond = f.insert (pinboard, PluginChain::device, 1);

            // Opening a window selects its track.
            expectEquals (f.model.getSelectedTrackId(), f.trackId (1));
            expect (dispatchUntil ([&] { return ! f.windows().isShowing (onFirst) && f.windows().isShowing (onSecond); }),
                    "the deselected track's window still shows");

            f.invoke (cmd::trackSelect, { f.trackId (0) });
            expect (dispatchUntil ([&] { return f.windows().isShowing (onFirst) && ! f.windows().isShowing (onSecond); }),
                    "selecting the track didn't swap its window in");

            f.windows().setPinned (onSecond, true);
            expect (f.windows().isShowing (onSecond), "a pinned window hid");

            f.commands.invokeById ("view.mixer");
            Windows::settle();
            expect (f.windows().isShowing (onFirst) && f.windows().isShowing (onSecond), "switching views hid a window");

            // The preference off: every window shows whatever is selected.
            f.windows().setPinned (onSecond, false);
            f.invoke (cmd::pluginWindowToggleSelectedTrackOnly);
            expect (dispatchUntil ([&] { return f.windows().isShowing (onFirst) && f.windows().isShowing (onSecond); }),
                    "with the preference off a window still hid");
        }

        beginTest ("Clicking a window selects its track");
        {
            Windows f;
            f.invoke (cmd::trackAdd);
            f.app.preferences.setPluginWindowsForSelectedTrackOnly (false);
            const auto onFirst = f.insert (pinboard, PluginChain::device, 0);
            f.invoke (cmd::trackSelect, { f.trackId (1) });
            auto* window = f.windows().getWindow (onFirst);
            expect (window != nullptr);

            if (window == nullptr || window->onActivated == nullptr)
                return;

            // What any click inside the window (the vendor UI's too) reports.
            window->onActivated();
            expectEquals (f.model.getSelectedTrackId(), f.trackId (0));
        }

        beginTest ("Mod+Alt+P hides and shows every window; Esc and Mod+W close the focused one");
        {
            Windows f;
            const auto a = f.insert (pinboard);
            const auto b = f.insert (pinboard);
            auto* window = f.windows().getWindow (a);
            expect (window != nullptr);

            if (window == nullptr)
                return;

            const auto altP = juce::KeyPress ('P', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0);
            expect (window->keyPressed (altP));
            expectEquals (visiblePluginWindows(), 0);
            f.invoke (cmd::pluginWindowToggleAll);
            expectEquals (visiblePluginWindows(), 2);

            expect (window->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
            expect (dispatchUntil ([&] { return ! f.windows().isOpen (a); }), "Esc didn't close the window");
            expect (f.windows().isOpen (b));
            expect (! f.plugins.getWindowState (a).open, "a closed window was saved as open");

            if (auto* other = f.windows().getWindow (b))
                expect (other->keyPressed (juce::KeyPress ('W', juce::ModifierKeys::commandModifier, 0)));

            expect (dispatchUntil ([] { return visiblePluginWindows() == 0; }), "Mod+W didn't close the window");
        }

        beginTest ("Space in a plug-in window plays, as in the main window");
        {
            Windows f;
            f.commandManager.registerAllCommandsForTarget (f.main.get());
            f.commandManager.setFirstCommandTarget (f.main.get());
            const auto id = f.insert (pinboard);
            auto* window = f.windows().getWindow (id);
            expect (window != nullptr && dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::ready; }));

            if (window == nullptr || window->getPeer() == nullptr)
                return;

            expect (! f.model.isPlaying());
            window->getPeer()->handleKeyPress (juce::KeyPress (juce::KeyPress::spaceKey));
            expect (dispatchUntil ([&] { return f.model.isPlaying(); }), "space in the plug-in window didn't play");
        }

        beginTest ("A/B compare keeps both settings; Copy A to B; a saved preset loads");
        {
            Windows f;
            const auto id = f.insert (pinboard);
            auto* instance = firstExternalInstance (f);
            expect (instance != nullptr);

            if (instance == nullptr)
                return;

            auto* parameter = instance->getParameters()[0];
            parameter->setValueNotifyingHost (0.2f);
            f.invoke (cmd::pluginSelectAB, { id, 1 });
            expectWithinAbsoluteError (parameter->getValue(), 0.2f, 0.001f, "B starts as a copy of A");
            parameter->setValueNotifyingHost (0.9f);

            f.invoke (cmd::pluginSelectAB, { id, 0 });
            expectWithinAbsoluteError (parameter->getValue(), 0.2f, 0.001f);
            f.invoke (cmd::pluginSelectAB, { id, 1 });
            expectWithinAbsoluteError (parameter->getValue(), 0.9f, 0.001f);

            f.invoke (cmd::pluginCopyAToB, { f.trackId(), id });
            expectWithinAbsoluteError (parameter->getValue(), 0.2f, 0.001f);

            f.plugins.setPresetFolder (f.scratchDir().getChildFile ("Presets"));
            parameter->setValueNotifyingHost (0.7f);
            f.invoke (cmd::pluginSavePreset, { id, "Seventy" });
            parameter->setValueNotifyingHost (0.1f);
            const auto presets = f.plugins.getPresetNames (id);
            expect (presets.contains ("Seventy"));
            f.invoke (cmd::pluginSelectPreset, { id, presets.indexOf ("Seventy") });
            expectWithinAbsoluteError (parameter->getValue(), 0.7f, 0.001f);
            expectEquals (f.plugins.getChain (f.trackId(), PluginChain::device).back().presetName, juce::String ("Seventy"));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
        }
    }
};

static PluginWindowTests pluginWindowTests;

//==============================================================================
/** PRD §9.2.3, §10.6 (#70): native and plug-in mixer inserts look different;
    clicking one opens its device's window: a plug-in's window, or the native
    device's floating Expanded editor under the same window rules. */
struct MixerInsertWindowTests : juce::UnitTest
{
    MixerInsertWindowTests() : juce::UnitTest ("Mixer Insert Windows", "Resamper") {}

    using Windows = PluginWindowTests::Windows;

    static int visibleNativeWindows()   { return (int) visibleDesktopWindows ("NativeDeviceWindow").size(); }

    void runTest() override
    {
        const juce::String reverb (te::ReverbPlugin::xmlTypeName), pinboard (FakePlugin::description().fileOrIdentifier);

        beginTest ("A native insert is InsertSlot/Filled, a plug-in insert InsertSlot/Plugin with its format");
        {
            Windows f;
            const auto native = f.insert (reverb, PluginChain::mixer);
            const auto plugIn = f.insert (pinboard, PluginChain::mixer);
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            // MixerChannel.inserts hold either kind; effects only.
            const auto inserts = f.plugins.getChain (f.trackId(), PluginChain::mixer);
            expect (inserts.size() == 2 && ! inserts[0].external && inserts[1].external);
            f.invoke (cmd::pluginInsert, { f.trackId(), te::FourOscPlugin::xmlTypeName, PluginChain::mixer });
            expectEquals ((int) f.plugins.getChain (f.trackId(), PluginChain::mixer).size(), 2, "an instrument went in a mixer insert");

            auto* nativeSlot = f.insertSlot (native);
            auto* plugInSlot = f.insertSlot (plugIn);
            expect (nativeSlot != nullptr && plugInSlot != nullptr);

            if (nativeSlot == nullptr || plugInSlot == nullptr)
                return;

            expect (nativeSlot->getLook() == InsertSlot::Look::filled);
            expect (plugInSlot->getLook() == InsertSlot::Look::plugin);

            // Never colour alone (§18): the plug-in says what it is and its format.
            expect (plugInSlot->getTooltip().contains ("VST3 plug-in"), plugInSlot->getTooltip());
            expect (! nativeSlot->getTooltip().contains ("plug-in"), nativeSlot->getTooltip());
        }

        beginTest ("Clicking a plug-in insert opens its window, or brings it forward");
        {
            Windows f;
            const auto id = f.insert (pinboard, PluginChain::mixer);
            expect (f.windows().isShowing (id), "adding a plug-in to a slot didn't open its window");
            f.windows().close (id);
            Windows::settle();

            auto* slot = f.insertSlot (id);
            expect (slot != nullptr);

            if (slot == nullptr)
                return;

            Windows::clickSlot (*slot);
            expect (f.windows().isShowing (id) && f.windows().getWindow (id) != nullptr);
            Windows::clickSlot (*slot);
            expectEquals (visiblePluginWindows(), 1);
            expectEquals (visibleNativeWindows(), 0);
        }

        beginTest ("Clicking a native insert floats its device expanded: one per instance, no plug-in window");
        {
            Windows f;
            const auto id = f.insert (reverb, PluginChain::mixer);
            expectEquals (visibleNativeWindows(), 0, "adding a native device opened a window");

            auto* slot = f.insertSlot (id);
            expect (slot != nullptr);

            if (slot == nullptr)
                return;

            Windows::clickSlot (*slot);
            Windows::clickSlot (*slot);
            expectEquals (visibleNativeWindows(), 1);
            expectEquals (visiblePluginWindows(), 0);
            expect (f.windows().getWindow (id) == nullptr, "a native device got a plug-in window");

            auto* window = dynamic_cast<NativeDeviceWindow*> (f.windows().getDeviceWindow (id));
            expect (window != nullptr);

            if (window == nullptr)
                return;

            // Its card, expanded: every parameter, nothing to fold or expand; Pin and Close in the title bar.
            for (auto& p : f.plugins.getParameters (id))
                expect (findOne (*window, p.id) != nullptr && findOne (*window, p.id)->isVisible(), p.name + " isn't in the window");

            expect (findOne (*window, "fold") == nullptr || ! findOne (*window, "fold")->isVisible());

            for (auto* part : { "pin", "close" })
                expect (findOne (*window, part) != nullptr && findOne (*window, part)->getWantsKeyboardFocus(), part);

            // It edits the device through Commands, like the docked card.
            click (findOne (*window, "power"));
            expect (dispatchUntil ([&] { return ! f.plugins.getChain (f.trackId(), PluginChain::mixer).front().enabled; }),
                    "the window's power didn't bypass the device");
        }

        beginTest ("A native device's window follows the window rules: Pin, the selected track, a click selects its track");
        {
            Windows f;
            f.invoke (cmd::trackAdd);
            const auto onFirst = f.insert (reverb, PluginChain::mixer, 0);
            const auto onSecond = f.insert (reverb, PluginChain::mixer, 1);
            f.windows().open (onFirst);
            f.windows().open (onSecond);

            // Opening a window selects its track; the other track's unpinned window hides.
            expectEquals (f.model.getSelectedTrackId(), f.trackId (1));
            expect (dispatchUntil ([&] { return ! f.windows().isShowing (onFirst) && f.windows().isShowing (onSecond); }),
                    "the deselected track's window still shows");

            f.windows().setPinned (onSecond, true);
            expect (f.plugins.getWindowState (onSecond).pinned, "Pin wasn't saved on the device");
            f.invoke (cmd::trackSelect, { f.trackId (0) });
            expect (dispatchUntil ([&] { return f.windows().isShowing (onFirst) && f.windows().isShowing (onSecond); }),
                    "a pinned window hid, or the selected track's didn't come back");

            auto* window = f.windows().getDeviceWindow (onSecond);
            expect (window != nullptr && window->isPinned() && window->onActivated != nullptr);

            if (window == nullptr || window->onActivated == nullptr)
                return;

            window->onActivated();
            expectEquals (f.model.getSelectedTrackId(), f.trackId (1));

            // Esc closes it, saved as closed; Mod+Alt+P reaches the other windows too.
            expect (window->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
            expect (dispatchUntil ([&] { return ! f.windows().isOpen (onSecond); }), "Esc didn't close the window");
            expect (! f.plugins.getWindowState (onSecond).open);
        }

        beginTest ("Open in Window floats a chain device the same way; it follows the device and closes when it goes");
        {
            Windows f;
            const auto id = f.insert (reverb);
            Windows::settle();
            auto* card = dynamic_cast<DeviceCard*> (findOne (*f.main, "DeviceCard/Native"));
            expect (card != nullptr && card->onFloat != nullptr);

            if (card == nullptr || card->onFloat == nullptr)
                return;

            card->onFloat();
            card->onFloat();
            expectEquals (visibleNativeWindows(), 1);
            expect (f.plugins.getChain (f.trackId(), PluginChain::device).front().size == DeviceSize::compact,
                    "floating it changed the docked card's size");

            f.invoke (cmd::pluginRemove, { f.trackId(), id });
            expect (dispatchUntil ([] { return visibleNativeWindows() == 0; }), "the removed device's window stayed open");
        }
    }
};

static MixerInsertWindowTests mixerInsertWindowTests;

} // namespace resamper::test
