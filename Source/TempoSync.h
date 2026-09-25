// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SynthParameters.h"
#include <optional>
#include <array>
#include <cstdint>

namespace wavelathe
{
namespace tempo
{
// Musical divisions, in quarter-note beats, slowest first.
struct Division
{
    double beats;
    const char* name;
};

const Division* getDivisions();
int getNumDivisions();

// How long one step of a division lasts, in quarter-note beats. This is the
// unit everything that follows the tempo is expressed in, so two features set
// to the same division are describing the same instants rather than two rates
// that happen to be close.
double divisionBeats(int index);

// The division closest to a step length in beats, for showing what a linked or
// multiplied rate has worked out to.
int nearestDivisionForBeats(double beats);

// Picks the division closest to a free-running rate, so a synced dial snaps to
// musical values instead of needing a separate control of its own.
int nearestDivisionForHz(double hz, double bpm);
int nearestDivisionForMs(double milliseconds, double bpm);

double divisionToHz(int index, double bpm);
double divisionToMs(int index, double bpm);
juce::String divisionName(int index);

// ---- Linking one rate to another ----------------------------------------
// How the arpeggiator's rate relates to the sequencer's. Hardware sequencers
// and modular clocks solve this with a divider: one rate is the clock, and
// everything else runs at a whole multiple of it. Whole multiples are what
// keep the two on the same grid - an arpeggiator at twice the sequencer's rate
// still lands on every sequencer step, so what it plays can be recorded onto
// the sequencer's own timeline without landing between steps.
struct RateLink
{
    double multiplier; // arpeggiator steps per sequencer step; 0 means unlinked
    const char* name;
};

const RateLink* getRateLinks();
int getNumRateLinks();

// The entry that means "ignore the sequencer, use my own rate".
int freeRateLink();
bool isFreeRateLink(int index);
juce::String rateLinkName(int index);

// The step length a linked feature should use, given the sequencer's.
double linkedBeats(double sequencerBeats, int linkIndex);
} // namespace tempo

// Supplies the tempo everything syncs to: either the local BPM setting, or
// incoming MIDI clock when the host/hardware is driving. Falls back to the
// local value if the clock stops, so a dropped cable does not freeze the
// arpeggiator.
class TempoClock
{
public:
    void prepare(double sampleRate);
    void reset();

    // Reads MIDI clock messages out of the incoming buffer (they are left in
    // place; the synth ignores them).
    void processMidi(const juce::MidiBuffer& midi, int numSamples);

    double getBpm(const SynthParameters& params) const;

    // The clock's tempo when it is worth having, and nothing when it is not -
    // sync switched off, no clock arriving, or a reading outside 20 to 300 BPM,
    // which is a cable being unplugged rather than a tempo anybody chose.
    //
    // getBpm is built on this rather than repeating the test, because the panel
    // now has to answer "is the dial still yours?" with exactly the condition
    // the audio path uses. Two copies of it would drift, and the first anyone
    // would know is a readout disagreeing with what they hear.
    std::optional<double> getExternalClockBpm(const SynthParameters& params) const;

    bool isReceivingExternalClock() const { return receivingClock; }

    // How far back the tempo is fitted over - a length of time, not a number of
    // pulses. The accuracy of a line follows the span it is drawn across, and a
    // fixed count of pulses covers less time the faster the clock runs:
    // twenty-five of them is a third of a second at 174 BPM against two thirds
    // at 90, which is why a first attempt held twice as steady at slow tempos
    // as at fast ones. Sized by duration instead, both come out the same.
    //
    // Two seconds because a clock almost always runs at one tempo, and the
    // wobble is noise around it. Error falls off faster than the span grows, so
    // going from three quarters of a second to two takes half a BPM of movement
    // down to about a tenth of one.
    static constexpr double clockWindowSeconds = 2.0;

    // A second window, watched rather than used. Noise cannot hold a steady
    // offset from the long window; a real tempo change holds one immediately
    // and keeps holding it. So a short window that has drifted off the long one
    // for several pulses running is the sender having moved, and the long
    // window is thrown away and rebuilt from the short one.
    //
    // That is what makes two seconds of averaging safe. Without it the reading
    // would take two seconds to follow anybody; with it a real change lands in
    // about a fifth of a second, which is quicker than the shorter window was.
    static constexpr double clockShortWindowSeconds = 0.3;
    static constexpr double clockChangeThreshold = 0.015;
    static constexpr int clockChangeConfirmations = 3;

    // Room for the long window at the top of the range. 300 BPM is 120 pulses a
    // second, so two seconds of them is 240.
    static constexpr int clockCapacity = 256;

private:
    void recordClock(int64_t at);
    void refit();
    void forgetClocks();

    // How many of the stored pulses fall inside the last so many seconds.
    int countWithin(double seconds) const;

    // A line fitted through the newest so many pulses, as a tempo. Nothing when
    // the fit is not usable - too few points, or a slope that is not a tempo.
    std::optional<double> fitBpm(int usable) const;

    double sampleRate = 44100.0;

    // A running count, so every pulse has one number saying when it arrived
    // rather than a gap from the one before it. Gaps were the whole problem:
    // each carries the placement error of two positions, and at 24 PPQN the
    // pulses are only 20ms apart to begin with.
    int64_t samplesElapsed = 0;
    int64_t lastClockSample = 0;

    std::array<int64_t, clockCapacity> clockAt{};

    // Which pulse each one is, counted in pulses rather than in arrivals. A
    // link drops one now and again, and counting arrivals calls the pulse after
    // a lost one "the next" when it is really the one after next - so the line
    // is fitted to a gap that is twice as wide as it is supposed to be. One
    // dropped pulse in a hundred was enough to throw the reading around by ten
    // BPM. Worked out from the spacing instead, a lost pulse leaves a hole in
    // the numbering and the fit is undisturbed.
    std::array<int64_t, clockCapacity> clockIndex{};
    int64_t nextPulseIndex = 0;

    int clockCount = 0;
    int clockNext = 0;

    bool receivingClock = false;
    double smoothedBpm = 0.0;

    // Consecutive pulses on which the short window has disagreed with the long
    // one. Consecutive is the whole point: noise disagrees and then agrees
    // again, a tempo change disagrees and stays disagreeing.
    int divergentPulses = 0;

    // Consecutive pulses that arrived a whole multiple of the expected spacing
    // late. One is a dropped pulse; a run of them is the sender slowing down.
    int multiStepPulses = 0;
};

namespace tempo
{
// The single musical timeline everything rides on.
//
// A host keeps one running position in beats and every synced device works out
// where it is from that, rather than each counting its own steps. That is the
// whole reason two synced devices stay together: they are not two clocks being
// kept in agreement, they are two readings of one clock. WaveLathe does the
// same. The arpeggiator and the sequencer both ask this where the beat is, so
// at matching rates their notes land on exactly the same instants, and what
// the arpeggiator plays can be recorded onto the sequencer's grid and play
// back in the same places.
//
// It also removes the drift that comes of each feature rounding its own step
// length to a whole number of samples: a position read from one continuous
// counter cannot slide away from itself.
class MusicalClock
{
public:
    void prepare(double sampleRate);

    // Back to the downbeat. Pressing play does this, so a pattern always
    // starts at the top and a take recorded now lines up with one recorded
    // later.
    void reset();

    // Called once per block before anything reads the position.
    void beginBlock(double bpm, int numSamples);

    double getBlockStartBeat() const { return positionBeats; }
    double getBeatsPerSample() const { return beatsPerSample; }
    double beatAt(int sampleOffset) const
    {
        return positionBeats + (double) sampleOffset * beatsPerSample;
    }

    // How many samples one step of this length lasts, for working out how long
    // a note should ring. Kept in floating point: rounding here is what makes
    // separately-counted clocks drift apart.
    double samplesPerBeat() const
    {
        return beatsPerSample > 0.0 ? 1.0 / beatsPerSample : 0.0;
    }

private:
    double sampleRate = 44100.0;
    double positionBeats = 0.0;
    double beatsPerSample = 0.0;
    double pendingAdvance = 0.0;
};
} // namespace tempo
} // namespace wavelathe
