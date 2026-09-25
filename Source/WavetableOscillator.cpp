// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "WavetableOscillator.h"
#include <cmath>

namespace wavelathe
{
int mipLevelForFrequency(float frequencyHz)
{
    if (frequencyHz <= (float) mipBaseHz)
        return 0;
    int level = (int) std::floor(std::log2((double) frequencyHz / mipBaseHz));
    return juce::jlimit(0, numMipLevels - 1, level);
}

int maxHarmonicForMipLevel(int level)
{
    // Harmonics must stay below Nyquist at the top of the octave this level covers.
    double topOfBandHz = mipBaseHz * std::pow(2.0, (double) level + 1.0);
    return juce::jmax(1, (int) (mipReferenceNyquist / topOfBandHz));
}

namespace
{
float interpolateAt(const float* data, int numSamples, double position)
{
    if (position < 0.0 || position >= (double) (numSamples - 1))
        return 0.0f;

    int i0 = (int) position;
    int i1 = i0 + 1;
    float frac = (float) (position - i0);
    return data[i0] + frac * (data[i1] - data[i0]);
}
} // namespace

void WavetableSet::fillAdditive(std::array<float, tableSize + 1>& table,
                                 const std::vector<float>& harmonicAmplitudes)
{
    for (int i = 0; i < tableSize; ++i)
    {
        double phase = (double) i / (double) tableSize;
        double sample = 0.0;

        for (size_t h = 0; h < harmonicAmplitudes.size(); ++h)
        {
            auto amplitude = harmonicAmplitudes[h];
            if (amplitude == 0.0f)
                continue;

            auto harmonicNumber = (double) (h + 1);
            sample += (double) amplitude * std::sin(juce::MathConstants<double>::twoPi * harmonicNumber * phase);
        }

        table[(size_t) i] = (float) sample;
    }

    float peak = 0.0f;
    for (int i = 0; i < tableSize; ++i)
        peak = juce::jmax(peak, std::abs(table[(size_t) i]));

    if (peak > 0.0f)
        for (int i = 0; i < tableSize; ++i)
            table[(size_t) i] /= peak;

    table[tableSize] = table[0];
}

// Removes DC, normalises to unity peak, and closes the loop so interpolation
// never has to wrap mid-read.
void WavetableSet::normaliseTable(std::array<float, tableSize + 1>& table)
{
    double mean = 0.0;
    for (int i = 0; i < tableSize; ++i) mean += table[(size_t) i];
    mean /= tableSize;
    for (int i = 0; i < tableSize; ++i) table[(size_t) i] -= (float) mean;

    float peak = 0.0f;
    for (int i = 0; i < tableSize; ++i)
        peak = juce::jmax(peak, std::abs(table[(size_t) i]));

    if (peak > 0.0f)
        for (int i = 0; i < tableSize; ++i)
            table[(size_t) i] /= peak;

    table[tableSize] = table[0];
}

// Produces a copy of `source` with harmonics above maxHarmonic removed.
// Deliberately does NOT renormalise: mip levels must stay consistent in level
// with each other, or the sound would jump in volume as notes cross an octave
// boundary and switch levels. The output is instead rescaled to the amplitude
// implied by the harmonics it kept (Parseval), which also makes this immune to
// whatever scaling convention the FFT's inverse transform uses.
void WavetableSet::bandLimitTable(const std::array<float, tableSize + 1>& source,
                                   std::array<float, tableSize + 1>& destination, int maxHarmonic)
{
    if (maxHarmonic >= tableSize / 2)
    {
        destination = source;
        return;
    }

    constexpr int order = 11; // 2048
    static_assert((1 << order) == tableSize, "FFT order must match tableSize");

    juce::dsp::FFT fft(order);
    std::vector<float> work((size_t) tableSize * 2, 0.0f);
    std::copy(source.begin(), source.begin() + tableSize, work.begin());

    double sourceSumSquares = 0.0;
    for (int i = 0; i < tableSize; ++i)
        sourceSumSquares += (double) source[(size_t) i] * source[(size_t) i];

    fft.performRealOnlyForwardTransform(work.data());

    double totalEnergy = 0.0, retainedEnergy = 0.0;
    for (int k = 0; k < tableSize; ++k)
    {
        double re = work[(size_t) (2 * k)];
        double im = work[(size_t) (2 * k) + 1];
        double energy = re * re + im * im;
        totalEnergy += energy;

        int harmonic = juce::jmin(k, tableSize - k);
        if (harmonic <= maxHarmonic)
            retainedEnergy += energy;
        else
        {
            work[(size_t) (2 * k)] = 0.0f;
            work[(size_t) (2 * k) + 1] = 0.0f;
        }
    }

    fft.performRealOnlyInverseTransform(work.data());

    for (int i = 0; i < tableSize; ++i)
        destination[(size_t) i] = work[(size_t) i];

    double outputSumSquares = 0.0;
    for (int i = 0; i < tableSize; ++i)
        outputSumSquares += (double) destination[(size_t) i] * destination[(size_t) i];

    if (outputSumSquares > 1.0e-20 && totalEnergy > 0.0)
    {
        double targetSumSquares = sourceSumSquares * (retainedEnergy / totalEnergy);
        float scale = (float) std::sqrt(targetSumSquares / outputSumSquares);
        for (int i = 0; i < tableSize; ++i)
            destination[(size_t) i] *= scale;
    }

    destination[tableSize] = destination[0];
}

void WavetableSet::generateMipLevels()
{
    for (int frame = 0; frame < numTables; ++frame)
        for (int level = 0; level < numMipLevels; ++level)
            bandLimitTable(sourceTables[(size_t) frame], mipTables[(size_t) frame][(size_t) level],
                            maxHarmonicForMipLevel(level));
}

WavetableSet::WavetableSet()
{
    constexpr int harmonicCount = 40;

    // Five archetype spectra; the frames interpolate between them so sweeping
    // the Wave dial travels smoothly sine -> triangle -> saw -> square ->
    // bright rather than crossfading between five fixed shapes.
    std::array<std::vector<float>, 5> archetypes;
    for (auto& a : archetypes)
        a.assign(harmonicCount, 0.0f);

    archetypes[0][0] = 1.0f; // sine

    for (int n = 1; n <= harmonicCount; n += 2) // triangle
    {
        float sign = ((n - 1) / 2) % 2 == 0 ? 1.0f : -1.0f;
        archetypes[1][(size_t) n - 1] = sign / (float) (n * n);
    }

    for (int n = 1; n <= harmonicCount; ++n) // sawtooth
        archetypes[2][(size_t) n - 1] = 1.0f / (float) n;

    for (int n = 1; n <= harmonicCount; n += 2) // square
        archetypes[3][(size_t) n - 1] = 1.0f / (float) n;

    for (int n = 1; n <= harmonicCount; ++n) // bright, with a formant-like bump
    {
        float base = 1.0f / (float) n;
        float bump = std::exp(-std::pow((float) n - 7.0f, 2.0f) / 8.0f) * 0.8f;
        archetypes[4][(size_t) n - 1] = base + bump;
    }

    for (int frame = 0; frame < numTables; ++frame)
    {
        float position = numTables > 1 ? (float) frame / (float) (numTables - 1) : 0.0f;
        float scaled = position * (float) (archetypes.size() - 1);
        int lower = juce::jlimit(0, (int) archetypes.size() - 1, (int) scaled);
        int upper = juce::jmin(lower + 1, (int) archetypes.size() - 1);
        float blend = scaled - (float) lower;

        std::vector<float> harmonics((size_t) harmonicCount, 0.0f);
        for (int n = 0; n < harmonicCount; ++n)
            harmonics[(size_t) n] = archetypes[(size_t) lower][(size_t) n]
                                    + blend
                                          * (archetypes[(size_t) upper][(size_t) n]
                                             - archetypes[(size_t) lower][(size_t) n]);

        fillAdditive(sourceTables[(size_t) frame], harmonics);
    }

    custom = false;
    sourceName = "Built-in";
    generateMipLevels();
}

bool WavetableSet::loadFromAudio(const float* data, int numSamples, double sampleRate, float fundamentalHz,
                                  juce::String& errorMessage)
{
    if (fundamentalHz <= 0.0f)
    {
        errorMessage = "No clear pitch was detected, so a single cycle can't be located.";
        return false;
    }

    double period = sampleRate / (double) fundamentalHz;
    if (period < 8.0)
    {
        errorMessage = "Source pitch is too high to sample a usable cycle.";
        return false;
    }

    // Average several consecutive cycles per frame: this suppresses noise and
    // largely averages out unison beating, leaving the core timbre - which is
    // what we want, since the synth re-applies its own unison on top.
    int cyclesPerFrame = 4;
    double frameSpan = period * cyclesPerFrame;

    if ((double) numSamples < frameSpan + period)
    {
        cyclesPerFrame = 1;
        frameSpan = period;
    }

    if ((double) numSamples < frameSpan + 2.0)
    {
        errorMessage = "Audio segment is too short to sample a cycle from.";
        return false;
    }

    double usableSpan = juce::jmax(0.0, (double) numSamples - frameSpan - 2.0);

    for (int frame = 0; frame < numTables; ++frame)
    {
        double frameStart = numTables > 1 ? usableSpan * frame / (numTables - 1) : 0.0;

        std::array<double, tableSize> accumulator{};
        accumulator.fill(0.0);

        for (int cycle = 0; cycle < cyclesPerFrame; ++cycle)
        {
            double cycleStart = frameStart + cycle * period;
            for (int i = 0; i < tableSize; ++i)
            {
                double source = cycleStart + (double) i / tableSize * period;
                accumulator[(size_t) i] += interpolateAt(data, numSamples, source);
            }
        }

        for (int i = 0; i < tableSize; ++i)
            sourceTables[(size_t) frame][(size_t) i] = (float) (accumulator[(size_t) i] / cyclesPerFrame);

        normaliseTable(sourceTables[(size_t) frame]);

        // A frame that came out silent (e.g. sampled from a gap) would create
        // a dead spot in the morph, so fall back to the previous frame.
        float peak = 0.0f;
        for (int i = 0; i < tableSize; ++i)
            peak = juce::jmax(peak, std::abs(sourceTables[(size_t) frame][(size_t) i]));

        if (peak < 1.0e-4f && frame > 0)
            sourceTables[(size_t) frame] = sourceTables[(size_t) frame - 1];
    }

    generateMipLevels();
    custom = true;
    return true;
}

std::vector<float> WavetableSet::getRawTables() const
{
    // Only the full-bandwidth source frames are stored; mip levels are
    // regenerated on load, so presets stay small.
    std::vector<float> raw((size_t) numTables * tableSize);
    for (int t = 0; t < numTables; ++t)
        for (int i = 0; i < tableSize; ++i)
            raw[(size_t) t * tableSize + i] = sourceTables[(size_t) t][(size_t) i];
    return raw;
}

bool WavetableSet::loadFromRawTables(const std::vector<float>& rawData, juce::String& errorMessage)
{
    if (rawData.size() != (size_t) numTables * tableSize)
    {
        errorMessage = "Stored wavetable is the wrong size for this version of WaveLathe.";
        return false;
    }

    for (int t = 0; t < numTables; ++t)
    {
        for (int i = 0; i < tableSize; ++i)
            sourceTables[(size_t) t][(size_t) i] = rawData[(size_t) t * tableSize + i];
        sourceTables[(size_t) t][tableSize] = sourceTables[(size_t) t][0];
    }

    generateMipLevels();
    custom = true;
    return true;
}

WavetableOscillator::WavetableOscillator(const WavetableSet* tablesToUse) : tableSet(tablesToUse) {}

void WavetableOscillator::setFrequency(float frequencyHz)
{
    phaseIncrement = (float) (frequencyHz / sampleRate);
    mipLevel = mipLevelForFrequency(frequencyHz);
}

float WavetableOscillator::readTableLinear(const std::array<float, tableSize + 1>& table, float tablePhase)
{
    float indexFloat = tablePhase * (float) tableSize;
    int index0 = (int) indexFloat;
    int index1 = index0 + 1;
    float frac = indexFloat - (float) index0;

    return table[(size_t) index0] + frac * (table[(size_t) index1] - table[(size_t) index0]);
}

float WavetableOscillator::getNextSample()
{
    if (tableSet == nullptr)
        return 0.0f;

    float scaledPosition = wavePosition * (float) (numTables - 1);
    int tableIndexLow = (int) scaledPosition;
    int tableIndexHigh = juce::jmin(tableIndexLow + 1, numTables - 1);
    float morphFrac = scaledPosition - (float) tableIndexLow;

    float sampleLow = readTableLinear(tableSet->getTable(tableIndexLow, mipLevel), phase);
    float sampleHigh = readTableLinear(tableSet->getTable(tableIndexHigh, mipLevel), phase);
    float output = sampleLow + morphFrac * (sampleHigh - sampleLow);

    phase += phaseIncrement;
    if (phase >= 1.0f)
        phase -= 1.0f;

    return output;
}
} // namespace wavelathe
