// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SynthParameters.h"
#include "TempoSync.h"
#include <vector>

namespace wavelathe
{
// Turns whatever notes are held into a repeating pattern. It sits between the
// keyboard and the synth: incoming note on/offs update the held set, and when
// it is enabled the notes actually sent to the voices are generated on its own
// clock instead of passed straight through.
class Arpeggiator
{
public:
    // The pattern vocabulary common to hardware and software arpeggiators:
    // the plain directions, repeat variants, the shape patterns popularised by
    // Ableton's arpeggiator, the two pedal-note patterns, order-of-play, and
    // the family of randomisers. Grouped so the picker reads sensibly.
    enum class Mode
    {
        // Direction
        up = 0,
        down,
        upDown,        // turns without repeating the end notes
        downUp,
        upAndDown,     // turns and repeats the end notes
        downAndUp,

        // Repeats and runs
        upTwice,
        downTwice,
        upDownTwice,
        cascade,       // rolling three-note runs: 1-2-3, 2-3-4, 3-4-5...

        // Shapes
        converge,      // outside in
        diverge,       // inside out
        convergeDiverge,

        // Pedal notes
        pinkyUp,       // every other step is the top note
        pinkyUpDown,
        thumbUp,       // every other step is the bottom note
        thumbUpDown,

        // Order of arrival
        playOrder,
        reversePlayOrder,
        chord,         // all held notes together, on the step clock

        // Random
        randomOnce,    // one random order, held until the chord changes
        random,
        randomOther,   // random, but every note before any repeats
        randomWalk,    // drifts one step at a time

        numModes
    };

    static constexpr int numModes = (int) Mode::numModes;

    // Short label for the picker, and a one-line description for the display.
    static const char* modeName(int mode);
    static const char* modeDescription(int mode);

    // The rhythm the mode is played in. Hardware arpeggiators keep this on its
    // own axis - the Virus calls it Pattern, the Blofeld gives every step a
    // type - because the musical character comes from the rests, ties, accents
    // and octave jumps rather than from the note order. Each pattern is written
    // as one character per step:
    //
    //   x  play the next note        X  play it accented      o  play it softly
    //   .  rest                      -  tie the note through this step
    //   ^  next note, octave up      v  next note, octave down
    //   =  play the previous note again (a ratchet)
    //   *  play the whole chord      ?  play any note at random
    static int numPatterns();
    static const char* patternName(int pattern);
    static const char* patternDescription(int pattern);
    static const char* patternSteps(int pattern);

    void prepare(double sampleRate);
    void reset();

    // Rewrites the buffer in place: held notes are tracked from it, and if the
    // arpeggiator is on, its own pattern replaces them.
    // The clock is the shared musical timeline; the arpeggiator reads its step
    // positions off it rather than counting its own, so it lands on the same
    // instants as anything else running at a linked rate.
    void process(juce::MidiBuffer& midi, int numSamples, const SynthParameters& params,
                 const tempo::MusicalClock& clock, double bpm);

    // The step length the arpeggiator is actually running at, in beats, given
    // the rate, the sync setting and what it is linked to.
    static double stepBeatsFor(const SynthParameters& params, double bpm);

private:
    void stopSoundingNotes(juce::MidiBuffer& midi, int samplePosition);
    void rebuildPattern(Mode mode, int octaveRange);
    void buildPool(Mode mode, int octaveRange);
    void buildSequence(Mode mode);
    int nextPoolIndex(Mode mode);

    // How many steps the note started on this step should be held for, given
    // the ties that follow it in the rhythm.
    int tiedStepsAfter(const char* steps, int length, int fromStep) const;

    double sampleRate = 44100.0;

    std::vector<int> heldNotes;      // sorted, low to high
    std::vector<int> arrivalOrder;   // the same notes, in the order they were pressed

    // The notes the pattern walks over (held notes spread across the octave
    // range), and the order it walks them in. Both are rebuilt only when the
    // chord, mode or octave range actually changes.
    std::vector<int> pool;
    std::vector<int> sequence;       // indices into pool
    std::vector<int> shuffleBag;     // remaining indices, for "random other"
    std::vector<int> soundingNotes;  // what the arpeggiator currently has down

    bool patternDirty = true;
    int builtMode = -1;
    int builtOctaves = -1;

    int stepIndex = 0;       // position in the note order
    int patternStep = 0;     // position in the rhythm
    int lastPoolIndex = 0;
    int lastNotePlayed = -1; // what a ratchet step repeats
    // The next step due on the shared grid, counted from the clock's zero
    // rather than from whenever the arpeggiator was switched on.
    long long nextStep = 0;
    bool needsResync = true;
    int samplesUntilNoteOff = 0;
    bool wasEnabled = false;
    juce::Random random;
};
} // namespace wavelathe
