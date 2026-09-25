// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

// Shared scaffolding for the two match suites.
//
// There are two because the one there used to be had grown to twenty minutes,
// which nobody decided on - it accumulated a probe at a time. A twenty-minute
// suite quietly changes how the work is done: it makes every experiment cost a
// coffee break, so ideas get reasoned about instead of measured, and four
// failed attempts at one problem cost an afternoon. The split is:
//
//   MatchTest   - the guards. Things that must keep being true, cheap enough
//                 to run after every change.
//   MatchProbe  - the measurements. Sweeps and multi-seed comparisons that
//                 answer questions rather than guard behaviour, run on purpose
//                 when investigating something.
//
// Both share the target patches below, so a probe and a guard are always
// talking about the same sound.

#include "PresetOptimizer.h"
#include "SampleMatcher.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "OfflineRenderer.h"
#include "WavetableOscillator.h"
#include "FactoryPresets.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace wavelathe;

namespace
{
int failures = 0;

void check(bool condition, const juce::String& what)
{
    if (!condition)
    {
        std::cout << "  FAIL: " << what << std::endl;
        ++failures;
    }
}

constexpr double sampleRate = 44100.0;
constexpr int midiNote = 60;
constexpr double duration = 1.6;
constexpr float holdRatio = 0.8f;

// The patch the search is being asked to find its way back to: a moving,
// tonally-shaped sound of the kind a real reference recording usually is.
PresetValues targetPatch()
{
    PresetValues p;
    p.name = "Target";
    p.wavePosition = 0.55f;
    p.osc1Level = 1.0f;
    p.attack = 0.02f;
    p.decay = 0.4f;
    p.sustain = 0.7f;
    p.release = 0.3f;
    p.filterCutoffHz = 2600.0f;
    p.filterResonance = 0.35f;
    p.unisonVoices = 3.0f;
    p.unisonDetuneCents = 14.0f;

    // The part the engine could not produce until now.
    p.chorusRate = 1.4f;
    p.chorusDepth = 0.7f;
    p.chorusMix = 0.75f;
    p.eqLowGain = -7.0f;
    p.eqHighGain = 8.0f;

    return p;
}

// A deliberately different sound to check the staging against: a short, plain
// pluck with a filter envelope, no unison, and no chorus or phaser whatsoever.
// Everything above is measured on one patch, and a staging tuned until that one
// patch comes out well is a staging fitted to a test rather than to the job.
// The sharpest question a second target can ask is the one this one asks: now
// that the chorus is easier to find, does the search start hearing it in sounds
// that do not have it?
PresetValues pluckTarget()
{
    PresetValues p;
    p.name = "Pluck";
    p.wavePosition = 0.30f;
    p.osc1Level = 1.0f;
    p.attack = 0.002f;
    p.decay = 0.25f;
    p.sustain = 0.05f;
    p.release = 0.20f;
    p.filterCutoffHz = 1200.0f;
    p.filterResonance = 0.50f;
    p.modEnvAttack = 0.001f;
    p.modEnvDecay = 0.18f;
    p.modEnvSustain = 0.0f;
    p.modEnvToCutoff = 0.70f;
    p.unisonVoices = 1.0f;
    p.unisonDetuneCents = 0.0f;
    p.reverbMix = 0.25f;
    p.reverbSize = 0.40f;
    return p;
}

// Which sections to run. With no arguments a suite runs the lot; with
// arguments it runs only the sections whose names contain one of them, so a
// single slow probe can be re-run on its own instead of sitting through the
// fifteen minutes around it.
std::vector<juce::String> requested;

void collectArguments(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
        requested.push_back(juce::String(argv[i]).trim());
}

bool wanted(const juce::String& sectionName)
{
    if (requested.empty())
        return true;

    for (const auto& r : requested)
        if (sectionName.containsIgnoreCase(r))
            return true;

    return false;
}

// How long each part of a suite takes. Printed as a table at the end, so the
// next thing worth splitting off is visible rather than guessed at - this is
// the measurement that was missing while the old suite was growing.
struct Timing
{
    juce::String name;
    double seconds;
};

std::vector<Timing> timings;

struct Section
{
    explicit Section(juce::String sectionName)
        : name(std::move(sectionName)), started(juce::Time::getMillisecondCounterHiRes())
    {
    }

    ~Section()
    {
        timings.push_back({name, (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0});
    }

    juce::String name;
    double started;
};

int finish(const juce::String& suiteName)
{
    double total = 0.0;
    for (const auto& t : timings)
        total += t.seconds;

    std::cout << std::endl << "Where the time went:" << std::endl;
    for (const auto& t : timings)
        std::cout << "  " << t.name.paddedRight(' ', 26)
                  << juce::String(t.seconds, 1).paddedLeft(' ', 8) << "s"
                  << juce::String(100.0 * t.seconds / juce::jmax(0.001, total), 1).paddedLeft(' ', 7)
                  << "%" << std::endl;

    std::cout << "  " << juce::String("total").paddedRight(' ', 26)
              << juce::String(total, 1).paddedLeft(' ', 8) << "s" << std::endl;

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL " << suiteName.toUpperCase() << " TESTS PASSED" << std::endl;
    else
        std::cout << failures << " " << suiteName.toUpperCase() << " TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
} // namespace
