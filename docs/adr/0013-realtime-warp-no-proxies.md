# Warp in real time; no proxy files

Tracktion plays a warped Audio Clip from a proxy file rendered for the clip's exact length, offset, Clip Loop and tempo, so every trim, split, loop or tempo edit re-rendered the whole proxy (about 7 s for a 30 s loop in Debug) and stopped the transport when it landed (#111). We open every Edit with proxies disabled, so warped clips are stretched in real time (Signalsmith) and an edit is heard at once. Because the same switch also turns off Tracktion's decode cache for compressed files, an imported FLAC, OGG or MP3 is decoded once, at import, to a 32-bit float WAV in the Project's `Audio/` folder, and the clip plays that WAV.

## Considered Options

- **Keep proxies and accept the renders** — leaves the stall and the stopped transport in place.
- **One proxy per source file and tempo, with the clip playing its region from it** — Tracktion has no such model; it would need our own renderer and playback node.

## Consequences

- Each playing warped clip costs CPU on the audio thread. The bar: 16 warped clips play without dropouts in a Release build, with Tracktion's read-ahead mode off unless that bar is missed.
- WAV and AIFF imports are still referenced in place; only compressed imports are copied into the Project. Projects saved by v0.1.x that reference compressed files directly are not migrated and read them in real time.
