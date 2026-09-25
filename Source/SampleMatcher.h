// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "FxpPreset.h"
#include <vector>

// Shared acoustic-analysis engine used by both the WaveLathe app's "Match
// Sample" feature and the standalone WavAnalyzer command-line tool. Measures
// pitch, spectral brightness, amplitude "beating" (the audible signature of
// detuned unison voices), and envelope shape from a rendered WAV recording,
// then maps those onto WaveLathe's own synth parameters as a starting-point
// approximation. This only ever looks at rendered audio a user supplies as a
// reference - it never reads or decodes another synth's proprietary preset
// or project format.
namespace wavelathe
{
namespace SampleMatch
{
struct SpectralStats
{
    float centroidHz = 0.0f;
    float rolloff85Hz = 0.0f;
};

struct BeatStats
{
    float beatHz = 0.0f;
    float depthPercent = 0.0f;
    bool reliable = false;
};

struct NoteSegment
{
    int startSample = 0;
    int endSample = 0;
};

struct SegmentReport
{
    double startTimeSec = 0.0;
    double durationSec = 0.0;
    float fundamentalHz = 0.0f;
    float crestFactorDb = 10.0f;

    // 0-100 periodicity of the segment. A single sustained note repeats
    // cleanly and scores high; a chord, two overlapping notes (including a
    // note ringing over the previous note's reverb tail), or noise scores low,
    // because the waveform does not repeat well at any single period.
    // Detection is monophonic, so a low score means "don't trust this pitch"
    // rather than "here are the notes in the chord".
    float harmonicFitPercent = 0.0f;

    SpectralStats spectral;
    BeatStats beat;

    // Clean enough to sample a wavetable from or match against.
    bool isCleanSingleNote() const { return harmonicFitPercent >= 70.0f; }
    // Not reliably a single pitch; anything derived from it is suspect.
    bool looksPolyphonic() const { return harmonicFitPercent < 50.0f; }

    juce::String clarityVerdict() const
    {
        if (isCleanSingleNote())
            return "clean single note";
        if (!looksPolyphonic())
            return "usable, slightly mixed";
        return "chord/overlap - pitch unreliable";
    }
};

struct EnvelopeTiming
{
    float attackSec = 0.01f;
    float sustainLevelRatio = 0.8f;
    float releaseSec = 0.2f;
};

juce::AudioBuffer<float> loadMono(const juce::File& file, double& sampleRateOut);
juce::String noteName(float hz);

// Nearest MIDI note number for a measured frequency (0 if not pitched).
int midiNoteForFrequency(float hz);

// Everything one analysis pass produced, so callers (e.g. the closed-loop
// optimizer) can work with the isolated reference note directly.
struct MatchContext
{
    juce::AudioBuffer<float> mono;
    double sampleRate = 44100.0;
    std::vector<NoteSegment> segments;
    std::vector<SegmentReport> reports;
    int chosenIndex = 0;
};

// Loads and segments a file, picking one note (noteIndex 0-based, or -1 to
// auto-pick the longest). Does not itself estimate parameters.
bool analyzeFile(const juce::File& wavFile, int noteIndex, MatchContext& outContext, juce::String& errorMessage);

// Writes an A/B comparison WAV: the isolated reference note, a short gap, then
// the synth rendered with the matched settings at the same pitch and length.
// Both are loudness-matched so the comparison is about timbre, not level.
// This is how you judge a match by ear rather than by a distance number.
// Widens a note segment to include what follows it, so the note's decay tail
// is part of the comparison. Matching against the note alone leaves release
// unobservable - anything longer than the window looks the same as holding -
// so it drifts long and the patch then rings on when played.
// Returns the window length; outHoldRatio is where the note itself ends.
int windowWithTail(const MatchContext& context, int segmentIndex, double tailSeconds, float& outHoldRatio);

bool writeAbComparison(const juce::File& outputFile, const float* reference, int referenceNumSamples,
                       const float* rendered, int renderedNumSamples, double sampleRate,
                       juce::String& errorMessage);

std::vector<NoteSegment> findNoteSegments(const float* data, int numSamples, double sampleRate);
SegmentReport analyzeSegment(const float* fullData, const NoteSegment& seg, double sampleRate);
EnvelopeTiming analyzeEnvelopeTiming(const float* fullData, const NoteSegment& seg, double sampleRate);
PresetValues estimatePreset(const SegmentReport& report, const EnvelopeTiming& timing, const juce::String& name);

// End-to-end: load, segment into notes, and estimate a preset from one of
// them. noteIndex is 0-based; pass -1 to auto-pick the longest segment.
// outAllReports, if non-null, receives every detected note's report (for
// diagnostics/UI display), not just the chosen one.
bool matchFile(const juce::File& wavFile, const juce::String& presetName, int noteIndex, PresetValues& outValues,
               juce::String& errorMessage, std::vector<SegmentReport>* outAllReports = nullptr);
} // namespace SampleMatch
} // namespace wavelathe
