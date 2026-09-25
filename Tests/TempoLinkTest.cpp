// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Arpeggiator.h"
#include "StepSequencer.h"
#include "TempoSync.h"
#include "Scales.h"
#include <cmath>
#include <iostream>
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
constexpr int blockSize = 256;

// How long one step of a division lasts here, exactly - no rounding, because
// rounding is one of the things these tests check has gone away.
double stepSamplesFor(int division, double bpm)
{
    return tempo::divisionBeats(division) * 60.0 * sampleRate / bpm;
}

// Runs the pair the way the processor does: one clock, advanced once a block,
// read by both. Records where each of them put its note-ons, measured from the
// start of the run rather than from the start of the block.
struct Rig
{
    Arpeggiator arp;
    StepSequencer sequencer;
    tempo::MusicalClock clock;

    std::vector<long long> arpOnsets;
    std::vector<long long> sequencerOnsets;
    long long samplesRendered = 0;

    Rig()
    {
        arp.prepare(sampleRate);
        sequencer.prepare(sampleRate);
        clock.prepare(sampleRate);
    }

    // The chord is pressed on the first block and held for the whole run.
    void run(SynthParameters& params, int numBlocks, double bpm, const std::vector<int>& chord,
             int pressAtSample = 0)
    {
        for (int block = 0; block < numBlocks; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0)
                for (int note : chord)
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), pressAtSample);

            clock.beginBlock(bpm, blockSize);

            arp.process(midi, blockSize, params, clock, bpm);

            int arpNotes = 0;
            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                {
                    arpOnsets.push_back(samplesRendered + metadata.samplePosition);
                    ++arpNotes;
                }

            sequencer.process(midi, blockSize, params, clock);

            // Whatever the sequencer added on top of what it was handed.
            int seen = 0;
            for (const auto metadata : midi)
            {
                if (!metadata.getMessage().isNoteOn())
                    continue;
                if (seen++ < arpNotes)
                    continue;
                sequencerOnsets.push_back(samplesRendered + metadata.samplePosition);
            }

            samplesRendered += blockSize;
        }
    }
};

void setupBoth(SynthParameters& p)
{
    p.arpEnabled = 1.0f;
    p.arpSync = 1.0f;
    p.arpPattern = 0.0f; // straight steps
    p.arpMode = 0.0f;    // up
    p.arpOctaves = 1.0f;
    p.arpSwing = 0.0f;
    p.seqPlaying = 1.0f;
    p.seqLength = 16.0f;
    p.seqRootNote = 48.0f;
    p.musicalScale = (float) music::Scale::naturalMinor;
}

// The furthest any onset sits from the grid it is meant to be on.
//
// A note can only be placed on a whole sample, so landing within a sample or so
// of the exact instant is as close as the grid can be hit. What matters is that
// the distance stays there however long the run is, rather than growing.
double worstOffGrid(const std::vector<long long>& onsets, double stepSamples)
{
    double worst = 0.0;
    for (auto onset : onsets)
    {
        double steps = (double) onset / stepSamples;
        double distance = std::abs(steps - std::round(steps)) * stepSamples;
        worst = juce::jmax(worst, distance);
    }
    return worst;
}
// ---- Incoming MIDI clock, as it really arrives --------------------------
// The sample positions a synth is handed are not when the clock arrived.
// JUCE's MidiMessageCollector measures the wall-clock gap between audio
// callbacks and rescales every message into the block to make it fit, so each
// position carries a few milliseconds of scheduling noise. Against the 20.8ms
// pulse of 24-PPQN clock at 120 BPM that is a tenth of the interval, and a
// tempo read straight off one interval inherits all of it.
//
// So the clock below is generated true and then knocked about the way the
// collector knocks it about, and the test asks what survives.
struct ClockRun
{
    double lowest = 1.0e9;
    double highest = 0.0;
    double settled = 0.0;
    int readings = 0;

    double spread() const { return readings > 0 ? highest - lowest : 0.0; }
};

ClockRun runMidiClock(TempoClock& clock, const SynthParameters& params, double sourceBpm,
                      double jitterMs, int blocks, int settleBlocks, double dropChance = 0.0)
{
    ClockRun run;

    const double pulseSamples = 60.0 * sampleRate / (sourceBpm * 24.0);
    const double jitterSamples = jitterMs * 0.001 * sampleRate;

    juce::Random random(0xc10c);
    double nextPulse = pulseSamples;

    for (int b = 0; b < blocks; ++b)
    {
        const double blockStart = (double) b * blockSize;
        const double blockEnd = blockStart + blockSize;

        juce::MidiBuffer midi;

        while (nextPulse < blockEnd)
        {
            // Displaced, then clamped into its own block - which is what the
            // collector does, and why the error cannot simply average itself
            // out inside one block.
            const bool dropped = random.nextDouble() < dropChance;
            const double wobble = (random.nextDouble() * 2.0 - 1.0) * jitterSamples;
            const int at = (int) juce::jlimit(blockStart, blockEnd - 1.0, nextPulse + wobble);

            if (! dropped)
                midi.addEvent(juce::MidiMessage::midiClock(), at - (int) blockStart);
            nextPulse += pulseSamples;
        }

        clock.processMidi(midi, blockSize);

        if (b >= settleBlocks)
        {
            if (auto external = clock.getExternalClockBpm(params))
            {
                run.lowest = juce::jmin(run.lowest, *external);
                run.highest = juce::jmax(run.highest, *external);
                run.settled = *external;
                ++run.readings;
            }
        }
    }

    return run;
}
} // namespace

int main()
{
    std::cout << "Tempo link test - one clock behind the arpeggiator and the sequencer"
              << std::endl << std::endl;

    int sixteenth = tempo::nearestDivisionForBeats(0.25);
    check(tempo::divisionName(sixteenth) == "1/16", "1/16 is findable by its length");

    // ---- The default is the same grid, not a near miss ---------------------
    std::cout << "Out of the box:" << std::endl;
    {
        SynthParameters p;
        check((int) std::round(p.seqDivision.load()) == sixteenth,
              "the sequencer starts on a straight 1/16, not a dotted one");
        check(tempo::rateLinkName((int) std::round(p.arpRateLink.load())) == "Seq x 1",
              "the arpeggiator starts following the sequencer at its own rate");

        double arpBeats = Arpeggiator::stepBeatsFor(p, 120.0);
        double seqBeats = tempo::divisionBeats((int) std::round(p.seqDivision.load()));
        check(std::abs(arpBeats - seqBeats) < 1.0e-9, "so both are the same length out of the box");

        std::cout << "  both at " << tempo::divisionName(sixteenth) << ", "
                  << juce::String(stepSamplesFor(sixteenth, 120.0), 1) << " samples a step"
                  << std::endl;
    }

    // ---- Linked rates land on the same instants ----------------------------
    std::cout << std::endl << "Linked at Seq x 1:" << std::endl;
    {
        SynthParameters p;
        setupBoth(p);

        Rig rig;
        for (int i = 0; i < 4; ++i)
        {
            StepSequencer::Step step;
            step.active = true;
            step.degree = i;
            rig.sequencer.setStep(i * 4, step);
        }

        double stepSamples = stepSamplesFor(sixteenth, 120.0);
        rig.run(p, 512, 120.0, {48, 51, 55});

        check(!rig.arpOnsets.empty(), "the arpeggiator played");
        check(!rig.sequencerOnsets.empty(), "the sequencer played");

        double arpOff = worstOffGrid(rig.arpOnsets, stepSamples);
        double seqOff = worstOffGrid(rig.sequencerOnsets, stepSamples);

        std::cout << "  arpeggiator worst distance off the grid: " << juce::String(arpOff, 3)
                  << " samples" << std::endl;
        std::cout << "  sequencer worst distance off the grid:   " << juce::String(seqOff, 3)
                  << " samples" << std::endl;

        check(arpOff < 2.0, "every arpeggiator note lands on a step of the shared grid");
        check(seqOff < 2.0, "every sequencer note lands on a step of the shared grid");
    }

    // ---- A multiplier is a whole multiple, so the grids still agree --------
    std::cout << std::endl << "Linked at Seq x 2:" << std::endl;
    {
        SynthParameters single;
        setupBoth(single);
        Rig one;
        one.run(single, 512, 120.0, {48, 51, 55});

        SynthParameters doubled;
        setupBoth(doubled);
        doubled.arpRateLink = 3.0f; // Seq x 2
        Rig two;
        two.run(doubled, 512, 120.0, {48, 51, 55});

        std::cout << "  x1 played " << (int) one.arpOnsets.size() << " notes, x2 played "
                  << (int) two.arpOnsets.size() << std::endl;
        check(two.arpOnsets.size() == one.arpOnsets.size() * 2,
              "twice the rate is exactly twice as many notes over the same stretch");

        double worst = 0.0;
        for (size_t i = 0; i < one.arpOnsets.size() && i * 2 < two.arpOnsets.size(); ++i)
            worst = juce::jmax(worst, (double) std::abs(two.arpOnsets[i * 2] - one.arpOnsets[i]));

        std::cout << "  worst disagreement between them: " << juce::String(worst, 1) << " samples"
                  << std::endl;
        check(worst <= 1.0, "and every other one of them lands where the slower rate played");
    }

    // ---- Starting late does not start a grid of its own -------------------
    std::cout << std::endl << "Pressing a key mid-step:" << std::endl;
    {
        SynthParameters onTime;
        setupBoth(onTime);
        Rig early;
        early.run(onTime, 32, 120.0, {48, 51, 55}, 0);

        SynthParameters late;
        setupBoth(late);
        Rig delayed;
        delayed.run(late, 32, 120.0, {48, 51, 55}, 137); // part-way into the block

        double stepSamples = stepSamplesFor(sixteenth, 120.0);
        double off = worstOffGrid(delayed.arpOnsets, stepSamples);

        std::cout << "  pressed 137 samples late, worst distance off the grid: "
                  << juce::String(off, 2) << " samples" << std::endl;
        check(off < 2.0, "a key pressed between steps still produces notes on the grid");
        check(delayed.arpOnsets.size() >= early.arpOnsets.size() - 1,
              "and it joins the pattern rather than waiting for the next bar");
    }

    // ---- No drift, however long it runs -----------------------------------
    std::cout << std::endl << "Over a long run:" << std::endl;
    {
        SynthParameters p;
        setupBoth(p);
        Rig rig;
        rig.run(p, 3000, 137.0, {48, 51, 55}); // an awkward tempo, on purpose

        double stepSamples = stepSamplesFor(sixteenth, 137.0);
        double off = worstOffGrid(rig.arpOnsets, stepSamples);

        std::cout << "  " << (int) rig.arpOnsets.size()
                  << " notes at 137 bpm, worst distance off the grid: " << juce::String(off, 3)
                  << " samples" << std::endl;
        check(rig.arpOnsets.size() > 100, "it ran long enough for drift to show");
        check(off < 2.0, "the last note is as much on the grid as the first");
    }

    // ---- Free running is still free ---------------------------------------
    std::cout << std::endl << "Sync off:" << std::endl;
    {
        SynthParameters p;
        setupBoth(p);
        p.arpSync = 0.0f;
        p.arpRateHz = 10.0f;

        double beats = Arpeggiator::stepBeatsFor(p, 120.0);
        double expected = 120.0 / 60.0 / 10.0;

        std::cout << "  10 /sec at 120 bpm is " << juce::String(beats, 4) << " beats a step"
                  << std::endl;
        check(std::abs(beats - expected) < 1.0e-9, "an unsynced rate is the speed you asked for");
        check(std::abs(beats - tempo::divisionBeats(sixteenth)) > 1.0e-6,
              "and is not quietly snapped onto the grid");
    }

    // ---- What the link works out to ---------------------------------------
    std::cout << std::endl << "What the link works out to:" << std::endl;
    {
        struct Case
        {
            const char* sequencerRate;
            const char* link;
            const char* arpRate;
        };

        const Case cases[] = {{"1/16", "Seq x 1", "1/16"}, {"1/16", "Seq x 2", "1/32"},
                              {"1/8", "Seq x 2", "1/16"},  {"1/8", "Seq / 2", "1/4"},
                              {"1/8T", "Seq x 2", "1/16T"}};

        for (const auto& c : cases)
        {
            int seqIndex = 0, linkIndex = 0;
            for (int i = 0; i < tempo::getNumDivisions(); ++i)
                if (tempo::divisionName(i) == c.sequencerRate)
                    seqIndex = i;
            for (int i = 0; i < tempo::getNumRateLinks(); ++i)
                if (tempo::rateLinkName(i) == c.link)
                    linkIndex = i;

            double beats = tempo::linkedBeats(tempo::divisionBeats(seqIndex), linkIndex);
            auto name = tempo::divisionName(tempo::nearestDivisionForBeats(beats));

            std::cout << "  " << c.sequencerRate << "  " << c.link << "  ->  " << name << std::endl;
            check(name == c.arpRate,
                  juce::String(c.sequencerRate) + " " + c.link + " should read " + c.arpRate);
        }
    }

    // ---- The pattern survives being put away and fetched back -------------
    std::cout << std::endl << "Saving and restoring the pattern:" << std::endl;
    {
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);

        SynthParameters p;
        setupBoth(p);

        StepSequencer::Step step;
        step.active = true;
        step.degree = 4;
        step.octave = 1;
        step.length = 6 * StepSequencer::ticksPerStep;
        step.tickOffset = 2;
        step.velocity = 0.42f;
        step.locked = true;
        sequencer.setStep(3, step);

        sequencer.createLane(1, p);
        sequencer.setLaneValue(1, 10, 0.75f);

        auto state = sequencer.captureState();

        StepSequencer restored;
        restored.prepare(sampleRate);
        restored.restoreState(state);

        auto back = restored.getStep(3);
        std::cout << "  step 3: degree " << back.degree << ", octave " << back.octave << ", length "
                  << back.length << ", offset " << back.tickOffset << ", velocity "
                  << juce::String(back.velocity, 2) << (back.locked ? ", locked" : "") << std::endl;

        check(back.active && back.degree == 4 && back.octave == 1, "the note came back");
        check(back.length == 6 * StepSequencer::ticksPerStep && back.tickOffset == 2,
              "with the length and timing it was given");
        check(std::abs(back.velocity - 0.42f) < 1.0e-6 && back.locked, "and its velocity and lock");
        check(restored.isLaneUsed(1), "the automation lane came back too");
        check(std::abs(restored.getLaneValue(1, 10) - 0.75f) < 1.0e-6, "with what was drawn on it");
    }

    // ---- A tempo worked out from incoming MIDI clock ------------------------
    // Nothing had ever tested this. Everything above exercises MusicalClock,
    // the beat clock the arpeggiator and the sequencer run on. TempoClock - the
    // thing that works a tempo out of 24-PPQN MIDI clock - had no test at all,
    // and shipped wrong from 0.3.0. It only showed when 0.38.3 made the readout
    // report the tempo actually in force, and a steady hardware clock made the
    // number jump around the room.
    std::cout << std::endl << "A tempo worked out from incoming MIDI clock:" << std::endl;
    {
        SynthParameters p;
        p.midiClockSync = 1.0f;
        p.bpm = 100.0f; // deliberately not the clock's tempo, so a fallback shows

        const int blocks = (int) (sampleRate * 6.0 / blockSize);
        const int settle = (int) (sampleRate * 3.0 / blockSize);

        const double tempos[] = {90.0, 120.0, 145.0, 174.0};

        for (double sourceBpm : tempos)
        {
            TempoClock clean;
            clean.prepare(sampleRate);
            auto perfect = runMidiClock(clean, p, sourceBpm, 0.0, blocks, settle);

            TempoClock rough;
            rough.prepare(sampleRate);
            auto jittered = runMidiClock(rough, p, sourceBpm, 3.0, blocks, settle);

            std::cout << "  " << juce::String(sourceBpm, 0).paddedLeft(' ', 4) << " BPM"
                      << "    clean " << juce::String(perfect.settled, 2).paddedLeft(' ', 7)
                      << " +/-" << juce::String(perfect.spread(), 2).paddedLeft(' ', 6)
                      << "    jittered " << juce::String(jittered.settled, 2).paddedLeft(' ', 7)
                      << " +/-" << juce::String(jittered.spread(), 2).paddedLeft(' ', 6)
                      << std::endl;

            check(perfect.readings > 0,
                  juce::String(sourceBpm, 0) + ": a clean clock is followed at all");
            check(std::abs(perfect.settled - sourceBpm) < 0.5,
                  juce::String(sourceBpm, 0) + ": a clean clock reads its own tempo");

            // The one that matters. Three milliseconds either way is ordinary
            // scheduling noise on Windows and it is what the collector hands
            // over; a tempo that swings with it is a tempo nobody can play to.
            check(std::abs(jittered.settled - sourceBpm) < 0.25,
                  juce::String(sourceBpm, 0) + ": a jittered clock still reads its own tempo");
            check(jittered.spread() < 0.3,
                  juce::String(sourceBpm, 0) + ": and holds still while it does");
        }
    }

    std::cout << std::endl << "With the occasional pulse lost on the way:" << std::endl;
    {
        SynthParameters p;
        p.midiClockSync = 1.0f;
        p.bpm = 100.0f;

        const int blocks = (int) (sampleRate * 6.0 / blockSize);
        const int settle = (int) (sampleRate * 3.0 / blockSize);

        for (double sourceBpm : {90.0, 120.0, 174.0})
        {
            for (double drop : {0.0, 0.002, 0.01})
            {
                TempoClock clock;
                clock.prepare(sampleRate);
                auto run = runMidiClock(clock, p, sourceBpm, 3.0, blocks, settle, drop);

                std::cout << "  " << juce::String(sourceBpm, 0).paddedLeft(' ', 4) << " BPM"
                          << "   losing " << juce::String(drop * 100.0, 1).paddedLeft(' ', 4) << "%"
                          << "   reads " << juce::String(run.settled, 2).paddedLeft(' ', 7)
                          << " +/-" << juce::String(run.spread(), 2).paddedLeft(' ', 6) << std::endl;
            }
        }
    }

    // The other half of averaging over a beat: it has to still be able to move.
    // A window long enough to ignore noise is a window long enough to ignore a
    // real change too, if nobody checks which.
    std::cout << std::endl << "When the sender changes tempo:" << std::endl;
    {
        SynthParameters p;
        p.midiClockSync = 1.0f;
        p.bpm = 100.0f;

        TempoClock clock;
        clock.prepare(sampleRate);

        const int threeSeconds = (int) (sampleRate * 3.0 / blockSize);
        const int halfSecond = (int) (sampleRate * 0.5 / blockSize);

        runMidiClock(clock, p, 120.0, 2.0, threeSeconds, threeSeconds);
        auto before = clock.getExternalClockBpm(p);
        check(before.has_value() && std::abs(*before - 120.0) < 1.5, "settled on 120 first");

        // The sender moves to 140 and keeps going. One second is fifty-six
        // pulses, better than two windows: if it has not arrived by then the
        // averaging has been taken too far.
        auto moved = runMidiClock(clock, p, 140.0, 2.0, halfSecond, halfSecond - 4);
        std::cout << "  120 -> 140 reads " << juce::String(moved.settled, 2)
                  << " half a second later" << std::endl;
        check(std::abs(moved.settled - 140.0) < 1.0, "and is at 140 half a second later");
    }

    // Halving is the case that tolerating dropped pulses would otherwise
    // swallow whole. Every gap doubles, which looks exactly like losing every
    // other pulse - and read that way the fit agrees with itself at the old
    // tempo and never notices it is wrong.
    std::cout << std::endl << "When the sender halves its tempo:" << std::endl;
    {
        SynthParameters p;
        p.midiClockSync = 1.0f;
        p.bpm = 100.0f;

        TempoClock clock;
        clock.prepare(sampleRate);

        const int threeSeconds = (int) (sampleRate * 3.0 / blockSize);

        runMidiClock(clock, p, 140.0, 2.0, threeSeconds, threeSeconds);
        auto before = clock.getExternalClockBpm(p);
        check(before.has_value() && std::abs(*before - 140.0) < 1.5, "settled on 140 first");

        auto halved = runMidiClock(clock, p, 70.0, 2.0, threeSeconds, threeSeconds - 8);
        std::cout << "  140 -> 70 reads " << juce::String(halved.settled, 2) << std::endl;
        check(std::abs(halved.settled - 70.0) < 1.5, "and follows it down to 70 rather than staying at 140");
    }

    std::cout << std::endl << "When the clock stops:" << std::endl;
    {
        SynthParameters p;
        p.midiClockSync = 1.0f;
        p.bpm = 100.0f;

        TempoClock clock;
        clock.prepare(sampleRate);

        const int blocks = (int) (sampleRate * 2.0 / blockSize);
        auto run = runMidiClock(clock, p, 140.0, 1.0, blocks, blocks / 2);
        check(run.readings > 0 && std::abs(run.settled - 140.0) < 1.5, "140 is being followed");

        // A second of silence - a cable out, or the sender stopping.
        juce::MidiBuffer nothing;
        for (int i = 0; i < (int) (sampleRate * 1.0 / blockSize); ++i)
            clock.processMidi(nothing, blockSize);

        check(! clock.getExternalClockBpm(p).has_value(), "the clock is let go of once it stops");
        check(std::abs(clock.getBpm(p) - 100.0) < 0.001, "and the tempo falls back to the local BPM");
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL TEMPO LINK TESTS PASSED" << std::endl;
    else
        std::cout << failures << " TEMPO LINK TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
