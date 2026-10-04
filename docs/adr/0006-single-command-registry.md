# One Command registry behind JSON, menus, and shortcuts

ADR-0003 made Commands the only mutation path from UI; this records how that's enforced across JUCE's three invocation styles. A single registry keys Commands by string ID (`transport.play`, `edit.undo`). JSON buttons resolve it directly; JUCE menus/shortcuts use one static table mapping `ApplicationCommandID` integers onto the same string IDs. No menu-only or shortcut-only actions that bypass the registry.

**Considered options:** Letting JUCE menus/shortcuts call the Application Model directly for "trivial" actions — rejected because it erodes the choke point one shortcut at a time.

**Consequences:** Adding a user action = registering one Command with one string ID, plus optionally one row in the shortcut table. If an action isn't in the registry, no UI surface can reach it.

**Update (2026-10-03):** The JSON layout system was removed; C++ buttons resolve the registry by string ID the way JSON buttons did. Menus and shortcuts are unchanged.
