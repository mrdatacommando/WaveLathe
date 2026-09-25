// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

namespace wavelathe
{
constexpr int tableSize = 2048;

// Frames per wavetable. The Wave dial morphs across these, and with a
// modulator routed to it the sound sweeps through them during a note - which
// is the defining gesture of wavetable synthesis. Five frames was too coarse
// for that to sound like anything but a crossfade; 32 gives a smooth sweep
// while keeping a set around 2.9 MB and an embedded preset around 262 KB.
constexpr int numTables = 32;

// Each frame is stored at several band-limited "mip" levels, one per octave of
// playback pitch. Playing a table far above the pitch it was built for would
// otherwise fold its upper harmonics back down as aliasing; picking a level
// whose harmonics still fit under Nyquist keeps high notes clean.
constexpr int numMipLevels = 10;      // covers 20 Hz up to ~20 kHz
constexpr double mipBaseHz = 20.0;    // level 0 covers 20-40 Hz
constexpr double mipReferenceNyquist = 22050.0; // built for 44.1 kHz; higher rates are simply safer

int mipLevelForFrequency(float frequencyHz);
int maxHarmonicForMipLevel(int level);

// A set of single-cycle wavetables. Either the built-in synthetic set, or a
// set of cycles sampled from an audio recording the user supplies.
class WavetableSet
{
public:
    WavetableSet(); // built-in synthetic tables

    // Band-limited table for a frame at a given playback mip level.
    const std::array<float, tableSize + 1>& getTable(int frame, int mipLevel) const
    {
        return mipTables[(size_t) frame][(size_t) mipLevel];
    }

    // Extracts numTables single cycles spread across the supplied audio, so
    // morphing Wave Position sweeps through how the source timbre evolves
    // over the note. Each frame averages several consecutive cycles to
    // suppress noise, and is resampled to the table size, DC-removed,
    // normalised and band-limited.
    bool loadFromAudio(const float* data, int numSamples, double sampleRate, float fundamentalHz,
                       juce::String& errorMessage);

    bool isCustom() const { return custom; }
    juce::String getSourceName() const { return sourceName; }
    void setSourceName(const juce::String& name) { sourceName = name; }

    // Raw table data (numTables * tableSize floats), so a sampled wavetable
    // can be stored inside a preset and travel with it.
    std::vector<float> getRawTables() const;
    bool loadFromRawTables(const std::vector<float>& rawData, juce::String& errorMessage);

private:
    // Full-bandwidth frames as sampled/synthesised; the mip levels are derived
    // from these, and these are what a preset stores.
    std::array<std::array<float, tableSize + 1>, numTables> sourceTables;
    std::array<std::array<std::array<float, tableSize + 1>, numMipLevels>, numTables> mipTables;
    bool custom = false;
    juce::String sourceName;

    static void fillAdditive(std::array<float, tableSize + 1>& table,
                              const std::vector<float>& harmonicAmplitudes);
    static void normaliseTable(std::array<float, tableSize + 1>& table);
    static void bandLimitTable(const std::array<float, tableSize + 1>& source,
                                std::array<float, tableSize + 1>& destination, int maxHarmonic);
    void generateMipLevels();
};

// Lets the audio thread pick up a newly loaded wavetable set without locking.
// The pointed-to set must outlive every voice that might still be reading it.
struct WavetableProvider
{
    std::atomic<const WavetableSet*> active{nullptr};
};

// A single wavetable oscillator voice component.
// wavePosition morphs across the table set: 0 = table 0, 1 = last table,
// with linear crossfade between the two nearest tables.
class WavetableOscillator
{
public:
    explicit WavetableOscillator(const WavetableSet* tablesToUse = nullptr);

    void setTableSet(const WavetableSet* newTables) { tableSet = newTables; }
    void setSampleRate(double newSampleRate) { sampleRate = newSampleRate; }
    void setFrequency(float frequencyHz);
    void setWavePosition(float newPosition01) { wavePosition = juce::jlimit(0.0f, 1.0f, newPosition01); }
    void resetPhase(float startPhase01 = 0.0f) { phase = startPhase01; }

    float getNextSample();

private:
    const WavetableSet* tableSet = nullptr;
    double sampleRate = 44100.0;
    float phase = 0.0f;
    float phaseIncrement = 0.0f;
    float wavePosition = 0.0f;
    int mipLevel = 0;

    static float readTableLinear(const std::array<float, tableSize + 1>& table, float tablePhase);
};
} // namespace wavelathe
