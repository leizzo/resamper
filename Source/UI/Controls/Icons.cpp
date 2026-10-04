#include "Icons.h"

#include "UI/Theme/ThemeManager.h"

#include <array>

namespace resamper
{

namespace
{
    /*  The icons are lucide's own SVG elements (lucide v1.34.0), as lucide.dev's "Copy SVG" gives
        them for a 24 x 24 viewBox drawn with stroke-width 2 and round caps and joins. To add an icon,
        paste its elements here under its lucide name and add the name to Icon.

        Lucide is ISC-licensed, some icons MIT (Feather); see Lucide-LICENSE.txt beside this file. */

    /** The side of lucide's square viewBox. */
    constexpr float viewBox = 24.0f;

    struct LucideIcon
    {
        Icon icon;
        const char* name;
        const char* svg;
    };

    constexpr LucideIcon lucideIcons[] =
    {
        // Transport & audio
        { Icon::play, "play",
            R"(<path d="M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z"/>)" },
        { Icon::pause, "pause",
            R"(<rect x="14" y="3" width="5" height="18" rx="1"/>)"
            R"(<rect x="5" y="3" width="5" height="18" rx="1"/>)" },
        { Icon::square, "square",
            R"(<rect width="18" height="18" x="3" y="3" rx="2"/>)" },
        { Icon::circleDot, "circle-dot",
            R"(<circle cx="12" cy="12" r="10"/><circle cx="12" cy="12" r="1"/>)" },
        { Icon::skipBack, "skip-back",
            R"(<path d="M17.971 4.285A2 2 0 0 1 21 6v12a2 2 0 0 1-3.029 1.715l-9.997-5.998a2 2 0 0 1-.003-3.432z"/>)"
            R"(<path d="M3 20V4"/>)" },
        { Icon::repeat, "repeat",
            R"(<path d="m17 2 4 4-4 4"/><path d="M3 11v-1a4 4 0 0 1 4-4h14"/><path d="m7 22-4-4 4-4"/>)"
            R"(<path d="M21 13v1a4 4 0 0 1-4 4H3"/>)" },
        { Icon::rotateCcw, "rotate-ccw",
            R"(<path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/>)" },
        { Icon::refreshCw, "refresh-cw",
            R"(<path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/>)"
            R"(<path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16"/><path d="M8 16H3v5"/>)" },
        { Icon::timer, "timer",
            R"(<line x1="10" x2="14" y1="2" y2="2"/><line x1="12" x2="15" y1="14" y2="11"/>)"
            R"(<circle cx="12" cy="14" r="8"/>)" },
        { Icon::metronome, "metronome",
            R"(<path d="M12 11.4V9.1"/><path d="m12 17 6.59-6.59"/>)"
            R"(<path d="m15.05 5.7-.218-.691a3 3 0 0 0-5.663 0L4.418 19.695A1 1 0 0 0 5.37 21h13.253a1 1 0 0 0 .951-1.31L18.45 16.2"/>)"
            R"(<circle cx="20" cy="9" r="2"/>)" },
        { Icon::crosshair, "crosshair",
            R"(<circle cx="12" cy="12" r="10"/><line x1="22" x2="18" y1="12" y2="12"/>)"
            R"(<line x1="6" x2="2" y1="12" y2="12"/><line x1="12" x2="12" y1="6" y2="2"/>)"
            R"(<line x1="12" x2="12" y1="22" y2="18"/>)" },
        { Icon::audioLines, "audio-lines",
            R"(<path d="M2 10v3"/><path d="M6 6v11"/><path d="M10 3v18"/><path d="M14 8v7"/>)"
            R"(<path d="M18 5v13"/><path d="M22 10v3"/>)" },
        { Icon::audioWaveform, "audio-waveform",
            R"(<path d="M2 13a2 2 0 0 0 2-2V7a2 2 0 0 1 4 0v13a2 2 0 0 0 4 0V4a2 2 0 0 1 4 0v13a2 2 0 0 0 4 0v-4a2 2 0 0 1 2-2"/>)" },
        { Icon::activity, "activity",
            R"(<path d="M22 12h-2.48a2 2 0 0 0-1.93 1.46l-2.35 8.36a.25.25 0 0 1-.48 0L9.24 2.18a.25.25 0 0 0-.48 0l-2.35 8.36A2 2 0 0 1 4.49 12H2"/>)" },
        { Icon::waves, "waves",
            R"(<path d="M2 12q2.5 2 5 0t5 0 5 0 5 0"/><path d="M2 19q2.5 2 5 0t5 0 5 0 5 0"/>)"
            R"(<path d="M2 5q2.5 2 5 0t5 0 5 0 5 0"/>)" },
        { Icon::spline, "spline",
            R"(<circle cx="19" cy="5" r="2"/><circle cx="5" cy="19" r="2"/>)"
            R"(<path d="M5 17A12 12 0 0 1 17 5"/>)" },
        { Icon::chartSpline, "chart-spline",
            R"(<path d="M3 3v16a2 2 0 0 0 2 2h16"/>)"
            R"(<path d="M7 16c.5-2 1.5-7 4-7 2 0 2 3 4 3 2.5 0 4.5-5 5-7"/>)" },
        { Icon::piano, "piano",
            R"(<path d="M18.5 8c-1.4 0-2.6-.8-3.2-2A6.87 6.87 0 0 0 2 9v11a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-8.5C22 9.6 20.4 8 18.5 8"/>)"
            R"(<path d="M2 14h20"/><path d="M6 14v4"/><path d="M10 14v4"/><path d="M14 14v4"/>)"
            R"(<path d="M18 14v4"/>)" },
        { Icon::music, "music",
            R"(<path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/>)" },
        { Icon::music2, "music-2",
            R"(<circle cx="8" cy="18" r="4"/><path d="M12 18V2l7 4"/>)" },
        { Icon::volume2, "volume-2",
            R"(<path d="M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z"/>)"
            R"(<path d="M16 9a5 5 0 0 1 0 6"/><path d="M19.364 18.364a9 9 0 0 0 0-12.728"/>)" },
        { Icon::drum, "drum",
            R"(<path d="m2 2 8 8"/><path d="m22 2-8 8"/><ellipse cx="12" cy="9" rx="10" ry="5"/>)"
            R"(<path d="M7 13.4v7.9"/><path d="M12 14v8"/><path d="M17 13.4v7.9"/>)"
            R"(<path d="M2 9v8a10 5 0 0 0 20 0V9"/>)" },
        // Navigation & disclosure
        { Icon::chevronDown, "chevron-down",
            R"(<path d="m6 9 6 6 6-6"/>)" },
        { Icon::chevronRight, "chevron-right",
            R"(<path d="m9 18 6-6-6-6"/>)" },
        { Icon::chevronLeft, "chevron-left",
            R"(<path d="m15 18-6-6 6-6"/>)" },
        { Icon::arrowDown, "arrow-down",
            R"(<path d="M12 5v14"/><path d="m19 12-7 7-7-7"/>)" },
        { Icon::arrowLeft, "arrow-left",
            R"(<path d="m12 19-7-7 7-7"/><path d="M19 12H5"/>)" },
        { Icon::arrowRight, "arrow-right",
            R"(<path d="M5 12h14"/><path d="m12 5 7 7-7 7"/>)" },
        { Icon::arrowUpRight, "arrow-up-right",
            R"(<path d="M7 7h10v10"/><path d="M7 17 17 7"/>)" },
        { Icon::arrowLeftRight, "arrow-left-right",
            R"(<path d="M8 3 4 7l4 4"/><path d="M4 7h16"/><path d="m16 21 4-4-4-4"/><path d="M20 17H4"/>)" },
        { Icon::cornerDownRight, "corner-down-right",
            R"(<path d="m15 10 5 5-5 5"/><path d="M4 4v7a4 4 0 0 0 4 4h12"/>)" },
        { Icon::ellipsis, "ellipsis",
            R"(<circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/><circle cx="5" cy="12" r="1"/>)" },
        { Icon::x, "x",
            R"(<path d="M18 6 6 18"/><path d="m6 6 12 12"/>)" },
        { Icon::maximize, "maximize",
            R"(<path d="M8 3H5a2 2 0 0 0-2 2v3"/><path d="M21 8V5a2 2 0 0 0-2-2h-3"/>)"
            R"(<path d="M3 16v3a2 2 0 0 0 2 2h3"/><path d="M16 21h3a2 2 0 0 0 2-2v-3"/>)" },
        { Icon::maximize2, "maximize-2",
            R"(<path d="M15 3h6v6"/><path d="m21 3-7 7"/><path d="m3 21 7-7"/><path d="M9 21H3v-6"/>)" },
        { Icon::minimize2, "minimize-2",
            R"(<path d="m14 10 7-7"/><path d="M20 10h-6V4"/><path d="m3 21 7-7"/><path d="M4 14h6v6"/>)" },
        { Icon::zoomIn, "zoom-in",
            R"(<circle cx="11" cy="11" r="8"/><line x1="21" x2="16.65" y1="21" y2="16.65"/>)"
            R"(<line x1="11" x2="11" y1="8" y2="14"/><line x1="8" x2="14" y1="11" y2="11"/>)" },
        { Icon::zoomOut, "zoom-out",
            R"(<circle cx="11" cy="11" r="8"/><line x1="21" x2="16.65" y1="21" y2="16.65"/>)"
            R"(<line x1="8" x2="14" y1="11" y2="11"/>)" },
        // Editing tools
        { Icon::mousePointer2, "mouse-pointer-2",
            R"(<path d="M4.037 4.688a.495.495 0 0 1 .651-.651l16 6.5a.5.5 0 0 1-.063.947l-6.124 1.58a2 2 0 0 0-1.438 1.435l-1.579 6.126a.5.5 0 0 1-.947.063z"/>)" },
        { Icon::textCursor, "text-cursor",
            R"(<path d="M17 22h-1a4 4 0 0 1-4-4V6a4 4 0 0 1 4-4h1"/><path d="M7 22h1a4 4 0 0 0 4-4"/>)"
            R"(<path d="M7 2h1a4 4 0 0 1 4 4"/>)" },
        { Icon::pencil, "pencil",
            R"(<path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497z"/>)"
            R"(<path d="m15 5 4 4"/>)" },
        { Icon::eraser, "eraser",
            R"(<path d="M21 21H8a2 2 0 0 1-1.42-.587l-3.994-3.999a2 2 0 0 1 0-2.828l10-10a2 2 0 0 1 2.829 0l5.999 6a2 2 0 0 1 0 2.828L12.834 21"/>)"
            R"(<path d="m5.082 11.09 8.828 8.828"/>)" },
        { Icon::scissors, "scissors",
            R"(<circle cx="6" cy="6" r="3"/><path d="M8.12 8.12 12 12"/><path d="M20 4 8.12 15.88"/>)"
            R"(<circle cx="6" cy="18" r="3"/><path d="M14.8 14.8 20 20"/>)" },
        { Icon::scissorsLineDashed, "scissors-line-dashed",
            R"(<path d="M5.42 9.42 8 12"/><circle cx="4" cy="8" r="2"/><path d="m14 6-8.58 8.58"/>)"
            R"(<circle cx="4" cy="16" r="2"/><path d="M10.8 14.8 14 18"/><path d="M16 12h-2"/>)"
            R"(<path d="M22 12h-2"/>)" },
        { Icon::magnet, "magnet",
            R"(<path d="m12 15 4 4"/>)"
            R"(<path d="M2.352 10.648a1.205 1.205 0 0 0 0 1.704l2.296 2.296a1.205 1.205 0 0 0 1.704 0l6.029-6.029a1 1 0 1 1 3 3l-6.029 6.029a1.205 1.205 0 0 0 0 1.704l2.296 2.296a1.205 1.205 0 0 0 1.704 0l6.365-6.367A1 1 0 0 0 8.716 4.282z"/>)"
            R"(<path d="m5 8 4 4"/>)" },
        { Icon::foldVertical, "fold-vertical",
            R"(<path d="M12 22v-6"/><path d="M12 8V2"/><path d="M4 12H2"/><path d="M10 12H8"/>)"
            R"(<path d="M16 12h-2"/><path d="M22 12h-2"/><path d="m15 19-3-3-3 3"/>)"
            R"(<path d="m15 5-3 3-3-3"/>)" },
        { Icon::unfoldVertical, "unfold-vertical",
            R"(<path d="M12 22v-6"/><path d="M12 8V2"/><path d="M4 12H2"/><path d="M10 12H8"/>)"
            R"(<path d="M16 12h-2"/><path d="M22 12h-2"/><path d="m15 19-3 3-3-3"/>)"
            R"(<path d="m15 5-3-3-3 3"/>)" },
        { Icon::copy, "copy",
            R"(<rect width="14" height="14" x="8" y="8" rx="2" ry="2"/>)"
            R"(<path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/>)" },
        { Icon::trash2, "trash-2",
            R"(<path d="M10 11v6"/><path d="M14 11v6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/>)"
            R"(<path d="M3 6h18"/><path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>)" },
        { Icon::plus, "plus",
            R"(<path d="M5 12h14"/><path d="M12 5v14"/>)" },
        { Icon::undo2, "undo-2",
            R"(<path d="M9 14 4 9l5-5"/>)"
            R"(<path d="M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11"/>)" },
        { Icon::flag, "flag",
            R"(<path d="M4 22V4a1 1 0 0 1 .4-.8A6 6 0 0 1 8 2c3 0 5 2 7.333 2q2 0 3.067-.8A1 1 0 0 1 20 4v10a1 1 0 0 1-.4.8A6 6 0 0 1 16 16c-3 0-5-2-8-2a6 6 0 0 0-4 1.528"/>)" },
        { Icon::search, "search",
            R"(<path d="m21 21-4.34-4.34"/><circle cx="11" cy="11" r="8"/>)" },
        { Icon::gripVertical, "grip-vertical",
            R"(<circle cx="9" cy="12" r="1"/><circle cx="9" cy="5" r="1"/><circle cx="9" cy="19" r="1"/>)"
            R"(<circle cx="15" cy="12" r="1"/><circle cx="15" cy="5" r="1"/><circle cx="15" cy="19" r="1"/>)" },
        // Routing, racks & structure
        { Icon::gitMerge, "git-merge",
            R"(<circle cx="18" cy="18" r="3"/><circle cx="6" cy="6" r="3"/>)"
            R"(<path d="M6 21V9a9 9 0 0 0 9 9"/>)" },
        { Icon::layers, "layers",
            R"(<path d="M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83z"/>)"
            R"(<path d="M2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12"/>)"
            R"(<path d="M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17"/>)" },
        { Icon::network, "network",
            R"(<rect x="16" y="16" width="6" height="6" rx="1"/>)"
            R"(<rect x="2" y="16" width="6" height="6" rx="1"/>)"
            R"(<rect x="9" y="2" width="6" height="6" rx="1"/>)"
            R"(<path d="M5 16v-3a1 1 0 0 1 1-1h12a1 1 0 0 1 1 1v3"/><path d="M12 12V8"/>)" },
        { Icon::folder, "folder",
            R"(<path d="M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z"/>)" },
        { Icon::folderOpen, "folder-open",
            R"(<path d="m6 14 1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6a2 2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2"/>)" },
        { Icon::folderMinus, "folder-minus",
            R"(<path d="M9 13h6"/>)"
            R"(<path d="M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z"/>)" },
        { Icon::list, "list",
            R"(<path d="M3 5h.01"/><path d="M3 12h.01"/><path d="M3 19h.01"/><path d="M8 5h13"/>)"
            R"(<path d="M8 12h13"/><path d="M8 19h13"/>)" },
        { Icon::slidersHorizontal, "sliders-horizontal",
            R"(<path d="M10 5H3"/><path d="M12 19H3"/><path d="M14 3v4"/><path d="M16 17v4"/>)"
            R"(<path d="M21 12h-9"/><path d="M21 19h-5"/><path d="M21 5h-7"/><path d="M8 10v4"/>)"
            R"(<path d="M8 12H3"/>)" },
        { Icon::slidersVertical, "sliders-vertical",
            R"(<path d="M10 8h4"/><path d="M12 21v-9"/><path d="M12 8V3"/><path d="M17 16h4"/>)"
            R"(<path d="M19 12V3"/><path d="M19 21v-5"/><path d="M3 14h4"/><path d="M5 10V3"/>)"
            R"(<path d="M5 21v-7"/>)" },
        { Icon::link2Off, "link-2-off",
            R"(<path d="M9 17H7A5 5 0 0 1 7 7"/><path d="M15 7h2a5 5 0 0 1 4 8"/>)"
            R"(<line x1="8" x2="12" y1="12" y2="12"/><line x1="2" x2="22" y1="2" y2="22"/>)" },
        { Icon::unlink, "unlink",
            R"(<path d="m18.84 12.25 1.72-1.71h-.02a5.004 5.004 0 0 0-.12-7.07 5.006 5.006 0 0 0-6.95 0l-1.72 1.71"/>)"
            R"(<path d="m5.17 11.75-1.71 1.71a5.004 5.004 0 0 0 .12 7.07 5.006 5.006 0 0 0 6.95 0l1.71-1.71"/>)"
            R"(<line x1="8" x2="8" y1="2" y2="5"/><line x1="2" x2="5" y1="8" y2="8"/>)"
            R"(<line x1="16" x2="16" y1="19" y2="22"/><line x1="19" x2="22" y1="16" y2="16"/>)" },
        // Files & visibility
        { Icon::file, "file",
            R"(<path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z"/>)"
            R"(<path d="M14 2v5a1 1 0 0 0 1 1h5"/>)" },
        { Icon::fileMusic, "file-music",
            R"(<path d="M11.65 22H18a2 2 0 0 0 2-2V8a2.4 2.4 0 0 0-.706-1.706l-3.588-3.588A2.4 2.4 0 0 0 14 2H6a2 2 0 0 0-2 2v10.35"/>)"
            R"(<path d="M14 2v5a1 1 0 0 0 1 1h5"/><path d="M8 20v-7l3 1.474"/>)"
            R"(<circle cx="6" cy="20" r="2"/>)" },
        { Icon::fileCode, "file-code",
            R"(<path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z"/>)"
            R"(<path d="M14 2v5a1 1 0 0 0 1 1h5"/><path d="M10 12.5 8 15l2 2.5"/>)"
            R"(<path d="m14 12.5 2 2.5-2 2.5"/>)" },
        { Icon::fileText, "file-text",
            R"(<path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z"/>)"
            R"(<path d="M14 2v5a1 1 0 0 0 1 1h5"/>)"
            R"(<path d="M10 9H8"/><path d="M16 13H8"/><path d="M16 17H8"/>)" },
        { Icon::download, "download",
            R"(<path d="M12 15V3"/><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>)"
            R"(<path d="m7 10 5 5 5-5"/>)" },
        { Icon::eye, "eye",
            R"(<path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"/>)"
            R"(<circle cx="12" cy="12" r="3"/>)" },
        { Icon::eyeOff, "eye-off",
            R"(<path d="M10.733 5.076a10.744 10.744 0 0 1 11.205 6.575 1 1 0 0 1 0 .696 10.747 10.747 0 0 1-1.444 2.49"/>)"
            R"(<path d="M14.084 14.158a3 3 0 0 1-4.242-4.242"/>)"
            R"(<path d="M17.479 17.499a10.75 10.75 0 0 1-15.417-5.151 1 1 0 0 1 0-.696 10.75 10.75 0 0 1 4.446-5.143"/>)"
            R"(<path d="m2 2 20 20"/>)" },
        // Plug-ins & windows
        { Icon::plug, "plug",
            R"(<path d="M12 22v-5"/><path d="M15 8V2"/>)"
            R"(<path d="M17 8a1 1 0 0 1 1 1v4a4 4 0 0 1-4 4h-4a4 4 0 0 1-4-4V9a1 1 0 0 1 1-1z"/>)"
            R"(<path d="M9 8V2"/>)" },
        { Icon::appWindow, "app-window",
            R"(<rect x="2" y="4" width="20" height="16" rx="2"/><path d="M10 4v4"/><path d="M2 8h20"/>)"
            R"(<path d="M6 4v4"/>)" },
        { Icon::externalLink, "external-link",
            R"(<path d="M15 3h6v6"/><path d="M10 14 21 3"/>)"
            R"(<path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/>)" },
        { Icon::pin, "pin",
            R"(<path d="M12 17v5"/>)"
            R"(<path d="M9 10.76a2 2 0 0 1-1.11 1.79l-1.78.9A2 2 0 0 0 5 15.24V16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-.76a2 2 0 0 0-1.11-1.79l-1.78-.9A2 2 0 0 1 15 10.76V7a1 1 0 0 1 1-1 2 2 0 0 0 0-4H8a2 2 0 0 0 0 4 1 1 0 0 1 1 1z"/>)" },
        { Icon::power, "power",
            R"(<path d="M12 2v10"/><path d="M18.4 6.6a9 9 0 1 1-12.77.04"/>)" },
        { Icon::cpu, "cpu",
            R"(<path d="M12 20v2"/><path d="M12 2v2"/><path d="M17 20v2"/><path d="M17 2v2"/>)"
            R"(<path d="M2 12h2"/><path d="M2 17h2"/><path d="M2 7h2"/><path d="M20 12h2"/>)"
            R"(<path d="M20 17h2"/><path d="M20 7h2"/><path d="M7 20v2"/><path d="M7 2v2"/>)"
            R"(<rect x="4" y="4" width="16" height="16" rx="2"/>)"
            R"(<rect x="8" y="8" width="8" height="8" rx="1"/>)" },
        { Icon::shieldCheck, "shield-check",
            R"(<path d="M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z"/>)"
            R"(<path d="m9 12 2 2 4-4"/>)" },
        { Icon::save, "save",
            R"(<path d="M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/>)"
            R"(<path d="M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7"/><path d="M7 3v4a1 1 0 0 0 1 1h7"/>)" },
        { Icon::redo2, "redo-2",
            R"(<path d="m15 14 5-5-5-5"/><path d="M20 9H9.5A5.5 5.5 0 0 0 4 14.5A5.5 5.5 0 0 0 9.5 20H13"/>)" },
        { Icon::moveDiagonal2, "move-diagonal-2",
            R"(<path d="M19 13v6h-6"/><path d="M5 11V5h6"/><path d="m5 5 14 14"/>)" },
        { Icon::squareDashed, "square-dashed",
            R"(<path d="M5 3a2 2 0 0 0-2 2"/><path d="M19 3a2 2 0 0 1 2 2"/>)"
            R"(<path d="M21 19a2 2 0 0 1-2 2"/><path d="M5 21a2 2 0 0 1-2-2"/><path d="M9 3h1"/>)"
            R"(<path d="M9 21h1"/><path d="M14 3h1"/><path d="M14 21h1"/><path d="M3 9v1"/>)"
            R"(<path d="M21 9v1"/><path d="M3 14v1"/><path d="M21 14v1"/>)" },
        { Icon::sparkles, "sparkles",
            R"(<path d="M11.017 2.814a1 1 0 0 1 1.966 0l1.051 5.558a2 2 0 0 0 1.594 1.594l5.558 1.051a1 1 0 0 1 0 1.966l-5.558 1.051a2 2 0 0 0-1.594 1.594l-1.051 5.558a1 1 0 0 1-1.966 0l-1.051-5.558a2 2 0 0 0-1.594-1.594l-5.558-1.051a1 1 0 0 1 0-1.966l5.558-1.051a2 2 0 0 0 1.594-1.594z"/>)"
            R"(<path d="M20 2v4"/><path d="M22 4h-4"/><circle cx="4" cy="20" r="2"/>)" },
        { Icon::package, "package",
            R"(<path d="M11 21.73a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73z"/>)"
            R"(<path d="M12 22V12"/><polyline points="3.29 7 12 12 20.71 7"/><path d="m7.5 4.27 9 5.15"/>)" },
        { Icon::box, "box",
            R"(<path d="M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z"/>)"
            R"(<path d="m3.3 7 8.7 5 8.7-5"/><path d="M12 22V12"/>)" },
        { Icon::puzzle, "puzzle",
            R"(<path d="M15.39 4.39a1 1 0 0 0 1.68-.474 2.5 2.5 0 1 1 3.014 3.015 1 1 0 0 0-.474 1.68l1.683 1.682a2.414 2.414 0 0 1 0 3.414L19.61 15.39a1 1 0 0 1-1.68-.474 2.5 2.5 0 1 0-3.014 3.015 1 1 0 0 1 .474 1.68l-1.683 1.682a2.414 2.414 0 0 1-3.414 0L8.61 19.61a1 1 0 0 0-1.68.474 2.5 2.5 0 1 1-3.014-3.015 1 1 0 0 0 .474-1.68l-1.683-1.682a2.414 2.414 0 0 1 0-3.414L4.39 8.61a1 1 0 0 1 1.68.474 2.5 2.5 0 1 0 3.014-3.015 1 1 0 0 1-.474-1.68l1.683-1.682a2.414 2.414 0 0 1 3.414 0z"/>)" },
        // Sidechain
        { Icon::keyRound, "key-round",
            R"(<path d="M2.586 17.414A2 2 0 0 0 2 18.828V21a1 1 0 0 0 1 1h3a1 1 0 0 0 1-1v-1a1 1 0 0 1 1-1h1a1 1 0 0 0 1-1v-1a1 1 0 0 1 1-1h.172a2 2 0 0 0 1.414-.586l.814-.814a6.5 6.5 0 1 0-4-4z"/>)"
            R"(<circle cx="16.5" cy="7.5" r=".5" fill="currentColor"/>)" },
        { Icon::headphones, "headphones",
            R"(<path d="M3 14h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-7a9 9 0 0 1 18 0v7a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3"/>)" },
        { Icon::funnel, "funnel",
            R"(<path d="M10 20a1 1 0 0 0 .553.895l2 1A1 1 0 0 0 14 21v-7a2 2 0 0 1 .517-1.341L21.74 4.67A1 1 0 0 0 21 3H3a1 1 0 0 0-.742 1.67l7.225 7.989A2 2 0 0 1 10 14z"/>)" },
        { Icon::cable, "cable",
            R"(<path d="M17 19a1 1 0 0 1-1-1v-2a2 2 0 0 1 2-2h2a2 2 0 0 1 2 2v2a1 1 0 0 1-1 1z"/>)"
            R"(<path d="M17 21v-2"/><path d="M19 14V6.5a1 1 0 0 0-7 0v11a1 1 0 0 1-7 0V10"/>)"
            R"(<path d="M21 21v-2"/><path d="M3 5V3"/>)"
            R"(<path d="M4 10a2 2 0 0 1-2-2V6a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v2a2 2 0 0 1-2 2z"/>)"
            R"(<path d="M7 5V3"/>)" },
        { Icon::check, "check",
            R"(<path d="M20 6 9 17l-5-5"/>)" },
        { Icon::arrowRightToLine, "arrow-right-to-line",
            R"(<path d="M17 12H3"/><path d="m11 18 6-6-6-6"/><path d="M21 5v14"/>)" },
    };

    const LucideIcon* find (Icon icon)
    {
        for (auto& entry : lucideIcons)
            if (entry.icon == icon)
                return &entry;

        return nullptr;
    }

    void appendOutlines (const juce::Component& drawable, juce::Path& outline)
    {
        if (auto* shape = dynamic_cast<const juce::DrawablePath*> (&drawable))
            outline.addPath (shape->getPath(), shape->getTransform());

        for (auto* child : drawable.getChildren())
            appendOutlines (*child, outline);
    }

    juce::Path parseOutline (const char* elements)
    {
        juce::Path outline;
        const auto xml = juce::parseXML (juce::String ("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" "
                                                       "viewBox=\"0 0 24 24\">")
                                         + elements + "</svg>");
        jassert (xml != nullptr);

        if (xml != nullptr)
            if (auto drawable = juce::Drawable::createFromSVG (*xml))
                appendOutlines (*drawable, outline);

        jassert (! outline.isEmpty());
        return outline;
    }

    using Outlines = std::array<juce::Path, std::size (lucideIcons)>;

    const Outlines& outlines()
    {
        static const Outlines parsed = []
        {
            Outlines result;

            for (size_t i = 0; i < result.size(); ++i)
                result[i] = parseOutline (lucideIcons[i].svg);

            return result;
        }();

        return parsed;
    }
}

juce::Colour iconColour (const Theme& theme, IconRole role)
{
    switch (role)
    {
        case IconRole::normal:    return theme.textSecondary;
        case IconRole::inactive:  return theme.textDim;
        case IconRole::active:    return theme.accent;
        case IconRole::onFilled:  return theme.textOnAccent;
        case IconRole::sidechain: return theme.stateSidechain;
    }

    return theme.textSecondary;
}

juce::String lucideName (Icon icon)
{
    if (auto* entry = find (icon))
        return entry->name;

    jassertfalse;
    return {};
}

std::optional<Icon> iconNamed (const juce::String& name)
{
    for (auto& entry : lucideIcons)
        if (name == entry.name)
            return entry.icon;

    return std::nullopt;
}

const juce::Path& iconPath (Icon icon)
{
    static const juce::Path none;

    if (auto* entry = find (icon))
        return outlines()[(size_t) (entry - lucideIcons)];

    jassertfalse;
    return none;
}

float iconStrokeWidth (float size, float physicalScale)
{
    const auto lucideWidth = size * 2.0f / viewBox;
    return physicalScale > 0.0f ? juce::jmax (lucideWidth, 1.0f / physicalScale) : lucideWidth;
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto size = juce::jmin (area.getWidth(), area.getHeight());

    if (size <= 0.0f)
        return;

    const auto box = area.withSizeKeepingCentre (size, size);
    const auto stroke = iconStrokeWidth (size, g.getInternalContext().getPhysicalPixelScaleFactor());

    g.setColour (colour);
    g.strokePath (iconPath (icon),
                  juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::scale (size / viewBox).translated (box.getX(), box.getY()));
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Point<float> centre, float size, juce::Colour colour)
{
    drawIcon (g, icon, juce::Rectangle<float> (size, size).withCentre (centre), colour);
}

} // namespace resamper
