// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <juce_dsp/juce_dsp.h>
#include "SpectrumAnalyser.h"
#include "MasterEffects.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace wavelathe;

namespace
{
int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition)
        ++failures;
}

constexpr double sampleRate = 48000.0;

std::vector<float> makeSine(double frequency, float amplitude, int numSamples)
{
    std::vector<float> samples((size_t) numSamples);

    for (int i = 0; i < numSamples; ++i)
        samples[(size_t) i] =
            amplitude * (float) std::sin(2.0 * juce::MathConstants<double>::pi * frequency
                                          * (double) i / sampleRate);

    return samples;
}

// Where the loudest bin actually is, which is the question the display asks in
// a roundabout way and the question a test can ask directly.
int loudestBin(const SpectrumAnalyser& analyser)
{
    int best = 0;
    float bestDb = SpectrumAnalyser::floorDb;

    for (int bin = 0; bin < analyser.numBins; ++bin)
        if (analyser.getBinDb(bin) > bestDb)
        {
            bestDb = analyser.getBinDb(bin);
            best = bin;
        }

    return best;
}

float decibelsOf(float gain) { return 20.0f * std::log10(juce::jmax(1.0e-6f, gain)); }
} // namespace

int main()
{
    std::printf("FFT size %d, %d bins, %.1f Hz per bin at 48 kHz\n\n", SpectrumAnalyser::fftSize,
                SpectrumAnalyser::numBins, sampleRate / SpectrumAnalyser::fftSize);

    // ---- Not enough to look at yet -----------------------------------------
    std::printf("Before a full window:\n");
    {
        SpectrumAnalyser analyser;
        analyser.prepare(sampleRate);

        auto few = makeSine(1000.0, 1.0f, 100);
        check(!analyser.update(few.data(), (int) few.size()), "a short buffer is refused");
        check(analyser.getDbInRange(900.0, 1100.0) <= SpectrumAnalyser::floorDb,
              "and nothing is invented from it");
    }

    // ---- A tone lands where it is ------------------------------------------
    // The whole point. A spectrum that puts 1 kHz anywhere but 1 kHz is worse
    // than no spectrum, because it is confidently wrong.
    std::printf("\nWhere a tone lands:\n");
    {
        SpectrumAnalyser analyser;
        analyser.prepare(sampleRate);

        auto tone = makeSine(1000.0, 1.0f, SpectrumAnalyser::fftSize);
        check(analyser.update(tone.data(), (int) tone.size()), "a full window is analysed");

        const int peak = loudestBin(analyser);
        const double found = analyser.getBinFrequency(peak);

        std::printf("  loudest bin is %d, %.1f Hz\n", peak, found);
        check(std::abs(found - 1000.0) < 20.0, "a 1 kHz tone peaks within a bin of 1 kHz");
    }

    // ---- And reads its own level -------------------------------------------
    // Scaled so these decibels mean the same thing as the ones on the meter
    // beside it. Otherwise the two panes disagree about the same sound.
    std::printf("\nWhat it reads:\n");
    {
        SpectrumAnalyser analyser;
        analyser.prepare(sampleRate);

        auto full = makeSine(1000.0, 1.0f, SpectrumAnalyser::fftSize);
        analyser.update(full.data(), (int) full.size());

        const float db = analyser.getDbInRange(950.0, 1050.0);
        std::printf("  full-scale sine reads %.2f dB\n", db);
        check(std::abs(db) < 1.0f, "a full-scale sine reads 0 dBFS");

        SpectrumAnalyser quieter;
        quieter.prepare(sampleRate);

        auto half = makeSine(1000.0, 0.5f, SpectrumAnalyser::fftSize);
        quieter.update(half.data(), (int) half.size());

        const float halfDb = quieter.getDbInRange(950.0, 1050.0);
        std::printf("  half-scale sine reads %.2f dB\n", halfDb);
        check(std::abs(halfDb + 6.0f) < 1.0f, "and half of that reads 6 dB down");
    }

    // ---- Two tones stay apart ----------------------------------------------
    std::printf("\nTwo tones at once:\n");
    {
        SpectrumAnalyser analyser;
        analyser.prepare(sampleRate);

        auto low = makeSine(200.0, 0.5f, SpectrumAnalyser::fftSize);
        auto high = makeSine(5000.0, 0.5f, SpectrumAnalyser::fftSize);

        std::vector<float> both((size_t) SpectrumAnalyser::fftSize);
        for (size_t i = 0; i < both.size(); ++i)
            both[i] = low[i] + high[i];

        analyser.update(both.data(), (int) both.size());

        check(analyser.getDbInRange(180.0, 220.0) > -12.0f, "the low tone is there");
        check(analyser.getDbInRange(4800.0, 5200.0) > -12.0f, "so is the high one");

        // And the gap between them is empty, which is what says the transform
        // is resolving rather than smearing.
        check(analyser.getDbInRange(1000.0, 1500.0) < -40.0f, "with nothing between them");
    }

    // ---- Asking for a range narrower than a bin ----------------------------
    // The whole bottom of a log plot does this: at 25 Hz a pixel is a fraction
    // of one bin, and answering "nothing" there would draw an empty bass end.
    std::printf("\nA range narrower than a bin:\n");
    {
        SpectrumAnalyser analyser;
        analyser.prepare(sampleRate);

        auto tone = makeSine(100.0, 1.0f, SpectrumAnalyser::fftSize);
        analyser.update(tone.data(), (int) tone.size());

        check(analyser.getDbInRange(100.0, 100.1f) > -20.0f, "still answers with the bin it falls in");
        check(analyser.getDbInRange(200.0, 199.0) > SpectrumAnalyser::floorDb,
              "and a range given backwards is read the right way round");
    }

    // ---- Falling back down --------------------------------------------------
    std::printf("\nDecay:\n");
    {
        SpectrumAnalyser analyser;
        analyser.prepare(sampleRate);

        auto tone = makeSine(1000.0, 1.0f, SpectrumAnalyser::fftSize);
        analyser.update(tone.data(), (int) tone.size());
        const float loud = analyser.getDbInRange(950.0, 1050.0);

        std::vector<float> silence((size_t) SpectrumAnalyser::fftSize, 0.0f);
        analyser.update(silence.data(), (int) silence.size());
        const float afterOne = analyser.getDbInRange(950.0, 1050.0);

        check(afterOne < loud, "silence starts the curve falling");
        check(afterOne > loud - 10.0f, "but not all at once, or a note would vanish as it ended");

        for (int i = 0; i < 200; ++i)
            analyser.update(silence.data(), (int) silence.size());

        check(analyser.getDbInRange(950.0, 1050.0) <= SpectrumAnalyser::floorDb,
              "and it reaches the floor eventually");

        analyser.reset();
        check(analyser.getDbInRange(0.0, 20000.0) <= SpectrumAnalyser::floorDb,
              "reset clears it at once");
    }

    // ---- The EQ curve is the EQ --------------------------------------------
    // Drawn from the same coefficients the audio path builds, so this is
    // checking that the shared numbers are shared rather than merely similar.
    std::printf("\nThe EQ response:\n");
    {
        const float flat = MasterEffects::getEqualiserMagnitude(0.0f, 0.0f, 0.0f, 1000.0, sampleRate);
        check(std::abs(decibelsOf(flat)) < 0.05f, "a flat EQ does nothing at any frequency");

        // A 6 dB low shelf is 6 dB at the bottom and nothing at the top.
        const float lowEnd = MasterEffects::getEqualiserMagnitude(6.0f, 0.0f, 0.0f, 40.0, sampleRate);
        const float topEnd = MasterEffects::getEqualiserMagnitude(6.0f, 0.0f, 0.0f, 12000.0, sampleRate);

        std::printf("  low shelf +6: %.2f dB at 40 Hz, %.2f dB at 12 kHz\n", decibelsOf(lowEnd),
                    decibelsOf(topEnd));
        check(std::abs(decibelsOf(lowEnd) - 6.0f) < 1.0f, "a low shelf lifts the bottom by its gain");
        check(std::abs(decibelsOf(topEnd)) < 0.5f, "and leaves the top alone");

        // The bell peaks where it is centred, which is the one thing a drawn
        // curve must get right or every band will be set at the wrong place.
        const float atCentre =
            MasterEffects::getEqualiserMagnitude(0.0f, 6.0f, 0.0f, eq::midFrequency, sampleRate);
        const float wellBelow =
            MasterEffects::getEqualiserMagnitude(0.0f, 6.0f, 0.0f, 80.0, sampleRate);

        check(std::abs(decibelsOf(atCentre) - 6.0f) < 0.5f, "the bell peaks at its own frequency");
        check(decibelsOf(wellBelow) < 1.0f, "and is over well before the bottom of the range");

        // Cuts, not just boosts.
        const float cut = MasterEffects::getEqualiserMagnitude(0.0f, 0.0f, -9.0f, 15000.0, sampleRate);
        check(decibelsOf(cut) < -7.0f, "a high shelf cut takes the top down");

        // The three multiply, because they run one after the other.
        const float stacked =
            MasterEffects::getEqualiserMagnitude(0.0f, 6.0f, 6.0f, eq::midFrequency, sampleRate);
        check(decibelsOf(stacked) > decibelsOf(atCentre),
              "and a second band on top of the first adds to it");

        // Clamped the way the audio path clamps, so a curve cannot promise a
        // boost the filtering will not deliver.
        const float beyond =
            MasterEffects::getEqualiserMagnitude(40.0f, 0.0f, 0.0f, 40.0, sampleRate);
        check(std::abs(decibelsOf(beyond) - 12.0f) < 1.0f, "and asking for more than 12 dB gives 12");
    }

    std::printf("\n%s\n", failures == 0 ? "ALL SPECTRUM TESTS PASSED" : "SOME SPECTRUM TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
