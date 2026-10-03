# Theme owns style, Layout Metrics owns geometry

The brief contradicts itself: §17's theme example contains `"trackHeight": 72`, and §18's `LayoutMetrics` struct also defines `trackHeight`. One value with two owners is the bug class ADR-0002 banned. We decided: **Theme owns style** (colours, radii, fonts), **Layout Metrics owns geometry** (heights, widths). `LayoutMetrics` is a struct populated from JSON at theme load, never hard-coded; the two may live in one JSON file under separate `colors`/`metrics` keys.

**Considered options:** (b) Merge everything into Theme — rejected because geometry and style change for different reasons (metrics per platform/DPI, colours per user taste). (c) Keep both and police overlap by convention — rejected; conventions without enforcement are how §17/§18 drifted apart.

**Consequences:** Brief §17's example theme is amended — `trackHeight` moves out of Theme. Components read geometry from Layout Metrics and style from Theme, never the reverse.
