// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Arpeggiator.h"
#include <iostream>
#include <map>
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
constexpr double stepsPerSecond = 10.0;

// A four-note chord, so every pattern has enough notes to show its shape.
const std::vector<int> chordNotes{48, 52, 55, 59};

// Runs a block the way the processor does: one advance of the shared clock,
// then the arpeggiator reads its step positions off it.
void runBlock(Arpeggiator& arp, tempo::MusicalClock& clock, juce::MidiBuffer& midi,
              int numSamples, const SynthParameters& params, double bpm)
{
    clock.beginBlock(bpm, numSamples);
    arp.process(midi, numSamples, params, clock, bpm);
}

struct Step
{
    int samplePosition = 0;
    std::vector<int> notes; // degrees within the pool, low to high
};

// Runs one mode and reports the steps it produced, as chord degrees rather
// than MIDI numbers so the expected patterns read as 0,1,2,3.
std::vector<Step> runMode(int mode, int octaves, int numSteps)
{
    Arpeggiator arp;
    arp.prepare(sampleRate);
    tempo::MusicalClock clock;
    clock.prepare(sampleRate);

    SynthParameters params;
    params.arpEnabled = 1.0f;
    params.arpSync = 0.0f; // free running, so the step length is exactly known
    params.arpMode = (float) mode;
    params.arpRateHz = (float) stepsPerSecond;
    params.arpOctaves = (float) octaves;
    params.arpGate = 0.5f;

    // The pool a pitch-ordered mode walks: the chord, spread across octaves.
    std::map<int, int> degreeOf;
    {
        std::vector<int> pool;
        for (int octave = 0; octave < octaves; ++octave)
            for (int note : chordNotes)
                pool.push_back(note + octave * 12);
        std::sort(pool.begin(), pool.end());
        for (int i = 0; i < (int) pool.size(); ++i)
            degreeOf[pool[(size_t) i]] = i;
    }

    juce::MidiBuffer midi;
    for (int note : chordNotes)
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);

    int totalSamples = (int) (sampleRate * (numSteps + 1) / stepsPerSecond);
    runBlock(arp, clock, midi, totalSamples, params, 120.0);

    std::vector<Step> steps;
    for (const auto metadata : midi)
    {
        auto message = metadata.getMessage();
        if (!message.isNoteOn())
            continue;

        auto found = degreeOf.find(message.getNoteNumber());
        int degree = found != degreeOf.end() ? found->second : -1;

        // Notes landing on the same sample are one block chord, not two steps.
        if (!steps.empty() && steps.back().samplePosition == metadata.samplePosition)
            steps.back().notes.push_back(degree);
        else
            steps.push_back({metadata.samplePosition, {degree}});
    }

    return steps;
}

std::vector<int> degreeSequence(int mode, int numSteps, int octaves = 1)
{
    std::vector<int> flat;
    for (const auto& step : runMode(mode, octaves, numSteps))
        for (int degree : step.notes)
            flat.push_back(degree);

    if ((int) flat.size() > numSteps)
        flat.resize((size_t) numSteps);

    return flat;
}

juce::String toText(const std::vector<int>& values)
{
    juce::String text;
    for (size_t i = 0; i < values.size(); ++i)
        text << (i > 0 ? " " : "") << values[i];
    return text;
}

void expectSequence(Arpeggiator::Mode mode, const std::vector<int>& expected)
{
    auto actual = degreeSequence((int) mode, (int) expected.size());
    bool matches = (actual == expected);

    std::cout << "  " << juce::String(Arpeggiator::modeName((int) mode)).paddedRight(' ', 15)
              << toText(actual);

    if (!matches)
        std::cout << "   <-- expected " << toText(expected);

    std::cout << std::endl;
    check(matches, juce::String(Arpeggiator::modeName((int) mode)) + " pattern");
}
} // namespace

int main()
{
    std::cout << "Arpeggiator pattern test - chord C3 E3 G3 B3, degrees 0..3" << std::endl << std::endl;

    std::cout << "Deterministic patterns:" << std::endl;
    using Mode = Arpeggiator::Mode;

    expectSequence(Mode::up,              {0, 1, 2, 3, 0, 1, 2, 3});
    expectSequence(Mode::down,            {3, 2, 1, 0, 3, 2, 1, 0});
    expectSequence(Mode::upDown,          {0, 1, 2, 3, 2, 1, 0, 1});
    expectSequence(Mode::downUp,          {3, 2, 1, 0, 1, 2, 3, 2});
    expectSequence(Mode::upAndDown,       {0, 1, 2, 3, 3, 2, 1, 0});
    expectSequence(Mode::downAndUp,       {3, 2, 1, 0, 0, 1, 2, 3});
    expectSequence(Mode::upTwice,         {0, 0, 1, 1, 2, 2, 3, 3});
    expectSequence(Mode::downTwice,       {3, 3, 2, 2, 1, 1, 0, 0});
    expectSequence(Mode::upDownTwice,     {0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 1, 1});
    expectSequence(Mode::cascade,         {0, 1, 2, 1, 2, 3, 2, 3, 0, 3, 0, 1});
    expectSequence(Mode::converge,        {0, 3, 1, 2, 0, 3, 1, 2});
    expectSequence(Mode::diverge,         {2, 1, 3, 0, 2, 1, 3, 0});
    expectSequence(Mode::convergeDiverge, {0, 3, 1, 2, 1, 3, 0, 3});
    expectSequence(Mode::pinkyUp,         {0, 3, 1, 3, 2, 3, 0, 3});
    expectSequence(Mode::pinkyUpDown,     {0, 3, 1, 3, 2, 3, 1, 3, 0, 3});
    expectSequence(Mode::thumbUp,         {0, 1, 0, 2, 0, 3, 0, 1});
    expectSequence(Mode::thumbUpDown,     {0, 1, 0, 2, 0, 3, 0, 2, 0, 1});
    expectSequence(Mode::playOrder,       {0, 1, 2, 3, 0, 1, 2, 3});
    expectSequence(Mode::reversePlayOrder,{3, 2, 1, 0, 3, 2, 1, 0});

    // Pressed at the same instant, arrival order and pitch order are the same,
    // so the two order-of-play modes need keys pressed out of pitch order to
    // show they are really following the playing and not the pitch.
    std::cout << std::endl << "Order of play (pressed G3, C3, B3, E3):" << std::endl;
    {
        const std::vector<int> pressOrder{55, 48, 59, 52};

        auto runPressed = [&pressOrder](Arpeggiator::Mode mode, int numSteps)
        {
            Arpeggiator arp;
            arp.prepare(sampleRate);
            tempo::MusicalClock clock;
            clock.prepare(sampleRate);

            SynthParameters params;
            params.arpEnabled = 1.0f;
            params.arpSync = 0.0f;
            params.arpMode = (float) mode;
            params.arpRateHz = (float) stepsPerSecond;
            params.arpOctaves = 1.0f;
            params.arpGate = 0.5f;

            // One key per block, so each arrives at a distinct moment.
            for (int note : pressOrder)
            {
                juce::MidiBuffer press;
                press.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
                runBlock(arp, clock, press, 64, params, 120.0);
            }

            juce::MidiBuffer midi;
            runBlock(arp, clock, midi, (int) (sampleRate * (numSteps + 1) / stepsPerSecond), params, 120.0);

            std::vector<int> notes;
            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                    notes.push_back(metadata.getMessage().getNoteNumber());

            if ((int) notes.size() > numSteps)
                notes.resize((size_t) numSteps);

            return notes;
        };

        auto played = runPressed(Mode::playOrder, 4);
        std::cout << "  Play Order     " << toText(played) << std::endl;
        check(played == pressOrder, "play order follows the order the keys were pressed");

        auto reversed = runPressed(Mode::reversePlayOrder, 4);
        std::cout << "  Reverse Order  " << toText(reversed) << std::endl;
        check(reversed == std::vector<int>({52, 59, 48, 55}), "reverse order undoes the order played");

        // A pitch-ordered mode pressed the same way must ignore the arrival
        // order entirely.
        auto climbed = runPressed(Mode::up, 4);
        std::cout << "  Up             " << toText(climbed) << std::endl;
        check(climbed == std::vector<int>({48, 52, 55, 59}), "up ignores the order played");
    }

    std::cout << std::endl << "Octave spread (Up, 2 octaves):" << std::endl;
    {
        auto twoOctaves = degreeSequence((int) Mode::up, 8, 2);
        std::cout << "  " << toText(twoOctaves) << std::endl;
        check(twoOctaves == std::vector<int>({0, 1, 2, 3, 4, 5, 6, 7}), "up spans both octaves");
    }

    std::cout << std::endl << "Chord:" << std::endl;
    {
        auto steps = runMode((int) Mode::chord, 1, 4);
        check(steps.size() >= 3, "chord produces repeating steps");
        if (!steps.empty())
        {
            std::cout << "  every step holds " << steps.front().notes.size() << " notes" << std::endl;
            check(steps.front().notes.size() == chordNotes.size(), "chord plays all held notes at once");
        }
    }

    std::cout << std::endl << "Random families (statistical, not fixed):" << std::endl;
    {
        auto once = degreeSequence((int) Mode::randomOnce, 12);
        std::cout << "  Random Once    " << toText(once) << std::endl;
        bool repeats = once.size() >= 8;
        for (size_t i = 0; i + 4 < once.size() && repeats; ++i)
            repeats = (once[i] == once[i + 4]);
        check(repeats, "random once repeats the same order every cycle");

        // The chosen order must still be a permutation of the whole chord.
        std::vector<int> firstCycle(once.begin(), once.begin() + 4);
        std::sort(firstCycle.begin(), firstCycle.end());
        check(firstCycle == std::vector<int>({0, 1, 2, 3}), "random once uses every note");
    }

    {
        auto other = degreeSequence((int) Mode::randomOther, 16);
        std::cout << "  Random Other   " << toText(other) << std::endl;

        bool allCyclesComplete = true;
        for (size_t block = 0; block + 4 <= other.size(); block += 4)
        {
            std::vector<int> cycle(other.begin() + (long) block, other.begin() + (long) block + 4);
            std::sort(cycle.begin(), cycle.end());
            if (cycle != std::vector<int>({0, 1, 2, 3}))
                allCyclesComplete = false;
        }
        check(allCyclesComplete, "random other plays every note before repeating");

        bool noImmediateRepeat = true;
        for (size_t i = 1; i < other.size(); ++i)
            if (other[i] == other[i - 1])
                noImmediateRepeat = false;
        check(noImmediateRepeat, "random other never repeats a note back to back");
    }

    {
        auto walk = degreeSequence((int) Mode::randomWalk, 20);
        std::cout << "  Random Walk    " << toText(walk) << std::endl;

        bool inRange = true;
        for (int degree : walk)
            if (degree < 0 || degree > 3)
                inRange = false;
        check(inRange, "random walk stays within the chord");
    }

    {
        auto plain = degreeSequence((int) Mode::random, 20);
        std::cout << "  Random         " << toText(plain) << std::endl;

        bool inRange = true;
        for (int degree : plain)
            if (degree < 0 || degree > 3)
                inRange = false;
        check(inRange, "random stays within the chord");
    }

    // ---- Rhythm patterns ---------------------------------------------------
    // The rhythm is the second axis: the mode says which note comes next, the
    // pattern says on which steps, and whether they rest, tie, accent, ratchet
    // or jump an octave.
    std::cout << std::endl << "Rhythm patterns:" << std::endl;

    // One entry per step that sounded, so rests show up as gaps in the numbering.
    struct PatternHit
    {
        int step = 0;
        std::vector<int> notes;
        float velocity = 0.0f;
        int lengthInSamples = 0;
    };

    auto runPattern = [](int pattern, Arpeggiator::Mode mode, int numSteps, float swing = 0.0f)
    {
        Arpeggiator arp;
        arp.prepare(sampleRate);
        tempo::MusicalClock clock;
        clock.prepare(sampleRate);

        SynthParameters params;
        params.arpEnabled = 1.0f;
        params.arpSync = 0.0f;
        params.arpMode = (float) mode;
        params.arpPattern = (float) pattern;
        params.arpRateHz = (float) stepsPerSecond;
        params.arpOctaves = 1.0f;
        params.arpGate = 0.5f;
        params.arpSwing = swing;

        juce::MidiBuffer midi;
        for (int note : chordNotes)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);

        const double stepSamples = sampleRate / stepsPerSecond;
        runBlock(arp, clock, midi, (int) (stepSamples * (numSteps + 0.5)), params, 120.0);

        std::vector<PatternHit> hits;
        std::map<int, int> startOfNote; // note -> sample it began at, to measure its length

        for (const auto metadata : midi)
        {
            auto message = metadata.getMessage();
            int step = (int) std::llround(metadata.samplePosition / stepSamples);

            if (message.isNoteOn())
            {
                if (!hits.empty() && hits.back().step == step)
                {
                    hits.back().notes.push_back(message.getNoteNumber());
                }
                else
                {
                    PatternHit hit;
                    hit.step = step;
                    hit.notes.push_back(message.getNoteNumber());
                    hit.velocity = message.getFloatVelocity();
                    hits.push_back(hit);
                }

                startOfNote[message.getNoteNumber()] = metadata.samplePosition;
            }
            else if (message.isNoteOff())
            {
                auto found = startOfNote.find(message.getNoteNumber());
                if (found == startOfNote.end())
                    continue;

                // Attribute the length back to the hit that started this note.
                for (auto& hit : hits)
                    if (hit.notes.front() == message.getNoteNumber()
                        && hit.lengthInSamples == 0
                        && startOfNote[message.getNoteNumber()] >= 0)
                    {
                        hit.lengthInSamples = metadata.samplePosition - found->second;
                        break;
                    }
            }
        }

        return hits;
    };

    // Every pattern must sound on exactly the steps its map says it does.
    {
        int checked = 0;
        for (int pattern = 0; pattern < Arpeggiator::numPatterns(); ++pattern)
        {
            juce::String steps(Arpeggiator::patternSteps(pattern));
            juce::String name(Arpeggiator::patternName(pattern));

            check(steps.isNotEmpty(), name + " has a step map");
            check(steps.length() == 8 || steps.length() == 16,
                  name + " is a musical length (got " + juce::String(steps.length()) + ")");

            for (int i = 0; i < steps.length(); ++i)
                check(juce::String(".-xXo^v=*?").containsChar(steps[i]),
                      name + " uses a known step character");

            std::vector<int> expected;
            for (int i = 0; i < steps.length(); ++i)
                if (steps[i] != '.' && steps[i] != '-')
                    expected.push_back(i);

            std::vector<int> actual;
            for (const auto& hit : runPattern(pattern, Mode::up, steps.length()))
                if (hit.step < steps.length())
                    actual.push_back(hit.step);

            check(actual == expected, name + " sounds on exactly the steps in its map");
            ++checked;
        }
        std::cout << "  " << checked << " patterns match their step maps" << std::endl;
    }

    // The individual step characters have to do what the alphabet claims.
    {
        auto findPattern = [](const juce::String& wanted)
        {
            for (int i = 0; i < Arpeggiator::numPatterns(); ++i)
                if (wanted == Arpeggiator::patternName(i))
                    return i;
            return -1;
        };

        // 'x' - straight through, one note per step, in mode order.
        {
            int pattern = findPattern("Straight");
            check(pattern >= 0, "Straight pattern exists");
            auto hits = runPattern(pattern, Mode::up, 4);
            std::vector<int> notes;
            for (const auto& hit : hits)
                notes.push_back(hit.notes.front());
            notes.resize(juce::jmin(notes.size(), chordNotes.size()));
            check(notes == chordNotes, "plain steps follow the mode's note order");
        }

        // '.' - a rest leaves its step silent.
        {
            int pattern = findPattern("Off Beat"); // ".x.x.x.x"
            auto hits = runPattern(pattern, Mode::up, 8);
            bool allOdd = !hits.empty();
            for (const auto& hit : hits)
                if (hit.step % 2 == 0)
                    allOdd = false;
            check(allOdd, "rests leave their step silent");
        }

        // '=' - a ratchet restrikes the same pitch instead of advancing.
        {
            int pattern = findPattern("Double Up"); // "x=x=x=x="
            auto hits = runPattern(pattern, Mode::up, 8);
            check(hits.size() >= 4, "double up sounds on every step");
            bool paired = hits.size() >= 4;
            for (size_t i = 0; i + 1 < hits.size() && paired; i += 2)
                if (hits[i].notes.front() != hits[i + 1].notes.front())
                    paired = false;
            check(paired, "a ratchet step repeats the note before it");
        }

        // '^' - an octave jump shifts the note it would otherwise have played.
        {
            int pattern = findPattern("Octave Jump"); // "x^x^x^x^"
            auto hits = runPattern(pattern, Mode::up, 4);
            check(hits.size() >= 4, "octave jump sounds on every step");
            if (hits.size() >= 4)
            {
                std::vector<int> notes;
                for (int i = 0; i < 4; ++i)
                    notes.push_back(hits[(size_t) i].notes.front());
                check(notes == std::vector<int>({48, 52 + 12, 55, 59 + 12}),
                      "an octave step lifts the note it plays by twelve");
            }
        }

        // '*' - a chord step plays the whole pool at once.
        {
            int pattern = findPattern("Chord Punch"); // "*.x.*.x."
            auto hits = runPattern(pattern, Mode::up, 8);
            check(!hits.empty() && hits.front().notes.size() == chordNotes.size(),
                  "a chord step plays every held note together");
            bool singleBetween = hits.size() > 1 && hits[1].notes.size() == 1;
            check(singleBetween, "single notes still fall between the chords");
        }

        // '-' - a tie holds the note through the next step instead of restriking.
        {
            int tied = findPattern("Long Short");  // "x-x.x-x."
            int plain = findPattern("Straight");
            auto tiedHits = runPattern(tied, Mode::up, 8);
            auto plainHits = runPattern(plain, Mode::up, 8);

            check(!tiedHits.empty() && !plainHits.empty(), "tie comparison produced notes");
            if (!tiedHits.empty() && !plainHits.empty())
            {
                std::cout << "  tied note " << tiedHits.front().lengthInSamples
                          << " samples vs untied " << plainHits.front().lengthInSamples << std::endl;
                check(tiedHits.front().lengthInSamples > plainHits.front().lengthInSamples,
                      "a tie makes the note it follows longer");
            }
        }

        // 'X' - an accent is louder than a plain step.
        {
            int pattern = findPattern("Stab"); // "Xxx.Xxx."
            auto hits = runPattern(pattern, Mode::up, 8);
            check(hits.size() >= 2 && hits[0].velocity > hits[1].velocity,
                  "an accented step is played harder than a plain one");
        }
    }

    // Swing delays every second step and pulls the next one back, so a pair
    // still spans two steps and the pattern holds its place against the tempo.
    {
        auto straightHits = runPattern(0, Mode::up, 4, 0.0f);
        auto swungHits = runPattern(0, Mode::up, 4, 0.6f);

        check(straightHits.size() >= 3 && swungHits.size() >= 3, "swing comparison produced notes");
        if (straightHits.size() >= 3 && swungHits.size() >= 3)
        {
            std::cout << "  swing off/on, step 2 at the same place: "
                      << (straightHits[2].step == swungHits[2].step ? "yes" : "no") << std::endl;
            check(straightHits[2].step == swungHits[2].step, "swing does not drift against the tempo");
        }
    }

    std::cout << std::endl << "Every mode has a name and a description:" << std::endl;
    for (int mode = 0; mode < Arpeggiator::numModes; ++mode)
    {
        juce::String name(Arpeggiator::modeName(mode));
        juce::String description(Arpeggiator::modeDescription(mode));
        check(name.isNotEmpty(), "mode " + juce::String(mode) + " has a name");
        check(description.isNotEmpty(), name + " has a description");
        check(!description.containsAnyOf("\xe2\xc2"), name + " description is plain ASCII");
    }
    std::cout << "  " << Arpeggiator::numModes << " modes checked" << std::endl;

    std::cout << std::endl << "Every pattern has a name and a description:" << std::endl;
    for (int pattern = 0; pattern < Arpeggiator::numPatterns(); ++pattern)
    {
        juce::String name(Arpeggiator::patternName(pattern));
        juce::String description(Arpeggiator::patternDescription(pattern));
        check(name.isNotEmpty(), "pattern " + juce::String(pattern) + " has a name");
        check(description.isNotEmpty(), name + " has a description");
        check(!description.containsAnyOf("\xe2\xc2"), name + " description is plain ASCII");
    }
    std::cout << "  " << Arpeggiator::numPatterns() << " patterns checked" << std::endl;

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL ARPEGGIATOR PATTERN TESTS PASSED" << std::endl;
    else
        std::cout << failures << " FAILURE(S)" << std::endl;

    return failures == 0 ? 0 : 1;
}
