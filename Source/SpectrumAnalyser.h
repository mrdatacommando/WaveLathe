// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace wavelathe
{
// Turns a window of audio into a level per frequency, for drawing.
//
// Separate from the view that draws it because the two go wrong in different
// ways and only one of them can be tested: whether a 1 kHz tone lands on 1 kHz
// and reads 0 dBFS is a question with an answer, and whether the resulting
// curve looks good is not.
//
// Scaled so that a sine at full scale reads 0 dBFS. Without that the numbers
// down the side are in units of "however long the window happened to be", which
// is no use at all next to a meter that reads in real decibels.
class SpectrumAnalyser
{
public:
    // 4096 points: 11.7 Hz per bin at 48 kHz. The next size down doubles that,
    // which puts the bottom two octaves - where EQ decisions are hardest and
    // most consequential - inside one or two bins.
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int numBins = fftSize / 2;

    SpectrumAnalyser();

    void prepare(double sampleRateToUse);
    void reset();

    // One frame, over the newest audio. Returns false when there is not a full
    // window yet, so a view can say "silent" rather than draw a floor.
    bool update(const float* samples, int numSamples);

    double getBinFrequency(int bin) const;
    float getBinDb(int bin) const;

    // The loudest bin between two frequencies, in dBFS. Loudest rather than
    // average because at the top of the range dozens of bins share one pixel,
    // and averaging them buries exactly the narrow peak worth seeing.
    float getDbInRange(double lowHz, double highHz) const;

    static constexpr float floorDb = -96.0f;

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;

    // Twice fftSize because the transform writes the mirrored half too.
    std::array<float, fftSize * 2> scratch{};

    // What is displayed: rises instantly to a new peak, falls slowly. A curve
    // that tracked the transform exactly would flicker too fast to read, and
    // one that was smoothed both ways would understate the transients.
    std::array<float, numBins> magnitudesDb{};

    double sampleRate = 48000.0;
    bool hasFrame = false;
};
} // namespace wavelathe
