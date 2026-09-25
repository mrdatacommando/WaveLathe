// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <memory>
#include "StepSequencer.h"
#include "NoteLatch.h"
#include "MonoVoice.h"
#include "UndoHistory.h"
#include "ParameterRegistry.h"
#include "LaneFilter.h"
#include "Scales.h"
#include "TempoSync.h"
#include <iostream>
#include <set>

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

juce::String toText(const std::vector<int>& values)
{
    juce::String text;
    for (size_t i = 0; i < values.size(); ++i)
        text << (i > 0 ? " " : "") << values[i];
    return text;
}

constexpr double sampleRate = 44100.0;

// SynthParameters holds atomics and so cannot be copied; the test settings are
// applied to a fresh block instead.
void setupParams(SynthParameters& p)
{
    p.seqPlaying = 1.0f;
    p.seqLength = 8.0f;
    p.seqRootNote = 48.0f;
    p.musicalKey = 0.0f;
    p.musicalScale = (float) music::Scale::naturalMinor;
}

// Counts note-ons out of a buffer.
std::vector<int> noteOnsIn(const juce::MidiBuffer& midi)
{
    std::vector<int> notes;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn())
            notes.push_back(metadata.getMessage().getNoteNumber());
    return notes;
}

std::vector<int> noteOffsIn(const juce::MidiBuffer& midi)
{
    std::vector<int> notes;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOff())
            notes.push_back(metadata.getMessage().getNoteNumber());
    return notes;
}

// Runs a block the way the processor does: the shared clock is advanced once
// for the block, then the sequencer reads its position off it. Tests drive it
// themselves so what they measure is the same timing the synth produces.
void runBlock(StepSequencer& sequencer, tempo::MusicalClock& clock, juce::MidiBuffer& midi,
              int numSamples, SynthParameters& params, double bpm)
{
    clock.beginBlock(bpm, numSamples);
    sequencer.process(midi, numSamples, params, clock);
}

// Samples per sequencer step at the given settings.
int samplesPerStep(const SynthParameters& params, double bpm)
{
    int division = (int) std::round(params.seqDivision.load());
    return (int) (sampleRate / tempo::divisionToHz(division, bpm));
}

// The drum pattern's timing. A function of its own for the same reason slide
// is - see the note below - and because this is the seam that makes drum
// timing answerable without rendering a single sample of audio.
//
// The sequencer hands out hits; the kit turns hits into sound; neither knows
// the other exists. So "does a hit nudged three ticks early land at the end of
// the bar before it" is a question about integers here, not about a waveform.
void drumHitSection()
{
    std::cout << std::endl << "  drum hits" << std::endl;

    constexpr int kick = 0;
    constexpr int snare = 1;
    constexpr double bpm = 120.0;

    // Four steps, so the pattern is 64 ticks and 8 cells long and a whole bar
    // of it fits in one block worth measuring.
    auto buildRig = [](StepSequencer& sequencer, SynthParameters& params)
    {
        setupParams(params);
        params.seqLength = 4.0f;

        sequencer.prepare(sampleRate);
        sequencer.reset();
    };

    // Where a tick falls, in samples, WITHOUT rounding the step first.
    //
    // samplesPerStep truncates, and at 120 bpm a 1/16 step is 5512.5 samples,
    // so by tick 61 a truncated step is three samples adrift - which reads
    // exactly like a timing bug in the sequencer and is a rounding error in
    // the test. The sequencer works in doubles off the shared clock, so an
    // expectation about it has to as well.
    auto sampleOfTick = [](const SynthParameters& params, double tempo, int tick)
    {
        const auto division = (int) std::round(params.seqDivision.load());
        const auto exactStep = sampleRate / tempo::divisionToHz(division, tempo);
        return (int) std::ceil((double) tick * exactStep / (double) StepSequencer::ticksPerStep);
    };

    // Every hit the block produced, as (voice, sample) pairs.
    auto hitsIn = [](const StepSequencer& sequencer)
    {
        std::vector<std::pair<int, int>> hits;

        for (int i = 0; i < sequencer.getNumDrumHits(); ++i)
        {
            const auto hit = sequencer.getDrumHit(i);
            hits.push_back({ hit.voice, hit.sampleOffset });
        }

        return hits;
    };

    // ---- A hit lands on the sample its cell asks for -----------------------
    {
        StepSequencer sequencer;
        SynthParameters params;
        buildRig(sequencer, params);

        const int stepSamples = samplesPerStep(params, bpm);

        sequencer.setDrumCellActive(kick, 0, true);
        sequencer.setDrumCellActive(snare, 4, true);   // two steps in, on the beat

        tempo::MusicalClock clock;
        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, stepSamples * 4, params, bpm);

        const auto hits = hitsIn(sequencer);
        check(hits.size() == 2, "a bar with two hits on it produces two hits");

        if (hits.size() == 2)
        {
            // Cell 4 is tick 32: four cells of eight ticks each.
            const auto expected = sampleOfTick(params, bpm, 32);

            check(hits[0].first == kick && hits[0].second == 0, "the kick is on the downbeat");
            check(hits[1].first == snare && std::abs(hits[1].second - expected) <= 2,
                  "and the snare is four cells later, to the sample");

            std::cout << "    kick at " << hits[0].second << ", snare at " << hits[1].second
                      << " (cell 4 is sample " << expected << ")" << std::endl;
        }
    }

    // ---- A nudge moves it, and can move it into the bar before -------------
    //
    // The half of nudge that the melodic pattern's late-only tickOffset cannot
    // express at all. A kick a hair AHEAD of the downbeat is one of the
    // commonest things anybody does with it, and where it actually sounds is
    // at the very end of the bar before.
    //
    // That behaviour lives in scanDrumCells wrapping the CELL into the pattern
    // while leaving its base tick unwrapped. The asymmetry is easy to tidy
    // away by accident, and this is the check that catches it: wrapping the
    // base as well loses the early hit entirely, and that mutation fails here.
    {
        StepSequencer sequencer;
        SynthParameters params;
        buildRig(sequencer, params);

        const int stepSamples = samplesPerStep(params, bpm);

        sequencer.setDrumCellActive(kick, 2, true);
        sequencer.setDrumCellNudge(kick, 2, 2);        // two ticks late
        sequencer.setDrumCellActive(snare, 0, true);
        sequencer.setDrumCellNudge(snare, 0, -3);      // three ticks early

        tempo::MusicalClock clock;
        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, stepSamples * 4, params, bpm);

        const auto hits = hitsIn(sequencer);
        check(hits.size() == 2, "both nudged hits still sound");

        if (hits.size() == 2)
        {
            // Cell 2 is tick 16; two ticks late is tick 18.
            const auto lateAt = sampleOfTick(params, bpm, 18);

            // Cell 0 is tick 0; three ticks early is tick -3, which in a
            // 64-tick pattern is tick 61.
            const auto earlyAt = sampleOfTick(params, bpm, 61);

            check(hits[0].first == kick && std::abs(hits[0].second - lateAt) <= 2,
                  "a hit nudged late lands late by exactly its nudge");
            check(hits[1].first == snare && std::abs(hits[1].second - earlyAt) <= 2,
                  "and one nudged early lands at the END of the bar before it");

            std::cout << "    +2 kick at " << hits[0].second << " (tick 18 is " << lateAt
                      << "), -3 snare at " << hits[1].second << " (tick 61 is " << earlyAt
                      << ", not 0)" << std::endl;
        }
    }

    // ---- What does not sound -----------------------------------------------
    {
        StepSequencer sequencer;
        SynthParameters params;
        buildRig(sequencer, params);

        const int stepSamples = samplesPerStep(params, bpm);

        // Past the end of a four-step pattern. The drum pattern shares the
        // melodic one's length - that is what stops a beat drifting out of
        // step with the line above it - so these cells are simply not reached.
        sequencer.setDrumCellActive(kick, 20, true);

        tempo::MusicalClock clock;
        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, stepSamples * 4, params, bpm);

        check(sequencer.getNumDrumHits() == 0, "a hit written past the pattern's length does not sound");

        // And the same pattern with the transport stopped.
        StepSequencer stopped;
        SynthParameters stoppedParams;
        buildRig(stopped, stoppedParams);
        stopped.setDrumCellActive(kick, 0, true);

        tempo::MusicalClock stoppedClock;
        juce::MidiBuffer stoppedMidi;
        runBlock(stopped, stoppedClock, stoppedMidi, stepSamples, stoppedParams, bpm);
        check(stopped.getNumDrumHits() == 1, "with the transport running, it does");

        stoppedParams.seqPlaying = 0.0f;
        runBlock(stopped, stoppedClock, stoppedMidi, stepSamples, stoppedParams, bpm);
        check(stopped.getNumDrumHits() == 0,
              "and a stopped sequencer reports nothing, rather than last block's hits again");
    }

    // ---- Velocity and accent travel with the hit ---------------------------
    {
        StepSequencer sequencer;
        SynthParameters params;
        buildRig(sequencer, params);

        StepSequencer::DrumCell cell;
        cell.active = true;
        cell.velocity = 0.42f;
        cell.accent = true;
        sequencer.setDrumCell(kick, 0, cell);

        tempo::MusicalClock clock;
        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, samplesPerStep(params, bpm), params, bpm);

        check(sequencer.getNumDrumHits() == 1, "the hit is there");

        if (sequencer.getNumDrumHits() == 1)
        {
            const auto hit = sequencer.getDrumHit(0);
            check(std::abs(hit.velocity - 0.42f) < 0.001f, "carrying the velocity it was drawn with");
            check(hit.accent, "and its accent");
        }

        check(sequencer.takeDroppedDrumHits() == 0, "and nothing was dropped");
    }
}

// Slide lives in a function of its own rather than inline in main, and not
// for tidiness: main already holds fifteen StepSequencers across its
// sections, MSVC reserves the whole frame on entry rather than per scope,
// and six more took it past the default one-megabyte stack. The suite then
// overflowed before printing a single line, which reads exactly like a
// crash in the new code and is not - it is the size of main.
void slideAndAccentSection()
{

    // ---- Slide: one step tied into the next ---------------------------------
    // A slide is not "a note plus a glide setting" - it is a note still
    // sounding when the next one starts, so that the next one has something to
    // bend from. That makes it a question about the ORDER of two events at the
    // same instant, which is what these read.
    //
    // Events are collected with their sample positions rather than split into
    // note-ons and note-offs, because "which came first" is the whole point and
    // the helpers above throw exactly that away.
    std::cout << std::endl << "Slide, one step tied into the next:" << std::endl;
    {
        struct Event
        {
            int sample;
            bool isOn;
            int note;
        };

        auto eventsIn = [](const juce::MidiBuffer& midi)
        {
            std::vector<Event> events;
            for (const auto metadata : midi)
            {
                auto message = metadata.getMessage();
                if (message.isNoteOn() || message.isNoteOff())
                    events.push_back({metadata.samplePosition, message.isNoteOn(),
                                      message.getNoteNumber()});
            }
            return events;
        };

        auto describe = [](const std::vector<Event>& events)
        {
            juce::String text;
            for (const auto& e : events)
                text << (text.isEmpty() ? "" : "  ") << (e.isOn ? "on " : "off ")
                     << e.note << "@" << e.sample;
            return text;
        };

        // Two steps a fifth apart, the first tied into the second, and a short
        // gate on the first so that without the tie it would plainly have
        // stopped long before the second arrived.
        auto buildRig = [](StepSequencer& sequencer, SynthParameters& params, bool mono,
                           float secondStepChance)
        {
            setupParams(params);
            params.seqLength = 2.0f;
            params.monoMode = mono ? 1.0f : 0.0f;
            params.legatoMode = mono ? 1.0f : 0.0f;

            sequencer.prepare(sampleRate);
            sequencer.reset();

            StepSequencer::Step first;
            first.active = true;
            first.degree = 0;
            first.gate = 0.25f; // would be over well before the next step
            first.slide = true;
            sequencer.setStep(0, first);

            StepSequencer::Step second;
            second.active = true;
            second.degree = 4;
            second.probability = secondStepChance;
            sequencer.setStep(1, second);
        };

        // ---- With mono and legato: the tie holds ----------------------------
        {
            StepSequencer sequencer;
            SynthParameters params;
            buildRig(sequencer, params, true, 1.0f);

            tempo::MusicalClock clock;
            const double bpm = 120.0;
            const int stepSamples = samplesPerStep(params, bpm);

            juce::MidiBuffer midi;
            runBlock(sequencer, clock, midi, stepSamples * 2, params, bpm);

            auto events = eventsIn(midi);
            std::cout << "  " << describe(events) << std::endl;

            // Four: the first note on, the second on, the first off - and then
            // the second one's own gate at the end of the buffer, which is an
            // ordinary release and not part of the slide. A fifth would mean
            // the tie leaked or the first gate fired after all.
            check(events.size() == 4, "a tied step gives one note-on, then the next, then one release");

            if (events.size() == 4)
            {
                check(events[0].isOn, "the first step starts");
                check(events[1].isOn, "and the next note arrives before anything is released");
                check(!events[2].isOn && events[2].note == events[0].note,
                      "the note released is the one that was slid out of");
                check(events[1].sample == events[2].sample,
                      "both land in the same instant, so nothing is heard to stop");
                check(events[1].note != events[0].note, "and the pitch actually moved");
            }

            // The first step's gate is a quarter of its length, so without the
            // tie its release would arrive a long way before the second step.
            // This is the reading that says the tie is doing the work, rather
            // than the arithmetic happening to line up.
            int releasesBeforeSecondStep = 0;
            for (const auto& e : events)
                if (!e.isOn && e.sample < stepSamples)
                    ++releasesBeforeSecondStep;

            std::cout << "  releases before the second step: " << releasesBeforeSecondStep
                      << "   (the gate alone would have ended at " << (int) (stepSamples * 0.25)
                      << " of " << stepSamples << ")" << std::endl;

            check(releasesBeforeSecondStep == 0, "a tied note outlasts its own gate");
        }

        // ---- Sliding into a step that never sounds --------------------------
        // A chance roll can skip the step a slide was aiming at. The held note
        // then has nothing to resolve into, and the release that was postponed
        // has to arrive anyway - otherwise the pattern hangs on that note for
        // good, which is the worst failure a sequencer has.
        {
            StepSequencer sequencer;
            SynthParameters params;
            buildRig(sequencer, params, true, 0.0f); // the second step never fires

            tempo::MusicalClock clock;
            const double bpm = 120.0;
            const int stepSamples = samplesPerStep(params, bpm);

            juce::MidiBuffer midi;
            runBlock(sequencer, clock, midi, stepSamples * 2, params, bpm);

            auto events = eventsIn(midi);
            std::cout << "  into a step that never fires: " << describe(events) << std::endl;

            int ons = 0, offs = 0;
            for (const auto& e : events)
                (e.isOn ? ons : offs) += 1;

            check(ons == 1 && offs == 1, "a slide into a skipped step still releases its note");
        }

        // ---- Without mono and legato, a slide plays straight ------------------
        // There is nothing to bend and nothing to bend it, so holding the note
        // on would only stack two notes into a chord. Playing the step normally
        // is wrong, but it is the kind of wrong a person can hear and explain.
        {
            StepSequencer sequencer;
            SynthParameters params;
            buildRig(sequencer, params, false, 1.0f);

            tempo::MusicalClock clock;
            const double bpm = 120.0;
            const int stepSamples = samplesPerStep(params, bpm);

            juce::MidiBuffer midi;
            runBlock(sequencer, clock, midi, stepSamples * 2, params, bpm);

            auto events = eventsIn(midi);
            std::cout << "  with mono off: " << describe(events) << std::endl;

            bool everTwoHeld = false;
            int held = 0;
            for (const auto& e : events)
            {
                held += e.isOn ? 1 : -1;
                if (held > 1)
                    everTwoHeld = true;
            }

            check(!everTwoHeld, "without mono and legato a slide never stacks two notes");
        }

        // ---- Stopping in the middle of a slide --------------------------------
        {
            StepSequencer sequencer;
            SynthParameters params;
            buildRig(sequencer, params, true, 1.0f);

            tempo::MusicalClock clock;
            const double bpm = 120.0;
            const int stepSamples = samplesPerStep(params, bpm);

            juce::MidiBuffer midi;
            runBlock(sequencer, clock, midi, stepSamples / 2, params, bpm); // mid-slide

            params.seqPlaying = 0.0f;
            juce::MidiBuffer stopped;
            runBlock(sequencer, clock, stopped, 256, params, bpm);

            std::cout << "  stopping mid-slide: " << describe(eventsIn(stopped)) << std::endl;

            check(noteOffsIn(stopped).size() == 1, "stopping in the middle of a slide lets the note go");
        }

        // ---- The flag survives being saved ------------------------------------
        {
            StepSequencer sequencer;
            sequencer.prepare(sampleRate);

            StepSequencer::Step step;
            step.active = true;
            step.slide = true;
            sequencer.setStep(3, step);

            auto state = sequencer.captureState();

            StepSequencer restored;
            restored.prepare(sampleRate);
            restored.restoreState(state);

            check(restored.getStep(3).slide, "a slide is remembered when the pattern is saved");
            check(!restored.getStep(2).slide, "and a step without one stays without one");
        }
    }

    // ---- Accent: one dial, many steps ---------------------------------------
    // An accented step ignores its own velocity and plays at the pattern's
    // accent level instead. That is the whole mechanism, and it is deliberately
    // the whole mechanism: everything an accent DOES - louder, brighter, more
    // resonant - happens downstream because velocity is already wired to all
    // three. Nothing here knows about any of it.
    std::cout << std::endl << "Accent:" << std::endl;
    {
        auto velocitiesIn = [](const juce::MidiBuffer& midi)
        {
            std::vector<float> velocities;
            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                    velocities.push_back(metadata.getMessage().getFloatVelocity());
            return velocities;
        };

        auto runPattern = [&](float accentAmount)
        {
            auto sequencerPtr = std::make_unique<StepSequencer>();
            auto& sequencer = *sequencerPtr;

            SynthParameters params;
            setupParams(params);
            params.seqLength = 2.0f;
            params.accentAmount = accentAmount;

            sequencer.prepare(sampleRate);
            sequencer.reset();

            // Two steps at the same quiet velocity; only the second is accented.
            StepSequencer::Step plain;
            plain.active = true;
            plain.degree = 0;
            plain.velocity = 0.30f;
            sequencer.setStep(0, plain);

            StepSequencer::Step accented = plain;
            accented.degree = 2;
            accented.accent = true;
            sequencer.setStep(1, accented);

            tempo::MusicalClock clock;
            const double bpm = 120.0;

            juce::MidiBuffer midi;
            runBlock(sequencer, clock, midi, samplesPerStep(params, bpm) * 2, params, bpm);

            return velocitiesIn(midi);
        };

        auto atFull = runPattern(0.95f);

        std::cout << "  accent 0.95: velocities";
        for (float v : atFull)
            std::cout << " " << juce::String(v, 2);
        std::cout << "   (both steps are written at 0.30)" << std::endl;

        check(atFull.size() == 2, "both steps sound");

        if (atFull.size() == 2)
        {
            check(std::abs(atFull[0] - 0.30f) < 0.02f,
                  "a step that is not accented plays at the velocity it was given");
            check(atFull[1] > 0.9f, "and an accented one plays at the pattern's accent level");
        }

        // The dial is the point: one setting retunes every accent in the
        // pattern at once, which is why accent is a flag and not just a taller
        // velocity bar someone dragged.
        auto atHalf = runPattern(0.50f);

        std::cout << "  accent 0.50: velocities";
        for (float v : atHalf)
            std::cout << " " << juce::String(v, 2);
        std::cout << std::endl;

        if (atHalf.size() == 2 && atFull.size() == 2)
        {
            check(std::abs(atHalf[0] - atFull[0]) < 0.02f,
                  "turning the accent dial down leaves unaccented steps alone");
            check(atHalf[1] < atFull[1] - 0.2f, "and moves every accented step with it");
        }

        // And it survives being saved.
        {
            auto sequencerPtr = std::make_unique<StepSequencer>();
            auto& sequencer = *sequencerPtr;
            sequencer.prepare(sampleRate);

            StepSequencer::Step step;
            step.active = true;
            step.accent = true;
            sequencer.setStep(5, step);

            auto restoredPtr = std::make_unique<StepSequencer>();
            auto& restored = *restoredPtr;
            restored.prepare(sampleRate);
            restored.restoreState(sequencer.captureState());

            check(restored.getStep(5).accent, "an accent is remembered when the pattern is saved");
            check(!restored.getStep(4).accent, "and a step without one stays without one");
        }
    }
}
} // namespace

int main()
{
    std::cout << "Sequencer test - latch, steps and automation" << std::endl << std::endl;

    // ---- Hold latch --------------------------------------------------------
    std::cout << "Hold latch:" << std::endl;
    {
        NoteLatch latch;
        SynthParameters params;
        params.holdNotes = 1.0f;

        // Three notes played and released one at a time must all stay down:
        // the old behaviour kept only the last.
        juce::MidiBuffer midi;
        for (int note : {60, 64, 67})
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
            midi.addEvent(juce::MidiMessage::noteOff(1, note), 1);
        }

        latch.process(midi, params);

        auto ons = noteOnsIn(midi);
        auto offs = noteOffsIn(midi);

        std::cout << "  played and released 60 64 67, still down: " << toText(ons) << std::endl;
        check(ons == std::vector<int>({60, 64, 67}), "every note played is started");
        check(offs.empty(), "no note-off escapes while hold is on");

        // Tapping a held note is how you take it back out of the chord.
        juce::MidiBuffer tap;
        tap.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 0);
        latch.process(tap, params);

        check(noteOnsIn(tap).empty(), "tapping a held note does not restart it");
        check(noteOffsIn(tap) == std::vector<int>({64}), "tapping a held note releases it");

        // Switching hold off has to drop what is left, or it sounds forever.
        params.holdNotes = 0.0f;
        juce::MidiBuffer release;
        latch.process(release, params);

        auto released = noteOffsIn(release);
        std::sort(released.begin(), released.end());
        std::cout << "  hold off released: " << toText(released) << std::endl;
        check(released == std::vector<int>({60, 67}), "switching hold off releases everything still held");
    }

    {
        // With hold off the stage must be a true bypass.
        NoteLatch latch;
        SynthParameters params;
        params.holdNotes = 0.0f;

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 10);
        latch.process(midi, params);

        check(noteOnsIn(midi) == std::vector<int>({60}), "hold off passes note-ons through");
        check(noteOffsIn(midi) == std::vector<int>({60}), "hold off passes note-offs through");
    }

    // ---- Steps -------------------------------------------------------------
    std::cout << std::endl << "Steps:" << std::endl;

    SynthParameters params;
    params.seqPlaying = 1.0f;
    params.seqLength = 8.0f;
    params.seqRootNote = 48.0f;
    params.musicalKey = 0.0f;
    params.musicalScale = (float) music::Scale::naturalMinor;

    {
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        // A rising run over the first four steps of the scale.
        for (int i = 0; i < 4; ++i)
        {
            StepSequencer::Step step;
            step.active = true;
            step.degree = i;
            step.gate = 0.5f;
            sequencer.setStep(i, step);
        }

        int stepSamples = samplesPerStep(params, 120.0);
        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, stepSamples * 4, params, 120.0);

        auto notes = noteOnsIn(midi);
        std::cout << "  degrees 0 1 2 3 in C minor: " << toText(notes) << std::endl;

        // C minor is C D Eb F, so degrees 0-3 from C3 are 48 50 51 53.
        check(notes == std::vector<int>({48, 50, 51, 53}), "degrees play the notes the key gives them");

        bool allInKey = true;
        for (int note : notes)
            if (!music::isInScale(note, 0, music::Scale::naturalMinor))
                allInKey = false;
        check(allInKey, "every step lands inside the key");
    }

    {
        // Changing key transposes the whole pattern and keeps it in tune - the
        // reason steps hold degrees rather than note numbers.
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        for (int i = 0; i < 4; ++i)
        {
            StepSequencer::Step step;
            step.active = true;
            step.degree = i;
            sequencer.setStep(i, step);
        }

        auto run = [&](int key, float scale)
        {
            SynthParameters local;
            local.seqPlaying = 1.0f;
            local.seqLength = 8.0f;
            local.seqRootNote = 48.0f;
            local.musicalKey = (float) key;
            local.musicalScale = scale;

            StepSequencer fresh;
            fresh.prepare(sampleRate);

            tempo::MusicalClock clock;

            clock.prepare(sampleRate);
            for (int i = 0; i < 4; ++i)
            {
                StepSequencer::Step step;
                step.active = true;
                step.degree = i;
                fresh.setStep(i, step);
            }

            juce::MidiBuffer midi;
            runBlock(fresh, clock, midi, samplesPerStep(local, 120.0) * 4, local, 120.0);
            return noteOnsIn(midi);
        };

        auto inCMinor = run(0, (float) music::Scale::naturalMinor);
        auto inGMajor = run(7, (float) music::Scale::major);

        std::cout << "  same pattern in C minor: " << toText(inCMinor) << std::endl;
        std::cout << "  same pattern in G major: " << toText(inGMajor) << std::endl;

        check(inCMinor != inGMajor, "changing key changes the notes");
        check(inCMinor.size() == inGMajor.size(), "changing key keeps the same number of notes");

        bool gMajorInKey = true;
        for (int note : inGMajor)
            if (!music::isInScale(note, 7, music::Scale::major))
                gMajorInKey = false;
        check(gMajorInKey, "the transposed pattern is in the new key");
    }

    {
        // Inactive steps stay silent, and a zero probability never fires.
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        StepSequencer::Step on;
        on.active = true;
        sequencer.setStep(0, on);

        StepSequencer::Step never;
        never.active = true;
        never.probability = 0.0f;
        sequencer.setStep(1, never);

        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, samplesPerStep(params, 120.0) * 4, params, 120.0);

        check(noteOnsIn(midi).size() == 1, "only active steps with a chance of playing sound");
    }

    {
        // Length must actually shorten the loop.
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        for (int i = 0; i < StepSequencer::maxSteps; ++i)
        {
            StepSequencer::Step step;
            step.active = true;
            sequencer.setStep(i, step);
        }

        SynthParameters shortPattern; setupParams(shortPattern);
        shortPattern.seqLength = 4.0f;

        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, samplesPerStep(shortPattern, 120.0) * 8, shortPattern, 120.0);

        std::cout << "  8 steps' worth of time at length 4: " << noteOnsIn(midi).size() << " notes" << std::endl;
        check(noteOnsIn(midi).size() == 8, "a four-step pattern repeats twice in eight steps of time");
    }

    {
        // Stopping must not leave the last note ringing.
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        StepSequencer::Step step;
        step.active = true;
        step.gate = 1.0f;
        sequencer.setStep(0, step);

        juce::MidiBuffer running;
        runBlock(sequencer, clock, running, 256, params, 120.0);
        check(!noteOnsIn(running).empty(), "the sequencer started a note");

        SynthParameters stopped; setupParams(stopped);
        stopped.seqPlaying = 0.0f;
        juce::MidiBuffer stopping;
        runBlock(sequencer, clock, stopping, 256, stopped, 120.0);

        check(!noteOffsIn(stopping).empty(), "stopping releases the note that was sounding");
    }

    // ---- Generate ----------------------------------------------------------
    std::cout << std::endl << "Generate:" << std::endl;
    {
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        sequencer.generate(8, 7, true, 16);

        int active = 0;
        std::vector<int> degrees;
        for (int i = 0; i < StepSequencer::maxSteps; ++i)
        {
            auto step = sequencer.getStep(i);
            if (step.active)
                ++active;
            degrees.push_back(step.degree);
        }

        std::cout << "  8 pulses over 16 steps gave " << active << " active steps" << std::endl;
        std::cout << "  degrees: " << toText(degrees) << std::endl;

        check(active == 8, "density asks for a number of notes and gets it");
        // Lengths now come from how far it is to the next note, so a sparse
        // pattern holds and a dense one does not. Eight notes over sixteen steps
        // means gaps of two, so nothing should be longer than that.
        check(sequencer.getStep(0).length >= StepSequencer::ticksPerStep / 4,
              "a generated note is at least a quarter of a step long");
        check(sequencer.getStep(0).length <= 2 * StepSequencer::ticksPerStep,
              "and no longer than the gap to the next one");
        check(sequencer.getStep(0).degree == 0, "the bar starts on the root when asked to resolve");

        bool inRange = true;
        for (int degree : degrees)
            if (degree < -7 || degree > 7)
                inRange = false;
        check(inRange, "generated degrees stay inside the range asked for");
    }

    {
        // Locks are what make Generate usable: keep what you like, reroll the rest.
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        StepSequencer::Step kept;
        kept.active = true;
        kept.degree = 5;
        kept.locked = true;
        sequencer.setStep(3, kept);

        bool survived = true;
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            sequencer.generate(8, 7, true, 16);
            auto step = sequencer.getStep(3);
            if (step.degree != 5 || !step.active || !step.locked)
                survived = false;
        }

        check(survived, "a locked step survives twenty regenerations");
    }

    // ---- Note length -------------------------------------------------------
    std::cout << std::endl << "Note length:" << std::endl;
    {
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        // A note four steps long must sound for four steps, not be cut at one.
        sequencer.placeNote(0, 0, 4 * StepSequencer::ticksPerStep, 0.9f);

        SynthParameters live;
        setupParams(live);
        live.seqLength = 16.0f;

        auto step = sequencer.getStep(0);
        check(step.active && step.length == 4 * StepSequencer::ticksPerStep,
              "a note remembers how long it is");

        juce::MidiBuffer midi;
        int stepSamples = samplesPerStep(live, 120.0);
        runBlock(sequencer, clock, midi, stepSamples * 4, live, 120.0);

        int onAt = -1, offAt = -1;
        for (const auto metadata : midi)
        {
            if (metadata.getMessage().isNoteOn() && onAt < 0) onAt = metadata.samplePosition;
            if (metadata.getMessage().isNoteOff() && offAt < 0) offAt = metadata.samplePosition;
        }

        double heldSteps = offAt > onAt ? (double) (offAt - onAt) / stepSamples : 0.0;
        std::cout << "  a 4-step note sounded for " << juce::String(heldSteps, 2).toStdString()
                  << " steps" << std::endl;
        check(heldSteps > 3.0, "a four-step note sounds for about four steps");
        check(heldSteps < 4.1, "and not longer than it was drawn");

        // Nothing retriggers underneath it.
        check(sequencer.getStep(1).active == false, "the steps a note covers are cleared");
        check(sequencer.noteCovering(2) == 0, "the covered steps report the note that owns them");
        check(sequencer.noteCovering(5) == -1, "steps past the note are free");
    }

    {
        // A note dropped inside another one takes the steps it needs.
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        constexpr int oneStep = StepSequencer::ticksPerStep;
        sequencer.placeNote(0, 0, 8 * oneStep, 0.9f);
        sequencer.placeNote(4, 3, 2 * oneStep, 0.9f);

        check(sequencer.getStep(0).length == 4 * oneStep,
              "a note is shortened when another starts inside it");
        check(sequencer.getStep(4).active && sequencer.getStep(4).length == 2 * oneStep,
              "the new note is placed");
        check(sequencer.noteCovering(3) == 0, "the shortened note still covers up to the new one");
    }

    {
        // Full-pattern length, which is what the request asked for.
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        sequencer.placeNote(0, 0, StepSequencer::maxTicks, 0.9f);
        check(sequencer.getStep(0).length == StepSequencer::maxTicks,
              "a note can be as long as the whole sequencer");
        check(StepSequencer::maxSteps == 64, "the sequencer is 64 steps long");

        // And as short as a sixteenth of a step, which is the other end of what
        // the tick grid buys.
        StepSequencer brief;
        brief.prepare(sampleRate);
        brief.placeNote(0, 0, 1, 0.9f);
        check(brief.getStep(0).length == 1, "and as short as a single tick");

        // What drawing one with the mouse gives you, which is not the same as
        // the shortest a note can be.
        StepSequencer drawn;
        drawn.prepare(sampleRate);
        drawn.placeNote(0, 0, 0.85f);
        check(drawn.getStep(0).length == StepSequencer::ticksPerStep,
              "a note placed without a length is one whole step");
        check(StepSequencer::ticksPerStep == 16, "a step divides into sixteen");
        check(brief.noteCovering(1) == -1, "a note shorter than its step covers nothing after it");
    }

    // ---- Capturing what is played -----------------------------------------
    std::cout << std::endl << "Recording played notes:" << std::endl;
    {
        auto playInto = [](bool quantise, int atSample, int heldSamples)
        {
            StepSequencer sequencer;
            sequencer.prepare(sampleRate);
            tempo::MusicalClock clock;
            clock.prepare(sampleRate);

            SynthParameters live;
            setupParams(live);
            live.seqLength = 16.0f;
            live.seqRecord = 1.0f;
            live.seqRecordNotes = 1.0f;
            live.seqQuantise = quantise ? 1.0f : 0.0f;

            int stepSamples = samplesPerStep(live, 120.0);

            // C3 is the pattern root, so it should land on degree 0.
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), atSample);
            midi.addEvent(juce::MidiMessage::noteOff(1, 48), atSample + heldSamples);

            runBlock(sequencer, clock, midi, stepSamples * 8, live, 120.0);

            std::vector<std::pair<int, StepSequencer::Step>> written;
            for (int i = 0; i < StepSequencer::maxSteps; ++i)
            {
                auto step = sequencer.getStep(i);
                if (step.active)
                    written.push_back({i, step});
            }
            return written;
        };

        int stepSamples = samplesPerStep(params, 120.0);

        // Played a little late in step 2: quantise should pull it back to 2.
        auto quantised = playInto(true, stepSamples * 2 + stepSamples / 5, stepSamples * 2);
        check(quantised.size() == 1, "one played note writes one step");
        if (!quantised.empty())
        {
            std::cout << "  quantised: step " << quantised[0].first
                      << ", length " << quantised[0].second.length
                      << ", offset " << quantised[0].second.tickOffset << std::endl;
            check(quantised[0].first == 2, "a slightly late note is snapped to its step");
            check(quantised[0].second.tickOffset == 0, "a quantised note sits on the step boundary");
            check(quantised[0].second.degree == 0, "playing the root writes degree zero");
            check(quantised[0].second.length == 2 * StepSequencer::ticksPerStep,
                  "the length played is the length written");
        }

        // The same performance with quantise off keeps how late it was.
        auto manual = playInto(false, stepSamples * 2 + stepSamples / 2, stepSamples * 2);
        check(manual.size() == 1, "one played note writes one step when unquantised");
        if (!manual.empty())
        {
            std::cout << "  unquantised: step " << manual[0].first
                      << ", length " << manual[0].second.length
                      << ", offset " << manual[0].second.tickOffset << std::endl;
            check(manual[0].first == 2, "an unquantised note stays in the step it was played in");
            check(manual[0].second.tickOffset > 0, "an unquantised note keeps how late it was played");
        }

        // A note played well after the beat is pulled forward by quantise, and
        // that is the whole difference between the two modes.
        auto late = playInto(true, stepSamples * 2 + (stepSamples * 4) / 5, stepSamples);
        if (!late.empty())
            check(late[0].first == 3, "a note played nearly a step late snaps to the next step");
    }

    {
        // With note capture off, playing must not touch the grid.
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        SynthParameters live;
        setupParams(live);
        live.seqRecord = 1.0f;
        live.seqRecordNotes = 0.0f;

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
        midi.addEvent(juce::MidiMessage::noteOff(1, 48), 100);
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0) * 4, live, 120.0);

        bool anythingWritten = false;
        for (int i = 0; i < StepSequencer::maxSteps; ++i)
            if (sequencer.getStep(i).active)
                anythingWritten = true;

        check(!anythingWritten, "notes are not captured when note recording is off");
    }

    // ---- Automation --------------------------------------------------------
    std::cout << std::endl << "Automation:" << std::endl;
    {
        int cutoffId = paramreg::idForName("Cutoff");
        check(cutoffId >= 0, "the registry knows the cutoff dial");

        StepSequencer sequencer;
        sequencer.prepare(sampleRate);

        tempo::MusicalClock clock;

        clock.prepare(sampleRate);

        SynthParameters live; setupParams(live);
        live.seqRecord = 1.0f;
        live.filterCutoffHz = 2000.0f;

        // Record one bar with the dial held at 2 kHz.
        sequencer.setParameterTouched(cutoffId, true);
        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0) * 16, live, 120.0);
        sequencer.setParameterTouched(cutoffId, false);

        check(sequencer.isLaneUsed(cutoffId), "moving a dial with REC armed creates its lane");
        check(sequencer.getNumUsedLanes() == 1, "only the dial that moved is recorded");

        // Now play back with the parameter moved somewhere else: the lane must
        // pull it back.
        live.seqRecord = 0.0f;
        live.filterCutoffHz = 15000.0f;

        juce::MidiBuffer playback;
        runBlock(sequencer, clock, playback, samplesPerStep(live, 120.0) * 2, live, 120.0);

        std::cout << "  recorded 2000 Hz, set to 15000, playback gave "
                  << (int) live.filterCutoffHz.load() << " Hz" << std::endl;
        check(std::abs(live.filterCutoffHz.load() - 2000.0f) < 60.0f,
              "playback puts the parameter back where it was recorded");
    }

    {
        // A hand on the dial beats the lane, so grabbing a knob during playback
        // takes over instead of fighting.
        int cutoffId = paramreg::idForName("Cutoff");

        StepSequencer sequencer;
        sequencer.prepare(sampleRate);

        tempo::MusicalClock clock;

        clock.prepare(sampleRate);

        SynthParameters live; setupParams(live);
        live.seqRecord = 1.0f;
        live.filterCutoffHz = 2000.0f;

        sequencer.setParameterTouched(cutoffId, true);
        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0) * 16, live, 120.0);

        // Still touched, still recording, but now at a different value: the
        // second pass has to overwrite the first.
        live.filterCutoffHz = 8000.0f;
        juce::MidiBuffer second;
        runBlock(sequencer, clock, second, samplesPerStep(live, 120.0) * 16, live, 120.0);
        sequencer.setParameterTouched(cutoffId, false);

        live.seqRecord = 0.0f;
        live.filterCutoffHz = 500.0f;

        juce::MidiBuffer playback;
        runBlock(sequencer, clock, playback, samplesPerStep(live, 120.0) * 2, live, 120.0);

        std::cout << "  recorded 2000, then re-recorded 8000, playback gave "
                  << (int) live.filterCutoffHz.load() << " Hz" << std::endl;
        check(std::abs(live.filterCutoffHz.load() - 8000.0f) < 250.0f,
              "recording over a lane overwrites what was there");
    }

    {
        int cutoffId = paramreg::idForName("Cutoff");

        StepSequencer sequencer;
        sequencer.prepare(sampleRate);

        tempo::MusicalClock clock;

        clock.prepare(sampleRate);

        SynthParameters live; setupParams(live);
        live.seqRecord = 1.0f;
        live.filterCutoffHz = 2000.0f;
        sequencer.setParameterTouched(cutoffId, true);
        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0) * 16, live, 120.0);

        // Held during playback: the lane must not yank it away.
        live.seqRecord = 0.0f;
        live.filterCutoffHz = 12000.0f;
        juce::MidiBuffer playback;
        runBlock(sequencer, clock, playback, samplesPerStep(live, 120.0) * 2, live, 120.0);

        check(live.filterCutoffHz.load() > 10000.0f, "a dial being held is not overwritten by its lane");

        sequencer.clearAllLanes();
        check(sequencer.getNumUsedLanes() == 0, "clearing automation empties every lane");
    }

    {
        // Every registry entry has to survive a normalised round trip, or
        // automation would drift a parameter every time it played back.
        SynthParameters live;
        bool allRoundTrip = true;

        for (int id = 0; id < paramreg::count(); ++id)
            for (float target : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
            {
                paramreg::writeNormalised(live, id, target);
                float readBack = paramreg::readNormalised(live, id);
                if (std::abs(readBack - target) > 0.001f)
                    allRoundTrip = false;
            }

        std::cout << "  " << paramreg::count() << " automatable parameters" << std::endl;
        check(allRoundTrip, "every parameter survives a normalised round trip");
        // Was "fits in one touched-bit word". There is no single word any more -
        // the touched set is a ParameterMask, as wide as the cap needs - so the
        // invariant is the one that outlives the representation: no id the
        // registry hands out may sit above the ceiling every per-parameter
        // array is sized by.
        check(paramreg::count() <= paramreg::maxParameters,
              "every registered parameter is within the cap");

        std::set<juce::String> names;
        for (int id = 0; id < paramreg::count(); ++id)
            names.insert(paramreg::name(id));
        check((int) names.size() == paramreg::count(), "parameter names are unique");

        // The ids saved automation lanes point at. This list is what those
        // files mean by 0..31, so it is pinned here: reordering the registry or
        // inserting an entry rather than appending one would repoint every lane
        // in every project saved before the change, silently and unrecoverably.
        // If this fails, the registry was edited above the append line - move
        // the new entry to the end instead of renumbering what is already out
        // in people's files.
        const char* const originalIds[] = {
            "Wave",      "Voices",    "Detune",     "Width",      "Osc1 Level", "Osc2 Level",
            "Osc2 Wave", "Osc2 Semi", "Osc2 Fine",  "Sub",        "Sub Wave",   "Noise",
            "Colour",    "Cutoff",    "Reso",       "Drive",      "Attack",     "Decay",
            "Sustain",   "Release",   "LFO Rate",   "To Filter",  "To Amp",     "To Wave",
            "LFO2 Rate", "LFO2 Wave", "LFO2 Cutoff","Distortion", "Delay Mix",  "Delay Fbk",
            "Reverb Mix","Gain"};

        bool idsHeld = true;
        for (int id = 0; id < (int) (sizeof(originalIds) / sizeof(originalIds[0])); ++id)
            if (juce::String(paramreg::name(id)) != originalIds[id])
            {
                std::cout << "    id " << id << " was \"" << originalIds[id] << "\", is now \""
                          << paramreg::name(id) << "\"" << std::endl;
                idsHeld = false;
            }

        check(idsHeld, "the first 32 ids still mean what saved lanes think they mean");

        // Every parameter belongs to a section, or the grouped chooser drops it.
        bool allGrouped = true;
        for (int id = 0; id < paramreg::count(); ++id)
            if (juce::String(paramreg::group(id)).isEmpty())
            {
                std::cout << "    \"" << paramreg::name(id) << "\" has no group" << std::endl;
                allGrouped = false;
            }

        check(allGrouped, "every parameter names the section it belongs to");
    }


    // ---- REC captures a MIDI controller, not only the mouse ----------------
    // A learned control writes the parameter on the audio thread, which the
    // dial-held set knows nothing about. If the two sets did not combine, REC
    // would sit there recording nothing while the sound visibly changed.
    std::cout << std::endl << "Recording from a controller:" << std::endl;
    {
        int resoId = paramreg::idForName("Reso");
        check(resoId >= 0, "the registry knows the resonance dial");

        StepSequencer sequencer;
        sequencer.prepare(sampleRate);

        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        SynthParameters live; setupParams(live);
        live.seqRecord = 1.0f;
        live.filterResonance = 0.8f;

        // No hand on the dial - only the flag the audio thread sets when a
        // learned controller moves the parameter.
        sequencer.setParameterDrivenByController(resoId, true);
        check(sequencer.isLaneTouched(resoId), "a controller counts as holding the parameter");

        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0) * 16, live, 120.0);
        sequencer.setParameterDrivenByController(resoId, false);

        check(sequencer.isLaneUsed(resoId), "so REC records it the same as a mouse move");

        // The two sets must not clear each other: the editor writes one from
        // the message thread every frame while the audio thread writes the
        // other, and either switching the other off would mean a sweep is
        // recorded only on the frames the two happened to agree.
        //
        // On a sequencer that has never recorded, so the latch - which
        // deliberately outlives both for the length of a pass - is not what is
        // being measured here.
        StepSequencer untouched;
        untouched.prepare(sampleRate);

        untouched.setParameterDrivenByController(resoId, true);
        check(untouched.isLaneTouched(resoId), "a controller alone marks it held");

        untouched.setParameterTouched(resoId, false);
        check(untouched.isLaneTouched(resoId), "a hand let go does not release a controller");

        untouched.setParameterTouched(resoId, true);
        untouched.setParameterDrivenByController(resoId, false);
        check(untouched.isLaneTouched(resoId), "and a controller let go does not release a hand");

        untouched.setParameterTouched(resoId, false);
        check(!untouched.isLaneTouched(resoId), "only both letting go releases it");
    }


    // ---- Latch: one move claims the rest of the pass ------------------------
    // A control is let go of the moment it stops moving. Recording that way
    // hands the parameter back to its lane between one adjustment and the next,
    // so a pass made of several plays back as the new value, then the old one,
    // then the new one - the jerkiness. Moving something once with REC armed is
    // a statement about the whole pass.
    std::cout << std::endl << "Latch while recording:" << std::endl;
    {
        int driveId = paramreg::idForName("Drive");
        check(driveId >= 0, "the registry knows the drive dial");

        StepSequencer sequencer;
        sequencer.prepare(sampleRate);

        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        SynthParameters live; setupParams(live);
        live.driveAmount = 0.2f;

        // A first pass with no hand on anything, to give the lane old values
        // for the latch to have to beat.
        live.seqRecord = 1.0f;
        sequencer.setParameterTouched(driveId, true);
        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0) * 16, live, 120.0);
        sequencer.setParameterTouched(driveId, false);
        check(sequencer.isLaneUsed(driveId), "the first pass records a lane");

        // Second pass: move it for a moment near the start, then let go
        // entirely. Everything after the release must still be recorded at the
        // new value rather than reverting to the lane.
        live.driveAmount = 0.9f;
        sequencer.setParameterTouched(driveId, true);
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0), live, 120.0);
        sequencer.setParameterTouched(driveId, false);

        check(sequencer.isLaneTouched(driveId), "letting go mid-pass leaves it latched");

        // Three steps, not eight: the pattern here is eight steps long, so a
        // full lap would land back on the ticks the move itself just wrote and
        // read 0.9 whether the latch works or not. Stopping partway lands on
        // ticks the FIRST pass owns, where the two answers differ.
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0) * 3, live, 120.0);

        check(std::abs(live.driveAmount.load() - 0.9f) < 0.01f,
              "so the lane does not pull it back while the pass runs");

        // And the pass is writing the new value into those ticks as it goes,
        // which is the point of latching rather than merely not being fought.
        check(std::abs(sequencer.getLaneValue(driveId, 2 * StepSequencer::ticksPerStep) - 0.9f) < 0.01f,
              "recording the steps it passes over at the value you left it at");

        // Disarming ends the pass, and the lane takes over again.
        live.seqRecord = 0.0f;
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0), live, 120.0);
        check(!sequencer.isLaneTouched(driveId), "disarming REC releases the latch");

        // And stopping the transport ends it too, so play does not begin with
        // half the panel already claimed.
        live.seqRecord = 1.0f;
        sequencer.setParameterTouched(driveId, true);
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0), live, 120.0);
        sequencer.setParameterTouched(driveId, false);
        check(sequencer.isLaneTouched(driveId), "a new pass latches again");

        live.seqPlaying = 0.0f;
        runBlock(sequencer, clock, midi, 512, live, 120.0);
        check(!sequencer.isLaneTouched(driveId), "and stopping releases it");
    }

    // ---- Clearing while the pass is still running ---------------------------
    // The clear buttons are pressed with REC still armed and the transport
    // still running, which is the case that used to undo itself: the latch
    // remembers that a dial was moved during this pass, so the next tick wrote
    // the lane straight back - flat, at wherever the dial was sitting. The
    // values looked cleared because they WERE all the same; the entry came back
    // because nothing had said the pass was over.
    std::cout << std::endl << "Clear while recording:" << std::endl;
    {
        int driveId = paramreg::idForName("Drive");
        int cutoffId = paramreg::idForName("Cutoff");
        check(driveId >= 0 && cutoffId >= 0, "the registry knows both dials");

        StepSequencer sequencer;
        sequencer.prepare(sampleRate);

        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        SynthParameters live; setupParams(live);
        live.driveAmount = 0.7f;
        live.seqRecord = 1.0f;

        juce::MidiBuffer midi;
        sequencer.setParameterTouched(driveId, true);
        sequencer.setParameterTouched(cutoffId, true);
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0) * 2, live, 120.0);
        sequencer.setParameterTouched(driveId, false);
        sequencer.setParameterTouched(cutoffId, false);

        check(sequencer.getNumUsedLanes() == 2, "two dials moved, two lanes recorded");

        sequencer.clearAllLanes();
        check(sequencer.getNumUsedLanes() == 0, "clearing empties them");

        // Three steps of still recording, with no hand on anything.
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0) * 3, live, 120.0);
        check(sequencer.getNumUsedLanes() == 0, "and they stay cleared while the pass runs on");

        // Moving something AFTER the clear is a new statement, and does record.
        // The clear ends the pass, it does not disarm the recording.
        live.driveAmount = 0.25f;
        sequencer.setParameterTouched(driveId, true);
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0), live, 120.0);
        sequencer.setParameterTouched(driveId, false);

        check(sequencer.isLaneUsed(driveId), "a move after the clear still records");
        check(!sequencer.isLaneUsed(cutoffId), "and only the dial that moved");

        // One lane at a time, same rule.
        sequencer.clearLane(driveId);
        check(!sequencer.isLaneUsed(driveId), "clearing one lane empties it");

        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0) * 2, live, 120.0);
        check(!sequencer.isLaneUsed(driveId), "and that one stays cleared too");
    }

    // ---- Lanes above the old ceiling ---------------------------------------
    //
    // The cap moved from 64 to 128, and ids 64 and up are where every bare
    // shift in the old code would have misbehaved: 1ull << 64 is 1 on x86, so a
    // lane at 64 would have shared its storage with the Wave dial at id 0.
    //
    // This block used to end by saying that playback at a high id could not be
    // tested, because applyAutomation walks count() and count() was 63. The cap
    // is 192 now and the kit's 108 controls are registered behind the synth's
    // 63, so count() is 171 and the gap is closed below - a lane above 127 is
    // played, and it drives a real dial.
    std::cout << std::endl << "Lanes above the old ceiling:" << std::endl;
    {
        const int highId = paramreg::maxParameters - 1;
        const int boundaryId = 64;

        check(paramreg::maxParameters > 64, "the cap is wider than one word");

        StepSequencer sequencer;
        sequencer.prepare(sampleRate);

        sequencer.setLaneValue(boundaryId, 0, 0.75f);
        sequencer.setLaneValue(highId, 0, 0.25f);

        check(sequencer.isLaneUsed(boundaryId), "a lane exists at id 64");
        check(sequencer.isLaneUsed(highId), "and at the top of the range");
        check(!sequencer.isLaneUsed(0), "and the Wave dial at id 0 has none");

        check(std::abs(sequencer.getLaneValue(boundaryId, 0) - 0.75f) < 0.001f,
              "id 64 reads back its own value");
        check(std::abs(sequencer.getLaneValue(highId, 0) - 0.25f) < 0.001f,
              "and so does the top of the range");

        // The held set, which is the mask the shift bug actually lived in.
        sequencer.setParameterTouched(boundaryId, true);
        check(sequencer.isLaneTouched(boundaryId), "id 64 can be held");
        check(!sequencer.isLaneTouched(0), "without holding id 0 with it");
        sequencer.setParameterTouched(boundaryId, false);

        // What a project file is made of.
        auto state = sequencer.captureState();

        StepSequencer restored;
        restored.prepare(sampleRate);
        restored.restoreState(state);

        check(restored.isLaneUsed(boundaryId) && restored.isLaneUsed(highId),
              "high lanes survive a capture and restore");
        check(std::abs(restored.getLaneValue(highId, 0) - 0.25f) < 0.001f,
              "with their values intact");
        check(!restored.isLaneUsed(0), "and nothing appears at id 0 that was not there");

        // Clearing one must not clear its old alias.
        restored.clearLane(boundaryId);
        check(!restored.isLaneUsed(boundaryId), "clearing id 64 empties it");
        check(restored.isLaneUsed(highId), "and leaves the other high lane alone");
    }

    // ---- A lane above 127 actually plays -----------------------------------
    //
    // The gap the block above used to name. Everything up there was storage:
    // a lane could be written, held, saved and read back at a high id, and
    // none of that proved the id could be PLAYED, because applyAutomation
    // walks the registry and the registry stopped at 63.
    //
    // The kit's controls are registered now, and the last of them sits at 170 -
    // which is in the third word of the mask, a word that did not exist until
    // the cap moved to 192. So this drives one end to end: a lane written at a
    // drum id, a bar run through the sequencer, and the value landing in the
    // slot of SynthParameters::drumControls that the panel's own dial writes to.
    //
    // Aux Amt 2 rather than a Level, deliberately. It is the LAST registered
    // parameter in the whole project, so an off-by-one anywhere between the
    // mask, the lane array and the registry walk lands on it first.
    {
        using DC = project::DrumControl;

        const int auxAmt2 = paramreg::drumParameterId(project::numDrumVoices - 1, DC::send2Amount);

        check(auxAmt2 == paramreg::count() - 1,
              "the kit's last control is the last id in the registry, at "
                  + juce::String(auxAmt2));
        check(auxAmt2 > 127, "which is above the old 128 ceiling, in the mask's third word");

        StepSequencer sequencer;
        sequencer.prepare(sampleRate);

        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        SynthParameters live;
        setupParams(live);

        // The whole bar at three quarters, so whichever tick the block lands on
        // is the tick under test. Amt 2 is a 0-1 control, so normalised and
        // real are the same number here - which is why the check below can read
        // the storage array directly and still be reading what was recorded.
        for (int tick = 0; tick < StepSequencer::maxTicks; ++tick)
            sequencer.setLaneValue(auxAmt2, tick, 0.75f);

        const auto storage = (size_t) project::drumControlIndex(project::numDrumVoices - 1,
                                                                DC::send2Amount);
        live.drumControls[storage].store(0.1f);

        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, samplesPerStep(live, 120.0) * 2, live, 120.0);

        std::cout << "  lane at id " << auxAmt2 << " held 0.75, dial set to 0.10, playback gave "
                  << live.drumControls[storage].load() << std::endl;

        check(std::abs(live.drumControls[storage].load() - 0.75f) < 0.01f,
              "a lane at the top of the registry plays into the parameter it names");

        // And it is that parameter and no other. A single-word shift would have
        // put this id onto id & 63, which lands in the synth's own section -
        // exactly the kind of fault that reads as an unrelated bug in a part of
        // the instrument nobody was automating.
        //
        // Set to something the lane's 0.75 would be visible against, then
        // checked after the bar rather than before: "no lane exists there" is
        // what the sequencer thinks, and this is what the parameter block says.
        const int alias = auxAmt2 & 63;
        const float aliasBefore = 0.3f;
        paramreg::writeNormalised(live, alias, aliasBefore);

        juce::MidiBuffer second;
        runBlock(sequencer, clock, second, samplesPerStep(live, 120.0) * 2, live, 120.0);

        check(!sequencer.isLaneUsed(alias),
              "no lane appeared at id " + juce::String(alias)
                  + ", where a single-word shift would have put this one");
        check(std::abs(paramreg::readNormalised(live, alias) - aliasBefore) < 0.001f,
              juce::String(paramreg::name(alias)) + " at id " + juce::String(alias)
                  + " is where it was left, not where the lane was");
    }

    // ---- Which lanes each grid shows ---------------------------------------
    //
    // The strip under the grid shows the lanes for the grid that is up, with an
    // override for all of them. What matters here is not the drawing - it is
    // that the strip and the Clear button agree, because Clear removes what is
    // SHOWN, and a Clear that reached a hidden lane would delete automation
    // nobody was looking at. Both ask LaneFilter.h, so this tests that.
    std::cout << std::endl << "Lanes by view:" << std::endl;
    {
        // The partition underneath it all. Every id is exactly one of the two,
        // and the drum side is exactly the kit's controls.
        int drumIds = 0;
        bool synthStartIsSynth = !paramreg::isDrumParameter(0);

        for (int id = 0; id < paramreg::count(); ++id)
            if (paramreg::isDrumParameter(id))
                ++drumIds;

        check(drumIds == project::numDrumParameters,
              "the registry splits into the kit's " + juce::String(project::numDrumParameters)
                  + " and the synth's " + juce::String(paramreg::count() - drumIds));
        check(synthStartIsSynth && paramreg::isDrumParameter(paramreg::firstDrumParameterId())
                  && !paramreg::isDrumParameter(paramreg::firstDrumParameterId() - 1),
              "and the boundary falls where the kit's block begins");
        check(!paramreg::isDrumParameter(-1) && !paramreg::isDrumParameter(paramreg::count()),
              "an id outside the registry is neither");

        const int cutoff = paramreg::idForName("Cutoff");
        const int wave = paramreg::idForName("Wave");
        const int kickLevel = paramreg::drumParameterId(0, project::DrumControl::level);

        check(laneBelongsTo(cutoff, LaneView::synth) && !laneBelongsTo(cutoff, LaneView::drums),
              "Cutoff belongs under the note grid and not the drum grid");
        check(laneBelongsTo(kickLevel, LaneView::drums) && !laneBelongsTo(kickLevel, LaneView::synth),
              "Kick Level the other way round");
        check(laneBelongsTo(cutoff, LaneView::all) && laneBelongsTo(kickLevel, LaneView::all),
              "and the override shows both");

        // Two synth lanes and one drum lane, created out of registry order so
        // the order check below means something.
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);

        SynthParameters params;
        setupParams(params);

        sequencer.createLane(kickLevel, params);
        sequencer.createLane(cutoff, params);
        sequencer.createLane(wave, params);

        const auto synthShown = shownLanes(sequencer, LaneView::synth);
        const auto drumsShown = shownLanes(sequencer, LaneView::drums);

        check(synthShown.size() == 2 && hiddenLaneCount(sequencer, LaneView::synth) == 1,
              "under the note grid: two lanes shown, one hidden");
        check(drumsShown.size() == 1 && drumsShown[0] == kickLevel
                  && hiddenLaneCount(sequencer, LaneView::drums) == 2,
              "under the drum grid: the Kick's one shown, two hidden");
        check(shownLanes(sequencer, LaneView::all).size() == 3
                  && hiddenLaneCount(sequencer, LaneView::all) == 0,
              "with the override: all three, and nothing hidden");

        // Registry order, whatever order the lanes were made in - which is the
        // order the strip draws them, so it does not reshuffle between visits.
        check(synthShown[0] == wave && synthShown[1] == cutoff,
              "in registry order rather than the order they were made");

        // What the Clear button does under the drum grid: remove what is shown,
        // and nothing else.
        for (const auto id : drumsShown)
            sequencer.clearLane(id);

        check(!sequencer.isLaneUsed(kickLevel), "clearing under the drum grid removes the drum lane");
        check(sequencer.isLaneUsed(cutoff) && sequencer.isLaneUsed(wave),
              "and leaves both synth lanes it was not showing");
        check(shownLanes(sequencer, LaneView::drums).empty()
                  && hiddenLaneCount(sequencer, LaneView::drums) == 2,
              "so the drum view is empty and still counts the two it hides");
        check(hiddenLaneCount(sequencer, LaneView::synth) == 0,
              "and the note grid has nothing hidden any more");
    }

    // ---- Chance and octave, the two the grid now lets you set --------------
    std::cout << std::endl << "Chance and octave:" << std::endl;
    {
        StepSequencer sequencer;
        sequencer.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        SynthParameters params;
        setupParams(params);
        params.seqLength = 4.0f;

        // A step that never sounds, and one that always does.
        StepSequencer::Step never;
        never.active = true;
        never.degree = 0;
        never.probability = 0.0f;
        sequencer.setStep(0, never);

        StepSequencer::Step always;
        always.active = true;
        always.degree = 0;
        always.probability = 1.0f;
        sequencer.setStep(2, always);

        juce::MidiBuffer midi;
        runBlock(sequencer, clock, midi, samplesPerStep(params, 120.0) * 16, params, 120.0);

        auto played = noteOnsIn(midi);
        std::cout << "  over four passes, " << (int) played.size()
                  << " notes from a pattern with one certain step and one at zero chance" << std::endl;
        check(!played.empty(), "the certain step sounded");
        check((int) played.size() <= 4, "the step at zero chance never did");

        // An octave shift moves the note by exactly twelve semitones.
        StepSequencer plain, shifted;
        plain.prepare(sampleRate);
        shifted.prepare(sampleRate);

        StepSequencer::Step note;
        note.active = true;
        note.degree = 0;
        plain.setStep(0, note);

        note.octave = 1;
        shifted.setStep(0, note);

        SynthParameters one, two;
        setupParams(one);
        setupParams(two);
        one.seqLength = 2.0f;
        two.seqLength = 2.0f;

        tempo::MusicalClock clockA, clockB;
        clockA.prepare(sampleRate);
        clockB.prepare(sampleRate);

        juce::MidiBuffer plainMidi, shiftedMidi;
        runBlock(plain, clockA, plainMidi, samplesPerStep(one, 120.0), one, 120.0);
        runBlock(shifted, clockB, shiftedMidi, samplesPerStep(two, 120.0), two, 120.0);

        auto low = noteOnsIn(plainMidi);
        auto high = noteOnsIn(shiftedMidi);

        check(!low.empty() && !high.empty(), "both patterns played");
        if (!low.empty() && !high.empty())
        {
            std::cout << "  same step at octave 0 plays " << low[0] << ", at +1 plays " << high[0]
                      << std::endl;
            check(high[0] - low[0] == 12, "one octave up is exactly twelve semitones");
        }
    }

    // ---- Mono and legato ---------------------------------------------------
    std::cout << std::endl << "Mono and legato:" << std::endl;
    {
        SynthParameters params;
        params.monoMode = 1.0f;
        params.legatoMode = 0.0f;

        MonoVoice mono;

        // Two keys pressed in turn: the first should be released as the second
        // takes over, so only one note is ever down.
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 10);
        mono.process(midi, params);

        auto ons = noteOnsIn(midi);
        auto offs = noteOffsIn(midi);
        std::cout << "  retrigger: note-ons " << toText(ons) << ", note-offs " << toText(offs)
                  << std::endl;
        check(ons.size() == 2 && offs.size() == 1 && offs[0] == 60,
              "the older note is released as the newer one takes over");

        // Releasing the newer one falls back to the key still held.
        juce::MidiBuffer release;
        release.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
        mono.process(release, params);

        auto backOns = noteOnsIn(release);
        std::cout << "  releasing the top note returns to " << toText(backOns) << std::endl;
        check(backOns.size() == 1 && backOns[0] == 60, "and the line falls back to the held key");
    }

    {
        SynthParameters params;
        params.monoMode = 1.0f;
        params.legatoMode = 1.0f;

        MonoVoice mono;

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.8f), 10);
        mono.process(midi, params);

        auto ons = noteOnsIn(midi);
        auto offs = noteOffsIn(midi);
        std::cout << "  legato: note-ons " << toText(ons) << ", note-offs " << toText(offs)
                  << ", target " << (int) params.monoTargetNote.load() << std::endl;

        check(ons.size() == 1 && ons[0] == 60, "an overlapping note starts no second note");
        check(offs.empty(), "and releases nothing, so the envelope carries on");
        check((int) params.monoTargetNote.load() == 67,
              "the sounding note is told to slide to the new pitch instead");

        // Letting the second go while the first is held slides back.
        juce::MidiBuffer release;
        release.addEvent(juce::MidiMessage::noteOff(1, 67), 0);
        mono.process(release, params);

        check(noteOnsIn(release).empty() && noteOffsIn(release).empty(),
              "and letting it go slides back rather than retriggering");
        check((int) params.monoTargetNote.load() == 60, "back to the key still held");
    }

    // ---- Undo history ------------------------------------------------------
    std::cout << std::endl << "Undo:" << std::endl;
    {
        UndoHistory history;

        auto stateNamed = [](const char* name, float cutoff)
        {
            PresetValues v;
            v.name = name;
            v.filterCutoffHz = cutoff;
            return v;
        };

        check(!history.canUndo() && !history.canRedo(), "a fresh history has nothing to step through");

        // Two edits, remembering how things were before each.
        history.record(stateNamed("first", 100.0f), nullptr, "Generate");
        history.record(stateNamed("second", 200.0f), nullptr, "Clear Steps");

        check(history.canUndo(), "there is something to undo");
        check(history.undoName() == "Clear Steps", "and it is named after the last thing done");

        UndoHistory::Snapshot back;
        auto now = stateNamed("now", 300.0f);

        check(history.undo(now, nullptr, back), "undo steps back");
        check(back.state.filterCutoffHz == 200.0f, "to how things were before the last action");
        check(history.canRedo() && history.redoName() == "Clear Steps",
              "and what was stepped away from can be redone");

        // Stepping back again through the same object, which is how a caller
        // naturally walks the history - the destination is also the current
        // state, and both have to be read correctly.
        check(history.undo(back.state, nullptr, back), "undo steps back again");
        check(back.state.filterCutoffHz == 100.0f, "to before the one before that");
        check(!history.canUndo(), "and then there is no more history");

        UndoHistory::Snapshot forward;
        check(history.redo(back.state, nullptr, forward), "redo steps forward");
        check(forward.state.filterCutoffHz == 200.0f, "to where undo came from");

        // Doing something new throws away the redo pile, because redo would
        // otherwise jump to a state that no longer follows from here.
        history.record(stateNamed("third", 400.0f), nullptr, "Edit Pattern");
        check(!history.canRedo(), "a new action abandons what was undone");
        check(history.undoName() == "Edit Pattern", "and becomes the thing undo reverses");

        std::cout << "  stepped back through " << history.getDepth() << " remembered states"
                  << std::endl;
    }

    {
        // The history has to stop growing somewhere, and drop the oldest rather
        // than refuse the newest.
        UndoHistory history;
        for (int i = 0; i < 200; ++i)
        {
            PresetValues v;
            v.filterCutoffHz = (float) i;
            history.record(v, nullptr, "Edit Pattern");
        }

        std::cout << "  after two hundred edits the history holds " << history.getDepth()
                  << std::endl;
        check(history.getDepth() >= 20, "a long session keeps at least twenty steps of history");
        check(history.getDepth() <= 64, "but not without limit");

        UndoHistory::Snapshot back;
        PresetValues current;
        check(history.undo(current, nullptr, back), "and the newest edit is still the first undone");
        check(back.state.filterCutoffHz == 199.0f, "not the oldest");
    }

    slideAndAccentSection();
    drumHitSection();

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL SEQUENCER TESTS PASSED" << std::endl;
    else
        std::cout << failures << " FAILURE(S)" << std::endl;

    return failures == 0 ? 0 : 1;
}
