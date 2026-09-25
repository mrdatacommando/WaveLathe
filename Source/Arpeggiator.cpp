// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Arpeggiator.h"
#include <algorithm>
#include <cstring>

namespace wavelathe
{
namespace
{
// A chord pattern spread over three octaves can ask for more notes than the
// synth has voices, so the block chord is capped.
constexpr int maxChordNotes = 8;

const char* const modeNames[] = {
    "Up", "Down", "Up-Down", "Down-Up", "Up & Down", "Down & Up",
    "Up x2", "Down x2", "Up-Down x2", "Cascade",
    "Converge", "Diverge", "Con-Diverge",
    "Pinky Up", "Pinky Up-Down", "Thumb Up", "Thumb Up-Down",
    "Play Order", "Reverse Order", "Chord",
    "Random Once", "Random", "Random Other", "Random Walk"
};

const char* const modeDescriptions[] = {
    "Lowest note to highest, then repeat.",
    "Highest note to lowest, then repeat.",
    "Up then back down, turning without repeating the end notes.",
    "Down then back up, turning without repeating the end notes.",
    "Up then down, playing the top and bottom notes twice at the turn.",
    "Down then up, playing the top and bottom notes twice at the turn.",
    "Up, with every note played twice.",
    "Down, with every note played twice.",
    "Up and down, with every note played twice.",
    "Rolling three-note runs, each starting one note higher.",
    "Outside in - lowest, highest, then working toward the middle.",
    "Inside out - middle notes first, working toward the extremes.",
    "Outside in, then back inside out.",
    "The top note on every other step, the rest climbing between.",
    "The top note on every other step, the rest going up then down.",
    "The bottom note on every other step, the rest climbing between.",
    "The bottom note on every other step, the rest going up then down.",
    "In the order you pressed the keys.",
    "In the reverse of the order you pressed the keys.",
    "All held notes together, retriggered on the step clock.",
    "One random order, kept until you change the chord.",
    "A different note at random on every step.",
    "Random, but every note plays before any repeats.",
    "Drifts one note at a time, up or down."
};

static_assert(sizeof(modeNames) / sizeof(modeNames[0]) == (size_t) Arpeggiator::numModes,
              "mode name table is out of step with the Mode enum");
static_assert(sizeof(modeDescriptions) / sizeof(modeDescriptions[0]) == (size_t) Arpeggiator::numModes,
              "mode description table is out of step with the Mode enum");

// The rhythms, borrowed from the patterns hardware arpeggiators are known for.
// See the step alphabet in Arpeggiator.h.
struct RhythmPattern
{
    const char* name;
    const char* description;
    const char* steps;
};

const RhythmPattern rhythmPatterns[] = {
    // --- Even -------------------------------------------------------------
    {"Straight",      "Every step, evenly - the plain arpeggio.",                "xxxxxxxxxxxxxxxx"},
    {"Off Beat",      "Only the off beats, the house and reggae skank.",         ".x.x.x.x"},
    {"Half Time",     "One note every other beat, for space under a lead.",      "x...x..."},

    // --- Syncopated -------------------------------------------------------
    {"Dotted 8th",    "Every third step - the dotted delay pattern of trance.",  "x..x..x..x..x..x"},
    {"Tresillo",      "The 3-3-2 pulse under most modern pop and latin music.",  "x..x..x."},
    {"Son Clave",     "The 3-2 son clave, the spine of afro-cuban rhythm.",      "x..x..x...x.x..."},
    {"Skip",          "Two on, one off - a loping, tripping feel.",              "xx.xxx.x"},
    {"Pump",          "Accent on the beat with a pickup, the french house push.", "X..x..X..x..X.x."},

    // --- Driving ----------------------------------------------------------
    {"Gallop",        "Long-short-short, the galloping riff rhythm.",            "x.xxx.xxx.xxx.xx"},
    {"Trance Gate",   "Chopped sixteenths, the gated trance stab.",              "x.xx.x.xx.xx.x.x"},
    {"Stab",          "Accented downbeat with three tight notes behind it.",     "Xxx.Xxx.Xxx.Xxx."},
    {"Rolling",       "Three-note rolls with a breath between them.",            "xxx.xxx.xxx.xx.."},
    {"Burst",         "Rests broken by short bursts of notes.",                  "x..xxx..x..xxx.."},

    // --- Repeats ----------------------------------------------------------
    {"Double Up",     "Every note struck twice, the classic ratchet pair.",      "x=x=x=x="},
    {"Ratchet",       "Sparse, then a fast roll on the last beat.",              "x...x...x...x==="},
    {"Long Short",    "A tied long note answered by a short one.",               "x-x.x-x."},

    // --- Octave -----------------------------------------------------------
    {"Octave Jump",   "Alternates with the note an octave up - the Juno arp.",   "x^x^x^x^"},
    {"Octave Bounce", "Bounces an octave above then below.",                     "x^xvx^xv"},
    {"Rise",          "Two low, two high, climbing in pairs.",                   "xx^^xx^^"},
    {"Fall",          "Two in place, two an octave down.",                       "xxvvxxvv"},
    {"Berlin",        "Wandering octaves over a steady pulse, berlin school.",   "x^x.x^x.xvx.x^x."},

    // --- Character --------------------------------------------------------
    {"Acid",          "Accents and slides, the 303 bassline feel.",              "X-x.x.X-x..xX-.x"},
    {"Chord Punch",   "The whole chord on the beat, single notes between.",      "*.x.*.x.*.x.*.x."},
    {"Chord Stab",    "Block chords on a 3-3-2 pulse.",                          "*..*..*."},
    {"Sprinkle",      "A steady pulse with a random note thrown in.",            "x?x.x?x."}
};

constexpr int patternCount = (int) (sizeof(rhythmPatterns) / sizeof(rhythmPatterns[0]));
} // namespace

const char* Arpeggiator::modeName(int mode)
{
    return modeNames[juce::jlimit(0, numModes - 1, mode)];
}

const char* Arpeggiator::modeDescription(int mode)
{
    return modeDescriptions[juce::jlimit(0, numModes - 1, mode)];
}

int Arpeggiator::numPatterns()
{
    return patternCount;
}

const char* Arpeggiator::patternName(int pattern)
{
    return rhythmPatterns[juce::jlimit(0, patternCount - 1, pattern)].name;
}

const char* Arpeggiator::patternDescription(int pattern)
{
    return rhythmPatterns[juce::jlimit(0, patternCount - 1, pattern)].description;
}

const char* Arpeggiator::patternSteps(int pattern)
{
    return rhythmPatterns[juce::jlimit(0, patternCount - 1, pattern)].steps;
}

int Arpeggiator::tiedStepsAfter(const char* steps, int length, int fromStep) const
{
    // A tie holds the note through the following step rather than restriking
    // it, so the note has to be given that length up front.
    int tied = 0;
    for (int offset = 1; offset < length; ++offset)
    {
        if (steps[(fromStep + offset) % length] != '-')
            break;

        ++tied;
    }

    return tied;
}

void Arpeggiator::prepare(double sampleRateToUse)
{
    sampleRate = sampleRateToUse;

    // Reserved up front so the audio thread is not allocating while a pattern
    // is running; only an unusually large chord will ever push past these.
    heldNotes.reserve(32);
    arrivalOrder.reserve(32);
    pool.reserve(96);
    sequence.reserve(256);
    shuffleBag.reserve(96);
    soundingNotes.reserve(maxChordNotes);

    reset();
}

void Arpeggiator::reset()
{
    heldNotes.clear();
    arrivalOrder.clear();
    pool.clear();
    sequence.clear();
    shuffleBag.clear();
    soundingNotes.clear();
    patternDirty = true;
    builtMode = -1;
    builtOctaves = -1;
    stepIndex = 0;
    patternStep = 0;
    lastPoolIndex = 0;
    lastNotePlayed = -1;
    nextStep = 0;
    needsResync = true;
    samplesUntilNoteOff = 0;
}

void Arpeggiator::stopSoundingNotes(juce::MidiBuffer& midi, int samplePosition)
{
    for (int note : soundingNotes)
        midi.addEvent(juce::MidiMessage::noteOff(1, note), samplePosition);

    soundingNotes.clear();
}

void Arpeggiator::buildPool(Mode mode, int octaveRange)
{
    pool.clear();

    // Two of the modes deliberately ignore pitch order and follow the order the
    // keys were pressed instead.
    const bool byArrival = (mode == Mode::playOrder || mode == Mode::reversePlayOrder);
    const auto& base = byArrival ? arrivalOrder : heldNotes;

    if (base.empty())
        return;

    for (int octave = 0; octave < octaveRange; ++octave)
        for (int note : base)
            if (note + octave * 12 <= 127)
                pool.push_back(note + octave * 12);

    if (!byArrival)
    {
        std::sort(pool.begin(), pool.end());
        pool.erase(std::unique(pool.begin(), pool.end()), pool.end());
    }
}

void Arpeggiator::buildSequence(Mode mode)
{
    sequence.clear();

    const int n = (int) pool.size();
    if (n <= 0)
        return;

    auto ascending = [this](int from, int to)
    {
        for (int i = from; i <= to; ++i)
            sequence.push_back(i);
    };

    auto descending = [this](int from, int to)
    {
        for (int i = from; i >= to; --i)
            sequence.push_back(i);
    };

    // Up then back down over a span, turning without repeating the end notes.
    // The pedal-note modes weave their fixed note through this same shape.
    auto upDownOver = [](int lo, int hi, std::vector<int>& out)
    {
        for (int i = lo; i <= hi; ++i)
            out.push_back(i);
        for (int i = hi - 1; i > lo; --i)
            out.push_back(i);
    };

    // Outside in: lowest, highest, second lowest, second highest, and so on.
    auto convergeInto = [](int count, std::vector<int>& out)
    {
        int lo = 0, hi = count - 1;
        while (lo <= hi)
        {
            out.push_back(lo++);
            if (lo <= hi)
                out.push_back(hi--);
        }
    };

    switch (mode)
    {
        case Mode::up:
            ascending(0, n - 1);
            break;

        case Mode::down:
            descending(n - 1, 0);
            break;

        case Mode::upDown:
            upDownOver(0, n - 1, sequence);
            break;

        case Mode::downUp:
            descending(n - 1, 0);
            if (n > 2)
                ascending(1, n - 2);
            break;

        case Mode::upAndDown:
            ascending(0, n - 1);
            descending(n - 1, 0);
            break;

        case Mode::downAndUp:
            descending(n - 1, 0);
            ascending(0, n - 1);
            break;

        case Mode::upTwice:
            for (int i = 0; i < n; ++i)
            {
                sequence.push_back(i);
                sequence.push_back(i);
            }
            break;

        case Mode::downTwice:
            for (int i = n - 1; i >= 0; --i)
            {
                sequence.push_back(i);
                sequence.push_back(i);
            }
            break;

        case Mode::upDownTwice:
        {
            std::vector<int> base;
            upDownOver(0, n - 1, base);
            for (int i : base)
            {
                sequence.push_back(i);
                sequence.push_back(i);
            }
            break;
        }

        case Mode::cascade:
            // With fewer than three notes the rolling run has nothing to roll
            // through, so it falls back to a plain climb.
            if (n < 3)
            {
                ascending(0, n - 1);
            }
            else
            {
                for (int start = 0; start < n; ++start)
                    for (int step = 0; step < 3; ++step)
                        sequence.push_back((start + step) % n);
            }
            break;

        case Mode::converge:
            convergeInto(n, sequence);
            break;

        case Mode::diverge:
            convergeInto(n, sequence);
            std::reverse(sequence.begin(), sequence.end());
            break;

        case Mode::convergeDiverge:
        {
            std::vector<int> converged;
            convergeInto(n, converged);
            sequence = converged;

            // Coming back out, the note it turned on and the note it started
            // from are already covered at those points, so both are skipped.
            for (int i = (int) converged.size() - 2; i >= 1; --i)
                sequence.push_back(converged[(size_t) i]);
            break;
        }

        case Mode::pinkyUp:
        case Mode::pinkyUpDown:
        {
            if (n < 2)
            {
                ascending(0, n - 1);
                break;
            }

            std::vector<int> base;
            if (mode == Mode::pinkyUp)
                for (int i = 0; i <= n - 2; ++i)
                    base.push_back(i);
            else
                upDownOver(0, n - 2, base);

            for (int i : base)
            {
                sequence.push_back(i);
                sequence.push_back(n - 1);
            }
            break;
        }

        case Mode::thumbUp:
        case Mode::thumbUpDown:
        {
            if (n < 2)
            {
                ascending(0, n - 1);
                break;
            }

            std::vector<int> base;
            if (mode == Mode::thumbUp)
                for (int i = 1; i <= n - 1; ++i)
                    base.push_back(i);
            else
                upDownOver(1, n - 1, base);

            for (int i : base)
            {
                sequence.push_back(0);
                sequence.push_back(i);
            }
            break;
        }

        case Mode::playOrder:
            ascending(0, n - 1);
            break;

        case Mode::reversePlayOrder:
            descending(n - 1, 0);
            break;

        case Mode::chord:
            // The pool is played as a block, so the sequence is only a clock.
            sequence.push_back(0);
            break;

        case Mode::randomOnce:
            ascending(0, n - 1);
            for (int i = n - 1; i > 0; --i)
                std::swap(sequence[(size_t) i], sequence[(size_t) random.nextInt(i + 1)]);
            break;

        // Chosen fresh at each step rather than laid out in advance.
        case Mode::random:
        case Mode::randomOther:
        case Mode::randomWalk:
        case Mode::numModes:
            break;
    }
}

void Arpeggiator::rebuildPattern(Mode mode, int octaveRange)
{
    buildPool(mode, octaveRange);
    buildSequence(mode);

    shuffleBag.clear();
    lastPoolIndex = juce::jlimit(0, juce::jmax(0, (int) pool.size() - 1), lastPoolIndex);

    // Adding a note mid-pattern should not restart the arpeggiator, so the
    // position is folded back into range rather than reset.
    if (!sequence.empty())
        stepIndex %= (int) sequence.size();
    else
        stepIndex = 0;

    builtMode = (int) mode;
    builtOctaves = octaveRange;
    patternDirty = false;
}

int Arpeggiator::nextPoolIndex(Mode mode)
{
    const int n = (int) pool.size();
    if (n <= 0)
        return -1;

    switch (mode)
    {
        case Mode::random:
            lastPoolIndex = random.nextInt(n);
            break;

        case Mode::randomOther:
        {
            // A bag holding every note, drawn from until empty: random, but
            // nothing repeats until the whole chord has been heard.
            if (shuffleBag.empty())
            {
                for (int i = 0; i < n; ++i)
                    shuffleBag.push_back(i);

                for (int i = n - 1; i > 0; --i)
                    std::swap(shuffleBag[(size_t) i], shuffleBag[(size_t) random.nextInt(i + 1)]);

                // Stop the seam between two bags landing on the same note.
                if (n > 1 && shuffleBag.back() == lastPoolIndex)
                    std::swap(shuffleBag.back(), shuffleBag.front());
            }

            lastPoolIndex = shuffleBag.back();
            shuffleBag.pop_back();
            break;
        }

        case Mode::randomWalk:
        {
            int stepBy = random.nextInt(3) - 1; // -1, 0 or +1
            int next = lastPoolIndex + stepBy;

            // Reflect off the ends rather than sticking against them.
            if (next < 0)
                next = juce::jmin(1, n - 1);
            else if (next >= n)
                next = juce::jmax(0, n - 2);

            lastPoolIndex = next;
            break;
        }

        default:
            if (sequence.empty())
                return -1;

            lastPoolIndex = juce::jlimit(0, n - 1, sequence[(size_t) (stepIndex % (int) sequence.size())]);
            stepIndex = (stepIndex + 1) % (int) sequence.size();
            break;
    }

    return lastPoolIndex;
}

// The rate the arpeggiator runs at, in beats per step. Synced, this is a
// musical division - either the one the sequencer is using (so the two share a
// grid) or its own. Unsynced it is a free rate in steps per second, converted
// to beats so the rest of the timing code has only one unit to deal with.
double Arpeggiator::stepBeatsFor(const SynthParameters& params, double bpm)
{
    if (params.arpSync.load() <= 0.5f)
    {
        double rateHz = juce::jlimit(0.5, 30.0, (double) params.arpRateHz.load());
        return juce::jmax(1.0, bpm) / (60.0 * rateHz);
    }

    int link = juce::jlimit(0, tempo::getNumRateLinks() - 1,
                            (int) std::round(params.arpRateLink.load()));

    if (tempo::isFreeRateLink(link))
        return tempo::divisionBeats((int) std::round(params.arpDivision.load()));

    return tempo::linkedBeats(tempo::divisionBeats((int) std::round(params.seqDivision.load())), link);
}

void Arpeggiator::process(juce::MidiBuffer& midi, int numSamples, const SynthParameters& params,
                          const tempo::MusicalClock& clock, double bpm)
{
    bool enabled = params.arpEnabled.load() > 0.5f;

    // Track what is held, from whatever the keyboard/MIDI produced this block.
    for (const auto metadata : midi)
    {
        auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            int note = message.getNoteNumber();
            auto position = std::lower_bound(heldNotes.begin(), heldNotes.end(), note);
            if (position == heldNotes.end() || *position != note)
            {
                heldNotes.insert(position, note);
                arrivalOrder.push_back(note);
                patternDirty = true;
            }
        }
        else if (message.isNoteOff())
        {
            int note = message.getNoteNumber();
            auto position = std::find(heldNotes.begin(), heldNotes.end(), note);
            if (position != heldNotes.end())
            {
                heldNotes.erase(position);
                arrivalOrder.erase(std::remove(arrivalOrder.begin(), arrivalOrder.end(), note),
                                   arrivalOrder.end());
                patternDirty = true;
            }
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            heldNotes.clear();
            arrivalOrder.clear();
            patternDirty = true;
        }
    }

    if (!enabled)
    {
        // Leaving the pattern running: silence its notes before handing control
        // back, or they would hang until something else stopped them.
        if (wasEnabled)
        {
            juce::MidiBuffer cleanup;
            stopSoundingNotes(cleanup, 0);
            for (const auto metadata : midi)
                cleanup.addEvent(metadata.getMessage(), metadata.samplePosition);
            midi.swapWith(cleanup);
            needsResync = true;
        }
        wasEnabled = false;
        return;
    }

    // From here the arpeggiator owns what the voices hear, so the played notes
    // are dropped and replaced with the pattern.
    juce::MidiBuffer generated;

    if (!wasEnabled)
    {
        // Switching on restarts the note order, but not the rhythm: which step
        // of the bar we are on comes from the shared clock, so an arpeggiator
        // switched on mid-bar joins the grid already running rather than
        // starting a private one of its own a fraction of a step out.
        stepIndex = 0;
        lastNotePlayed = -1;
        needsResync = true;
        patternDirty = true;
        wasEnabled = true;
    }

    int octaveRange = juce::jlimit(1, 3, (int) std::round(params.arpOctaves.load()));
    float gate = juce::jlimit(0.05f, 1.0f, params.arpGate.load());
    auto mode = (Mode) juce::jlimit(0, numModes - 1, (int) std::round(params.arpMode.load()));

    if ((int) mode != builtMode || octaveRange != builtOctaves)
        patternDirty = true;

    if (patternDirty)
        rebuildPattern(mode, octaveRange);

    // One step, in beats of the shared clock. Everything below is worked out
    // from the clock position rather than from a countdown of its own, so the
    // arpeggiator cannot drift away from anything else on the same grid.
    double beatsPerStep = juce::jmax(1.0e-6, stepBeatsFor(params, bpm));
    double beatsPerSample = clock.getBeatsPerSample();
    double blockStartBeat = clock.getBlockStartBeat();

    int stepSamples = juce::jmax(1, (int) (beatsPerStep * clock.samplesPerBeat()));
    int gateSamples = juce::jmax(1, (int) (stepSamples * gate));

    // The rhythm the mode is played in, and how hard the pairs of steps shuffle.
    int patternIndex = juce::jlimit(0, numPatterns() - 1, (int) std::round(params.arpPattern.load()));
    const char* steps = patternSteps(patternIndex);
    int patternLength = (int) std::strlen(steps);

    double swing = juce::jlimit(0.0, 0.75, (double) params.arpSwing.load());

    // When a step is due, measured from the clock's zero. Swing pushes every
    // second step of the grid later and leaves the one after it where it was,
    // so a pair still spans two steps and the groove is anchored to the bar
    // rather than to whenever the arpeggiator happened to start.
    auto dueBeatFor = [beatsPerStep, swing](long long step)
    {
        double base = (double) step * beatsPerStep;
        return (step % 2) != 0 ? base + swing * 0.5 * beatsPerStep : base;
    };

    if (pool.empty())
    {
        stopSoundingNotes(generated, 0);
        needsResync = true;
        midi.swapWith(generated);
        return;
    }

    // Joining the grid: take the next step at or after where the clock is now,
    // rather than firing a burst to catch up from wherever we last were.
    if (needsResync || dueBeatFor(nextStep) < blockStartBeat - beatsPerStep
        || dueBeatFor(nextStep) > blockStartBeat + beatsPerStep * 2.0)
    {
        nextStep = (long long) std::ceil(blockStartBeat / beatsPerStep);
        needsResync = false;
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (!soundingNotes.empty() && --samplesUntilNoteOff <= 0)
            stopSoundingNotes(generated, sample);

        if (blockStartBeat + (double) sample * beatsPerSample >= dueBeatFor(nextStep))
        {
            // Where in the rhythm this step falls comes from its place on the
            // grid, so the pattern lines up with the bar and repeats in the
            // same places every time round.
            if (patternLength > 0)
                patternStep = (int) (((nextStep % patternLength) + patternLength) % patternLength);
            const char action = patternLength > 0 ? steps[patternStep % patternLength] : 'x';

            // A tie leaves the note from the previous step ringing; everything
            // else takes over the step and clears whatever was sounding.
            if (action != '-')
                stopSoundingNotes(generated, sample);

            // How long the note started here should ring: through any ties
            // that follow it, then the gate fraction of its own step.
            auto holdSamples = [&]
            {
                return tiedStepsAfter(steps, patternLength, patternStep) * stepSamples + gateSamples;
            };

            auto playNote = [&](int note, float velocity)
            {
                note = juce::jlimit(0, 127, note);
                generated.addEvent(juce::MidiMessage::noteOn(1, note, velocity), sample);
                soundingNotes.push_back(note);
                lastNotePlayed = note;
                samplesUntilNoteOff = holdSamples();
            };

            // A block chord, either because the mode is Chord or because the
            // rhythm calls for one on this step.
            auto playChord = [&](float velocity)
            {
                int count = juce::jmin((int) pool.size(), maxChordNotes);
                for (int i = 0; i < count; ++i)
                {
                    generated.addEvent(juce::MidiMessage::noteOn(1, pool[(size_t) i], velocity), sample);
                    soundingNotes.push_back(pool[(size_t) i]);
                }
                samplesUntilNoteOff = holdSamples();
            };

            // Steps that ask for "the next note" go through the mode, which is
            // what decides the order; the rest address the pool directly.
            auto nextNote = [&](int octaveShift, float velocity)
            {
                if (mode == Mode::chord)
                {
                    playChord(velocity);
                    return;
                }

                int index = nextPoolIndex(mode);
                if (index >= 0)
                    playNote(pool[(size_t) index] + octaveShift * 12, velocity);
            };

            switch (action)
            {
                case '.': break;                         // rest
                case '-': break;                         // tie, already left ringing
                case 'x': nextNote(0, 0.85f); break;
                case 'X': nextNote(0, 1.0f); break;      // accent
                case 'o': nextNote(0, 0.55f); break;     // ghost note
                case '^': nextNote(1, 0.85f); break;
                case 'v': nextNote(-1, 0.85f); break;

                case '=':                                // ratchet: strike it again
                    if (lastNotePlayed >= 0)
                        playNote(lastNotePlayed, 0.7f);
                    else
                        nextNote(0, 0.85f);
                    break;

                case '*': playChord(0.95f); break;

                case '?':
                    playNote(pool[(size_t) random.nextInt((int) pool.size())], 0.85f);
                    break;

                default: nextNote(0, 0.85f); break;
            }

            ++nextStep;
        }
    }

    midi.swapWith(generated);
}
} // namespace wavelathe
