#pragma once

#include <compare>

namespace resamper
{

/** A level in decibels: a track or master fader, a send. Its own type, so a dB
    value cannot be passed where a pan, a linear gain or a fader position is
    expected, or the reverse; code that does the maths reads .value. */
struct Decibels
{
    double value = 0;

    constexpr Decibels() noexcept = default;
    constexpr explicit Decibels (double db) noexcept : value (db) {}

    constexpr auto operator<=> (const Decibels&) const = default;
};

} // namespace resamper
