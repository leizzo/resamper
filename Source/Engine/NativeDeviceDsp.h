#pragma once

#include "EngineInternal.h"
#ifdef RESAMPER_ENGINE_INTERNAL
#include "EqBand.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>

namespace resamper::dsp
{

//==============================================================================
/** One second-order section, normalised so a0 is 1. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    /** Its gain at frequency hz, in dB, at sampleRate. */
    double magnitudeDb (double hz, double sampleRate) const
    {
        const auto w = juce::MathConstants<double>::twoPi * hz / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto h = (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
        return 20.0 * std::log10 (std::max (std::abs (h), 1.0e-9));
    }
};

/** Adaptive Q: a band's Q grows as its boost or cut does (×2 at 12 dB). */
inline double adaptiveQ (double q, double gainDb)
{
    return q * std::pow (2.0, std::abs (gainDb) / 12.0);
}

/** A second-order analog prototype, (n0 + n1 s + n2 s²) / (d0 + d1 s + d2 s²),
    with s normalised to the band's frequency. */
struct AnalogSection
{
    double n0, n1, n2, d0, d1, d2;

    /** Its squared magnitude at ratio times the band's frequency. */
    double magnitudeSquared (double ratio) const
    {
        const auto w2 = ratio * ratio;
        const auto re = [w2] (double c0, double c2) { return c0 - c2 * w2; };
        return (std::pow (re (n0, n2), 2.0) + n1 * n1 * w2) / (std::pow (re (d0, d2), 2.0) + d1 * d1 * w2);
    }
};

/** The analog band an EQ Eight band type stands for: the RBJ Audio EQ
    Cookbook's prototypes, gain in dB and Q as the cookbook takes them. */
inline AnalogSection analogBand (EqBandType type, double gainDb, double q)
{
    const auto a = std::pow (10.0, gainDb / 40.0), sqrtA = std::sqrt (a);

    switch (type)
    {
        case EqBandType::lowCut:    return { 0, 0, 1, 1, 1 / q, 1 };
        case EqBandType::highCut:   return { 1, 0, 0, 1, 1 / q, 1 };
        case EqBandType::bell:      return { 1, a / q, 1, 1, 1 / (a * q), 1 };
        case EqBandType::notch:     return { 1, 0, 1, 1, 1 / q, 1 };
        case EqBandType::lowShelf:  return { a * a, a * sqrtA / q, a, 1, sqrtA / q, a };
        case EqBandType::highShelf: return { a, a * sqrtA / q, a * a, a, sqrtA / q, 1 };
    }

    return { 1, 0, 0, 1, 0, 0 };
}

/** The digital section for a band: its analog prototype's poles mapped by the
    matched z-transform, and zeros chosen so the magnitude is the prototype's
    at DC, at Nyquist and at the band's frequency (Vicanek, "Matched Second
    Order Digital Filters", 2016). Unlike the bilinear transform it doesn't
    cramp a band toward Nyquist, and it adds no latency. Frequency is clamped
    below Nyquist. */
inline Biquad designBand (EqBandType type, double hz, double gainDb, double q, double sampleRate)
{
    hz = juce::jlimit (10.0, sampleRate * 0.5 * 0.98, hz);
    const auto analog = analogBand (type, gainDb, juce::jmax (0.05, q));
    const auto w0 = juce::MathConstants<double>::twoPi * hz / sampleRate;

    // The poles: the prototype's, at e^(sT).
    const auto poleHz = std::sqrt (analog.d0 / analog.d2);
    const auto zeta = analog.d1 / (2.0 * std::sqrt (analog.d0 * analog.d2));
    const auto decay = zeta * poleHz * w0, spread = std::sqrt (std::abs (zeta * zeta - 1.0)) * poleHz * w0;
    const auto a1 = -2.0 * std::exp (-decay) * (zeta <= 1.0 ? std::cos (spread) : std::cosh (spread));
    const auto a2 = std::exp (-2.0 * decay);

    // |H(w)|² of a section is (B0 φ0 + B1 φ1 + B2 φ2) / (A0 φ0 + A1 φ1 + A2 φ2), with
    // φ1 = sin²(w/2), φ0 = 1 - φ1 and φ2 = 4 φ0 φ1: fix B at w = 0, π and w0.
    const auto phi1 = std::pow (std::sin (w0 / 2.0), 2.0), phi0 = 1.0 - phi1, phi2 = 4.0 * phi0 * phi1;
    const std::complex<double> z1 = std::polar (1.0, -w0);
    const auto poleSquared = std::norm (1.0 + a1 * z1 + a2 * z1 * z1);

    const auto b0Sum = std::sqrt (std::pow (1 + a1 + a2, 2.0) * analog.magnitudeSquared (0.0));
    const auto b1Sum = std::sqrt (std::pow (1 - a1 + a2, 2.0) * analog.magnitudeSquared (sampleRate * 0.5 / hz));
    const auto bigB2 = (analog.magnitudeSquared (1.0) * poleSquared - b0Sum * b0Sum * phi0 - b1Sum * b1Sum * phi1) / phi2;

    const auto w = (b0Sum + b1Sum) / 2.0;
    const auto b0 = juce::jmax (1.0e-12, (w + std::sqrt (juce::jmax (0.0, w * w + bigB2))) / 2.0);

    return { b0, (b0Sum - b1Sum) / 2.0, -bigB2 / (4.0 * b0), a1, a2 };
}

/** The band-pass an audition plays: what one band acts on, alone. */
inline Biquad designAudition (double hz, double q, double sampleRate)
{
    hz = juce::jlimit (10.0, sampleRate * 0.49, hz);
    const auto w = juce::MathConstants<double>::twoPi * hz / sampleRate;
    const auto alpha = std::sin (w) / (2.0 * juce::jmax (0.05, q));
    const auto a0 = 1 + alpha;
    return { alpha / a0, 0, -alpha / a0, -2 * std::cos (w) / a0, (1 - alpha) / a0 };
}

/** One channel's state of a Biquad (transposed direct form II). */
struct BiquadState
{
    double z1 = 0, z2 = 0;

    float process (const Biquad& c, float in) noexcept
    {
        const double x = in;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }

    void reset() noexcept   { z1 = z2 = 0; }
};

//==============================================================================
/** How Compressor v2 reads its input (PRD §9.2.1a): the peak, a 10 ms RMS, or
    peak into a downward expander instead of a compressor. */
enum class DetectMode { peak, rms, expand };

/** The static curve of Compressor v2: output level for an input level, in dB,
    with a soft knee of kneeDb centred on the threshold. */
inline double transferDb (double inputDb, double thresholdDb, double ratio, double kneeDb, bool expand)
{
    const auto over = inputDb - thresholdDb;
    ratio = juce::jmax (1.0, ratio);

    if (! expand)
    {
        if (2 * over <= -kneeDb)
            return inputDb;

        if (kneeDb > 0 && 2 * std::abs (over) < kneeDb)
            return inputDb + (1.0 / ratio - 1.0) * std::pow (over + kneeDb / 2, 2.0) / (2 * kneeDb);

        return thresholdDb + over / ratio;
    }

    if (2 * over >= kneeDb)
        return inputDb;

    if (kneeDb > 0 && 2 * std::abs (over) < kneeDb)
        return inputDb - (ratio - 1.0) * std::pow (over - kneeDb / 2, 2.0) / (2 * kneeDb);

    return thresholdDb + over * ratio;
}

/** Auto makeup: half the reduction a full-scale signal would get, so a
    compressed mix comes back to about the level it went in at. */
inline double autoMakeupDb (double thresholdDb, double ratio, double kneeDb, bool expand)
{
    return expand ? 0.0 : juce::jmax (0.0, (0.0 - transferDb (0.0, thresholdDb, ratio, kneeDb, false)) / 2.0);
}

/** The lookahead choices, in ms: 0, 1 and 10. */
inline double lookaheadMs (int choice)
{
    return choice <= 0 ? 0.0 : choice == 1 ? 1.0 : 10.0;
}

//==============================================================================
/** Stores the largest value written since the last take(): how a meter's
    peak crosses from the audio thread to the UI. Wait-free on both sides. */
struct PeakSince
{
    std::atomic<float> value { 0.0f };

    static_assert (std::atomic<float>::is_always_lock_free);

    /** Audio thread. */
    void raise (float v) noexcept
    {
        auto current = value.load (std::memory_order_relaxed);

        while (v > current && ! value.compare_exchange_weak (current, v, std::memory_order_relaxed))
        {
        }
    }

    /** UI thread: the peak since the last take, and starts again from zero. */
    float take() noexcept   { return value.exchange (0.0f, std::memory_order_relaxed); }
};

} // namespace resamper::dsp
#endif
