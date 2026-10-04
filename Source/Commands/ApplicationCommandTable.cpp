#include "ApplicationCommandTable.h"

#include <array>
#include <vector>

namespace resamper
{

namespace
{
    using MK = juce::ModifierKeys;
    constexpr int cmd = MK::commandModifier;
    constexpr int shift = MK::shiftModifier;
    constexpr int alt = MK::altModifier;
    using KP = juce::KeyPress;

    /** A submenu: its title and its Commands, in order. */
    struct CommandSubmenu
    {
        const char* name;
        std::span<const char* const> commandIds;
    };

    /** One menu: its title, its Commands, then its submenus, in order. */
    struct CommandMenu
    {
        const char* name;
        std::span<const char* const> commandIds;
        std::span<const CommandSubmenu> submenus = {};
    };

    const char* const fileMenu[]
    {
        "project.new",
        "project.open",
        "project.save",
        "project.saveAs",
        "project.autosave",
        "project.recover",
        "project.saveTemplate",
        "project.newFromTemplate",
        "file.exportMix",
    };

    const char* const editMenu[]
    {
        "edit.undo",
        "edit.redo",
        "clip.duplicate",
        "clip.split",
        "clip.consolidate",
        "edit.delete",
        "ui.escape",
        "track.remove",
        "track.freeze",
        "track.unfreeze",
        "track.bounce",
    };

    const char* const createMenu[]
    {
        "track.add",
        "track.addMidi",
        "mixer.addReturn",
        "mixer.addBus",
        "clip.add",
        "clip.addMidi",
    };

    const char* const viewMenu[]
    {
        "view.session",
        "view.arrange",
        "view.mixer",
        "view.pianoRoll",
        "view.editor",
        "view.toggleSessionArrange",
        "view.toggleBrowser",
        "view.toggleDetail",
        "arrange.zoomIn",
        "arrange.zoomOut",
        "arrange.zoomToSelection",
        "arrange.zoomToSong",
        "pluginWindow.toggleAll",
        "pluginWindow.closeFocused",
        "theme.use",
        "dev.reloadTheme",
        "dev.toggleOverlay",
    };

    const char* const optionsMenu[]
    {
        "transport.togglePlay",
        "transport.playFromSelection",
        "transport.play",
        "transport.stop",
        "transport.returnToStart",
        "transport.record",
        "transport.loopSelection",
        "transport.toggleLoop",
        "transport.toggleMetronome",
        "transport.toggleCountIn",
        "transport.tapTempo",
        "view.toggleFollow",
        "plugin.scan",
        "pluginWindow.toggleAutoOpen",
        "pluginWindow.toggleSelectedTrackOnly",
        "session.stopAll",
        "session.recordToArrangement",
    };

    const char* const languageMenu[]
    {
        "ui.language.system",
        "ui.language.en",
        "ui.language.tr",
    };

    // The title is bilingual in every UI Language, so a wrong choice can be undone (ADR-0015).
    const CommandSubmenu optionsSubmenus[]
    {
        { "Language / Dil", languageMenu },
    };

    /** The menus and their items, in order (PRD §6.1). The ApplicationCommand
        IDs are derived from this order; nothing stores them. */
    const std::array menus
    {
        CommandMenu { "File", fileMenu },
        CommandMenu { "Edit", editMenu },
        CommandMenu { "Create", createMenu },
        CommandMenu { "View", viewMenu },
        CommandMenu { "Options", optionsMenu, optionsSubmenus },
        CommandMenu { "Help", {} },
    };

    /** Above JUCE's StandardApplicationCommandIDs, so no menu item shares an ID with them. */
    constexpr juce::CommandID firstApplicationCommandID = 0x10000;

    const std::vector<ApplicationCommandEntry>& table()
    {
        static const auto entries = []
        {
            std::vector<ApplicationCommandEntry> result;

            auto add = [&result] (const char* commandId, const char* menu, const char* submenu)
            {
                result.push_back ({ firstApplicationCommandID + (juce::CommandID) result.size(), commandId, menu, submenu });
            };

            for (auto& menu : menus)
            {
                for (auto* commandId : menu.commandIds)
                    add (commandId, menu.name, nullptr);

                for (auto& submenu : menu.submenus)
                    for (auto* commandId : submenu.commandIds)
                        add (commandId, menu.name, submenu.name);
            }

            return result;
        }();

        return entries;
    }

    constexpr int timeline = arrangeView | sessionView;

    /** PRD §17, plus the app's own (file, developer). Contextual keys come after the global ones. */
    const std::array bindings
    {
        // File
        KeyBinding { "project.new",               'N',                 cmd,               anyView },
        KeyBinding { "project.open",              'O',                 cmd,               anyView },
        KeyBinding { "project.save",              'S',                 cmd,               anyView },
        KeyBinding { "project.saveAs",            'S',                 cmd | shift,       anyView },
        KeyBinding { "project.autosave",          'S',                 cmd | alt,         anyView },
        KeyBinding { "file.exportMix",            'E',                 cmd | shift,       anyView },
        KeyBinding { "clip.add",                  'I',                 cmd,               anyView },
        KeyBinding { "clip.addMidi",              'I',                 cmd | shift,       anyView },

        // Transport
        KeyBinding { "transport.togglePlay",      KP::spaceKey,        0,                 anyView },
        KeyBinding { "transport.playFromSelection", KP::spaceKey,      shift,             anyView },
        KeyBinding { "transport.record",          KP::F9Key,           0,                 anyView },
        KeyBinding { "transport.loopSelection",   'L',                 cmd,               anyView },
        KeyBinding { "transport.toggleMetronome", 'C',                 0,                 anyView },
        KeyBinding { "transport.tapTempo",        'T',                 0,                 anyView },
        KeyBinding { "transport.returnToStart",   KP::homeKey,         0,                 anyView },

        // Views
        KeyBinding { "view.toggleSessionArrange", KP::tabKey,          0,                 anyView },
        KeyBinding { "view.mixer",                'M',                 cmd | alt,         anyView },
        KeyBinding { "view.toggleDetail",         'L',                 cmd | alt,         anyView },
        KeyBinding { "view.toggleBrowser",        'B',                 cmd | alt,         anyView },

        // Edit
        KeyBinding { "edit.undo",                 'Z',                 cmd,               anyView },
        KeyBinding { "edit.redo",                 'Z',                 cmd | shift,       anyView },
        KeyBinding { "clip.duplicate",            'D',                 cmd,               anyView },
        KeyBinding { "clip.split",                'E',                 cmd,               anyView },
        KeyBinding { "clip.consolidate",          'J',                 cmd,               anyView },
        KeyBinding { "edit.delete",               KP::deleteKey,       0,                 anyView },
        KeyBinding { "edit.delete",               KP::backspaceKey,    0,                 anyView },
        KeyBinding { "ui.escape",                 KP::escapeKey,       0,                 anyView },
        KeyBinding { "track.remove",              KP::backspaceKey,    cmd,               anyView },
        KeyBinding { "track.freeze",              'F',                 cmd | shift,       anyView },
        KeyBinding { "track.unfreeze",            'F',                 cmd | alt,         anyView },
        KeyBinding { "track.bounce",              'B',                 cmd,               anyView },

        // Tracks
        KeyBinding { "track.add",                 'T',                 cmd,               anyView },
        KeyBinding { "track.addMidi",             'T',                 cmd | shift,       anyView },
        KeyBinding { "mixer.addReturn",           'T',                 cmd | alt,         anyView },
        KeyBinding { "track.toggleMuteAt",        KP::F1Key,           0,                 anyView, 0 },
        KeyBinding { "track.toggleMuteAt",        KP::F2Key,           0,                 anyView, 1 },
        KeyBinding { "track.toggleMuteAt",        KP::F3Key,           0,                 anyView, 2 },
        KeyBinding { "track.toggleMuteAt",        KP::F4Key,           0,                 anyView, 3 },
        KeyBinding { "track.toggleMuteAt",        KP::F5Key,           0,                 anyView, 4 },
        KeyBinding { "track.toggleMuteAt",        KP::F6Key,           0,                 anyView, 5 },
        KeyBinding { "track.toggleMuteAt",        KP::F7Key,           0,                 anyView, 6 },
        KeyBinding { "track.toggleMuteAt",        KP::F8Key,           0,                 anyView, 7 },

        // Other app shortcuts
        KeyBinding { "plugin.scan",               'P',                 cmd | shift,       anyView },
        KeyBinding { "pluginWindow.toggleAll",    'P',                 cmd | alt,         anyView },
        KeyBinding { "pluginWindow.closeFocused", 'W',                 cmd,               anyView },
        KeyBinding { "dev.reloadTheme",           'T',                 cmd | alt | shift, anyView },
        KeyBinding { "dev.toggleOverlay",         'D',                 cmd | alt | shift, anyView },

        // Only in the view in front
        KeyBinding { "track.toggleSoloSelected",  'S',                 0,                 timeline | mixerView },
        KeyBinding { "arrange.zoomIn",            '=',                 0,                 arrangeView },
        KeyBinding { "arrange.zoomIn",            '+',                 0,                 arrangeView },
        KeyBinding { "arrange.zoomIn",            '+',                 shift,             arrangeView },   // + typed as Shift+=
        KeyBinding { "arrange.zoomOut",           '-',                 0,                 arrangeView },
        KeyBinding { "arrange.zoomToSelection",   'Z',                 0,                 arrangeView },
        KeyBinding { "arrange.zoomToSong",        'Z',                 shift,             arrangeView },
        KeyBinding { "pianoRoll.quantize",        'Q',                 0,                 pianoRollView },
        KeyBinding { "pianoRoll.transpose",       KP::upKey,           0,                 pianoRollView, 1 },
        KeyBinding { "pianoRoll.transpose",       KP::downKey,         0,                 pianoRollView, -1 },
        KeyBinding { "pianoRoll.transpose",       KP::upKey,           shift,             pianoRollView, 12 },
        KeyBinding { "pianoRoll.transpose",       KP::downKey,         shift,             pianoRollView, -12 },
        KeyBinding { "pianoRoll.selectAll",       'A',                 cmd,               pianoRollView },
    };

    /** §17 rows waiting on their feature tickets. */
    const std::array pending
    {
        PendingShortcut { "Mod+G / Mod+Shift+G", "Group / ungroup (folder or rack)", "Folders (M2) and racks (M5)" },
        PendingShortcut { "A",                   "Automation mode",                  "Automation (M3)" },
        PendingShortcut { "B",                   "Pencil / draw",                    "Automation (M3) and editors (M4)" },
        PendingShortcut { "Mod+Shift+R",         "Re-enable automation",             "Automation recording (M3)" },
        PendingShortcut { "Mod+D in the Piano Roll", "Duplicate notes",              "Piano roll (M4)" },
        PendingShortcut { "+ / - in the Piano Roll", "Zoom",                         "Piano roll (M4)" },
    };

    bool sameKey (const KeyBinding& b, const juce::KeyPress& key)
    {
        return juce::KeyPress (b.keyCode, juce::ModifierKeys (b.modifiers), 0) == key;
    }
}

std::span<const ApplicationCommandEntry> getApplicationCommandTable()   { return table(); }
std::span<const KeyBinding> getKeyBindings()                            { return bindings; }
std::span<const PendingShortcut> getPendingShortcuts()                  { return pending; }

std::span<const char* const> getMenuNames()
{
    static const auto names = []
    {
        std::array<const char*, menus.size()> result {};

        for (size_t i = 0; i < menus.size(); ++i)
            result[i] = menus[i].name;

        return result;
    }();

    return names;
}

juce::PopupMenu createCommandMenu (juce::ApplicationCommandManager& manager, const juce::String& menuName)
{
    juce::PopupMenu menu;
    auto registered = [&manager] (const ApplicationCommandEntry& entry)
    {
        return manager.getCommandForID (entry.applicationCommandID) != nullptr;
    };

    for (auto& entry : table())
        if (menuName == entry.menu && entry.submenu == nullptr && registered (entry))
            menu.addCommandItem (&manager, entry.applicationCommandID);

    for (auto& m : menus)
    {
        if (menuName != m.name)
            continue;

        for (auto& submenu : m.submenus)
        {
            juce::PopupMenu items;

            for (auto& entry : table())
                if (entry.submenu == submenu.name && registered (entry))
                    items.addCommandItem (&manager, entry.applicationCommandID);

            if (items.getNumItems() > 0)
                menu.addSubMenu (submenu.name, items);
        }
    }

    if (menuName == "Help")
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            menu.addItem (app->getApplicationName() + " " + app->getApplicationVersion(), false, false, nullptr);

    return menu;
}

const ApplicationCommandEntry* findApplicationCommand (juce::CommandID id)
{
    for (auto& e : table())
        if (e.applicationCommandID == id)
            return &e;

    return nullptr;
}

juce::KeyPress findShortcut (const juce::String& commandId)
{
    for (auto& b : bindings)
        if (commandId == b.commandId)
            return juce::KeyPress (b.keyCode, juce::ModifierKeys (b.modifiers), 0);

    return {};
}

const KeyBinding* findBinding (const juce::KeyPress& key, int context)
{
    const KeyBinding* global = nullptr;

    for (auto& b : bindings)
    {
        if (! sameKey (b, key))
            continue;

        if (b.contexts == anyView)
            global = &b;
        else if ((b.contexts & context) != 0)
            return &b;
    }

    return global;
}

std::any bindingArgs (const KeyBinding& b)
{
    if (b.argument == KeyBinding::noArgument)
        return {};

    return b.argument;
}

} // namespace resamper
