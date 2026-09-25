// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "FxpPreset.h"
#include "WavetableOscillator.h"
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace wavelathe
{
// Closed-loop ("analysis-by-synthesis") preset matching: repeatedly render
// WaveLathe's own engine with candidate settings, measure how far each render
// is from the reference recording using a perceptual distance, and search for
// the settings that minimise it.
//
// This replaces hand-written feature->knob heuristics with actual measurement:
// rather than guessing that a given crest factor implies a given drive amount,
// it tries settings and keeps what genuinely sounds closest. It only ever
// analyses rendered audio the user supplies as a reference - it never reads or
// decodes another synth's proprietary preset format.
namespace SoundMatch
{
// A perceptual fingerprint of a signal: what we actually compare.
struct AudioFeatures
{
    // Log-magnitude mel spectrograms at two time/frequency resolutions.
    // Coarse resolution captures overall timbre, fine captures transients.
    std::vector<float> logMelCoarse;
    std::vector<float> logMelFine;
    int coarseFrames = 0, fineFrames = 0, numMelBands = 0;

    // Amplitude envelope in dB, resampled to a fixed length (ADSR shape).
    std::vector<float> envelopeDb;

    // Magnitude spectrum of the amplitude envelope (0.3-40Hz): captures
    // unison beating rate/depth and LFO movement, the "character" of a
    // reese-style patch rather than its static timbre.
    std::vector<float> modulationSpectrum;
};

AudioFeatures extractFeatures(const float* mono, int numSamples, double sampleRate);

// What the search is free to move. Exposed so the set can be reported and
// pinned by a test rather than only being visible in the source.
int numSearchedParameters();
const char* searchedParameterName(int index);
bool isParameterSearched(const juce::String& name);

// Where a preset's value for one searched parameter sits in its range, 0 to 1.
// Lets a caller see whether a result is pinned against an end.
float normalisedValueOf(const PresetValues& values, int index);

// Moves one searched parameter to a position in its range, leaving the rest
// alone. Exists so a caller can sweep a parameter by index and see how much
// the objective actually reacts to it - a parameter nothing can hear is a
// dimension the search has to cross for nothing.
void setNormalisedValueOf(PresetValues& values, int index, float normalised);

// Whether this parameter is sitting at its "not used" value - a mix at zero, a
// depth centred - which is a decision rather than a limit being hit.
bool isParameterAtNeutral(const PresetValues& values, int index);

// Puts settings that currently do nothing back to a sane value - a second
// oscillator's tuning while its level is zero, a reverb's size at zero mix.
// The search cannot hear those, so it leaves them wherever they landed, which
// is silent during matching and audible the moment the parent is turned up.
// Returns how many were moved. Cannot change how the patch sounds.
int tidyInactiveParameters(PresetValues& values);

// Which stage of the search a parameter belongs to: "voice", "modulation" or
// "effect". Exposed so the split can be checked rather than taken on trust -
// the flags are positional, and getting one wrong is silent.
const char* parameterStage(int index);

// Weighted distance between two fingerprints. Lower is more similar.
// Both signals are loudness-normalised first, so this compares timbre and
// movement rather than raw level.
float featureDistance(const AudioFeatures& a, const AudioFeatures& b);
// The distance split into the terms it is made of. Exposed because a single
// number cannot say WHY two sounds measure alike, and that is exactly the
// question when the search keeps confusing one kind of movement for another.
struct DistanceBreakdown
{
    float spectralCoarse = 0.0f;
    float spectralFine = 0.0f;
    float envelope = 0.0f;
    float modulation = 0.0f;
    float total = 0.0f;
};

DistanceBreakdown featureDistanceParts(const AudioFeatures& a, const AudioFeatures& b);


struct OptimizerSettings
{
    int populationSize = 80; // scaled to the number of searched parameters
    int generations = 60;
    int numThreads = 0; // 0 = auto-detect
    int randomSeed = 20260831;

    // Optional: set to true from another thread to stop early and return the
    // best result found so far (used so the UI can close without blocking).
    std::shared_ptr<std::atomic<bool>> cancelFlag;

    // Whether to play the shipped bank against the reference before searching.
    // What that buys is a handful of musically coherent patches in the starting
    // population - one per category, so they differ from each other - rather
    // than a population drawn independently at random, where a bell's short
    // attack and high cutoff and particular wavetable position never co-occur.
    // Off, the search runs from the caller's estimate alone, as it did before
    // the bank was consulted; kept so the two can be compared rather than the
    // improvement being taken on trust.
    bool auditionFactoryBank = true;
};

struct OptimizerResult
{
    PresetValues best;
    float startingDistance = 0.0f; // distance of the heuristic seed

    // The shipped preset that came closest to the reference before any
    // searching, and how far away it was. The search does not start from it -
    // measured over eight random seeds, starting there was no better than
    // ignoring the bank entirely - but it is worth reporting in its own right:
    // "your sample is nearest to Glass Bell" is useful to a person even when
    // the search then goes somewhere else.
    juce::String closestPresetName;
    float closestPresetDistance = 0.0f;

    float bestDistance = 0.0f;

    // How many settings the search could not hear were put back to a sane value
    // afterwards - see tidyInactiveParameters.
    int inactiveTidied = 0;

    // How many settings the search chose that turned out not to earn their
    // place - offered at their simpler value and left there because the fit did
    // not get worse. See simplifyUnearnedParameters.
    int unearnedSimplified = 0;
    int evaluations = 0;
};

// How close one shipped preset is to a reference note.
struct PresetAudition
{
    juce::String name;
    int bankIndex = -1; // index into FactoryPresets::all(), or -1 for the estimate
    float distance = 0.0f;
};

// Plays every factory preset at the reference's pitch and measures each one the
// same way a search candidate is measured, closest first.
//
// Forty-odd renders cost less than a single generation of the search, and they
// answer a question the search is otherwise bad at: which region of the
// parameter space is this sound even in? Differential evolution starts from
// independently random values, but real patches are nothing like that - a bell
// is a short attack AND a high cutoff AND no unison AND a particular wavetable
// position, all at once, and a random draw satisfies one of those at a time.
// The bank is a set of points that are already musically coherent.
//
// If optionalEstimate is given it competes on the same terms rather than being
// assumed best, and is reported with a bankIndex of -1.
std::vector<PresetAudition> auditionFactoryPresets(const WavetableSet& tableSet, const float* referenceMono,
                                                   int referenceNumSamples, double sampleRate,
                                                   int midiNoteNumber,
                                                   const PresetValues* optionalEstimate = nullptr,
                                                   float holdRatio = 0.85f, int numThreads = 0);

// Optimises a preset to match one reference note segment.
// referenceMono/referenceNumSamples: the isolated reference note.
// midiNoteNumber: the note to render candidates at (matched to the reference).
// seed: heuristic starting estimate, included in the initial population.
// progress: optional, called with (generation, totalGenerations, bestDistance).
// Scores a single preset against a reference note (lower = closer match).
// Useful for verifying an optimizer result and for sweeping one parameter to
// see whether an extreme value is genuinely better or only marginally so.
float scorePreset(const WavetableSet& tableSet, const float* referenceMono, int referenceNumSamples,
                  double sampleRate, int midiNoteNumber, const PresetValues& values, float holdRatio = 0.85f);

// One note of the reference: the audio, the pitch it was played at, and how
// much of that window the note is held for.
struct ReferenceNote
{
    const float* mono = nullptr;
    int numSamples = 0;
    int midiNoteNumber = 60;

    // The fraction of the window during which the note is held; the rest is its
    // release tail. A window that runs past the end of the note is what makes
    // release observable - measured against the note alone, any release longer
    // than the window looks the same as "still sustaining", so it drifts to
    // absurd lengths and the note rings forever when actually played.
    float holdRatio = 0.85f;
};

// Optimises one set of settings to match every reference note at once.
//
// Fitting a single note is what lets the search cheat. A filter in the wrong
// place, an equaliser bent to cancel it - these can be made to measure well on
// one pitch and be wrong everywhere else, and no penalty on the settings
// themselves reliably prevents it. Given several pitches, the same cutoff has
// to suit all of them, and it cannot: a filter cuts the harmonic series in a
// different place at each fundamental. The escape hatch closes because of how
// the problem is posed rather than because it is being penalised.
//
// Every note is rendered for each candidate, so K notes cost K times as much
// per evaluation. Three is usually enough to pin the settings down.
//
// seed: heuristic starting estimate, included in the initial population.
// progress: optional, called with (generation, totalGenerations, bestDistance).
OptimizerResult optimize(const WavetableSet& tableSet, const std::vector<ReferenceNote>& notes,
                         double sampleRate, const PresetValues& seed,
                         const OptimizerSettings& settings = {},
                         std::function<void(int, int, float)> progress = {});

// The single-note case, which is just the above with one note in the list.
OptimizerResult optimize(const WavetableSet& tableSet, const float* referenceMono, int referenceNumSamples,
                         double sampleRate, int midiNoteNumber, const PresetValues& seed,
                         const OptimizerSettings& settings = {},
                         std::function<void(int, int, float)> progress = {}, float holdRatio = 0.85f);
} // namespace SoundMatch
} // namespace wavelathe
