// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SpectrumAnalyser.h"
#include <cmath>

namespace wavelathe
{
namespace
{
// How fast a bin is allowed to fall, per frame at screen rate. Slow enough that
// a note's spectrum stays readable after it stops, fast enough that the curve
// follows a filter sweep rather than trailing behind it.
constexpr float fallDbPerFrame = 1.6f;

// A Hann window passes half the energy of the signal under it, so a sine
// analysed through one reads 6 dB low unless the scale accounts for it. Folding
// it in here is what lets the numbers on this curve be compared with the ones
// on the meter beside it.
constexpr float windowCoherentGain = 0.5f;
} // namespace

SpectrumAnalyser::SpectrumAnalyser()
    : fft(fftOrder), window((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false)
{
    reset();
}

void SpectrumAnalyser::prepare(double sampleRateToUse)
{
    sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 48000.0;
    reset();
}

void SpectrumAnalyser::reset()
{
    magnitudesDb.fill(floorDb);
    scratch.fill(0.0f);
    hasFrame = false;
}

bool SpectrumAnalyser::update(const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples < fftSize)
        return false;

    // The newest window, not the oldest: a spectrum a tenth of a second behind
    // the sound is a spectrum of a note you have already stopped playing.
    const float* newest = samples + (numSamples - fftSize);

    std::copy(newest, newest + fftSize, scratch.begin());
    std::fill(scratch.begin() + fftSize, scratch.end(), 0.0f);

    window.multiplyWithWindowingTable(scratch.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform(scratch.data());

    // Two over N over the window's gain: the factor that makes a full-scale
    // sine read 0 dB rather than some number that depends on the window size.
    const float scale = 2.0f / ((float) fftSize * windowCoherentGain);

    for (int bin = 0; bin < numBins; ++bin)
    {
        const float magnitude = scratch[(size_t) bin] * scale;
        const float db = magnitude > 1.0e-6f ? 20.0f * std::log10(magnitude) : floorDb;

        float& shown = magnitudesDb[(size_t) bin];

        // Straight up, slowly down, for the same reason the meter beside it
        // does: a peak that is averaged on the way in is a peak understated.
        shown = db > shown ? db : juce::jmax(floorDb, shown - fallDbPerFrame);
    }

    hasFrame = true;
    return true;
}

double SpectrumAnalyser::getBinFrequency(int bin) const
{
    return (double) bin * sampleRate / (double) fftSize;
}

float SpectrumAnalyser::getBinDb(int bin) const
{
    if (bin < 0 || bin >= numBins)
        return floorDb;

    return magnitudesDb[(size_t) bin];
}

float SpectrumAnalyser::getDbInRange(double lowHz, double highHz) const
{
    if (!hasFrame)
        return floorDb;

    if (highHz < lowHz)
        std::swap(lowHz, highHz);

    const double binsPerHz = (double) fftSize / sampleRate;

    int firstBin = (int) std::floor(lowHz * binsPerHz);
    int lastBin = (int) std::ceil(highHz * binsPerHz);

    firstBin = juce::jlimit(0, numBins - 1, firstBin);
    lastBin = juce::jlimit(0, numBins - 1, lastBin);

    // A span narrower than one bin still has to answer with that bin rather
    // than with nothing, which is the whole bottom of the range on a log plot.
    float loudest = floorDb;
    for (int bin = firstBin; bin <= lastBin; ++bin)
        loudest = juce::jmax(loudest, magnitudesDb[(size_t) bin]);

    return loudest;
}
} // namespace wavelathe
