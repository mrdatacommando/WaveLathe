// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SynthParameters.h"
#include "Scales.h"

namespace wavelathe
{
// Sits between the keyboard and the arpeggiator. Every note played is pulled
// into the chosen key, and optionally opened out into the chord that belongs on
// that degree - so a single finger produces something that is in key, and the
// arpeggiator downstream then has real chord tones to work with.
//
// The mapping from each played key to the notes it started is remembered, so
// releasing a key always releases exactly what it began even if the key, scale
// or chord mode was changed while it was held.
class HarmonyProcessor
{
public:
    void reset();

    // Rewrites the buffer in place.
    void process(juce::MidiBuffer& midi, const SynthParameters& params);

private:
    static constexpr int maxChordNotes = 5;

    struct SoundingChord
    {
        int count = 0;
        int notes[maxChordNotes] = {};
    };

    void releaseChord(juce::MidiBuffer& destination, int playedNote, int samplePosition);

    SoundingChord sounding[128];
};
} // namespace wavelathe
