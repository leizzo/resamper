#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace resamper::dsp
{

/** The shape of one EQ Eight band (PRD §9.2.1a): 12 dB / octave cuts,
    shelves, a bell and a notch. The filter that realises one lives in the
    engine (NativeDeviceDsp.h); this is the part a view names. */
enum class EqBandType { lowCut, lowShelf, bell, notch, highShelf, highCut };

inline constexpr int numEqBandTypes = 6;

/** Whether a band type has a gain (the cuts and the notch don't). */
inline bool hasGain (EqBandType t)
{
    return t == EqBandType::lowShelf || t == EqBandType::bell || t == EqBandType::highShelf;
}

/** The span a band's Q covers, as the ratio of its upper edge to its frequency
    (the edges are hz / ratio and hz * ratio): what the Q-width shading shows. */
inline double qEdgeRatio (double q)
{
    const auto octaves = 2.0 / std::log (2.0) * std::asinh (1.0 / (2.0 * juce::jmax (0.05, q)));
    return std::pow (2.0, octaves / 2.0);
}

} // namespace resamper::dsp
