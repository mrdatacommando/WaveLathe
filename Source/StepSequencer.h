// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SynthParameters.h"
#include "ParameterRegistry.h"
#include "ParameterMask.h"
#include "Scales.h"
#include "ProjectState.h"
#include "TempoSync.h"
#include <array>
#include <atomic>
#include <vector>

namespace wavelathe
{
// A step-sequenced line of up to sixty-four steps that writes its own notes,
// plus the automation lanes that
// ride the same clock.
//
// Steps hold a scale DEGREE rather than a note number, so what a step plays
// depends on the key the synth is set to and a wrong note is not representable.
// Changing key transposes the whole pattern and keeps it in tune.
class StepSequencer
{
public:
    static constexpr int maxSteps = 64;

    // Sixteen drum voices, which is what an 808 has and what the grid can show
    // without becoming a spreadsheet. A fixed number rather than a growable
    // list because a voice's identity is its row: saved patterns refer to voice
    // 3, and a list that could be reordered would repoint every one of them.
    //
    // From project:: so the preset reader and the sequencer cannot disagree
    // about how many there are.
    static constexpr int numDrumVoices = project::numDrumVoices;
    // Sixteen ticks to a step, so a note can be as short as a sixteenth of one
    // and can start anywhere inside it. Lengths and offsets are both counted
    // in these, never in whole steps.
    static constexpr int ticksPerStep = project::ticksPerStep;
    static constexpr int maxTicks = maxSteps * ticksPerStep;

    struct Step
    {
        bool active = false;
        int degree = 0;          // scale degrees above the pattern's root
        int octave = 0;          // -1 .. +2, added on top of the degree
        int length = ticksPerStep; // how long the note lasts, in ticks
        int tickOffset = 0;        // 0..ticksPerStep-1, how late inside its step it starts
        float velocity = 0.85f;
        float gate = 0.9f;       // fraction of its length the note actually sounds
        float probability = 1.0f;
        bool locked = false;     // Generate leaves locked steps alone
        // Ties this step into the next one: the note is held until the next
        // step starts, and the pitch slides across instead of the envelope
        // retriggering. Needs mono and legato to mean anything - see process()
        // for what the sequencer does when they are off.
        bool slide = false;

        // Plays this step at the pattern's accent level instead of its own
        // velocity. A flag rather than just a tall velocity bar so that one
        // dial can retune every accent at once, and so Generate can place
        // accents as rhythm rather than as numbers.
        bool accent = false;
    };

    // One cell of the drum grid - a SUB-step, not a step. There are
    // project::drumSubdivisions of these to each sequencer step, so a row is
    // maxDrumCells long rather than maxSteps.
    //
    // Called a cell rather than a step for exactly that reason: the two stopped
    // being the same thing the moment the grid could hold a hit between one
    // beat and the next, and a name that still said "step" would have someone
    // indexing this with a step number sooner or later.
    struct DrumCell
    {
        bool active = false;
        float velocity = 0.85f;
        float probability = 1.0f;

        // Same meaning as a melodic step's accent: play at the pattern's accent
        // level instead of this cell's own velocity, so one dial retunes every
        // accent at once. On an 808 this is a single global control, and
        // copying that restraint is the point.
        bool accent = false;

        // How far off its own cell this hit sits, in ticks, either way. This is
        // feel rather than placement: the cell says which 32nd it belongs to,
        // this says how tightly it sits on it. See project::maxDrumNudge.
        int nudge = 0;
    };

    static constexpr int numDrumCells = project::maxDrumCells;
    static constexpr int drumSubdivisions = project::drumSubdivisions;
    static constexpr int ticksPerDrumCell = project::ticksPerDrumCell;
    static constexpr int maxDrumNudge = project::maxDrumNudge;

    // Where a cell actually falls on the tick grid, nudge included. One place,
    // so the grid draws a hit exactly where the sequencer will fire it - which
    // is the whole point of having the nudge be data rather than a drawing
    // offset.
    static int tickOfDrumCell(int cell, int nudge)
    {
        return cell * ticksPerDrumCell + juce::jlimit(-maxDrumNudge, maxDrumNudge, nudge);
    }

    // What each row is called, in the order an 808 lays them out. Kept here
    // rather than in the panel because a voice's number is its identity - a
    // saved pattern says "voice 3" - so the name the grid prints and the name
    // whatever eventually makes the sound answers to have to come from one
    // list. Two lists would disagree the first time one gained an entry.
    static const char* drumVoiceName(int voice);

    void prepare(double sampleRate);
    void reset();

    // Adds the sequencer's notes to the buffer and moves the automation on.
    // Not const: playing back a lane writes to the parameters.
    void process(juce::MidiBuffer& midi, int numSamples, SynthParameters& params,
                 const tempo::MusicalClock& clock);

    // ---- Pattern, edited from the UI ------------------------------------
    Step getStep(int index) const;
    void setStep(int index, const Step& step);
    void clearPattern();

    // Placing and resizing a note, clearing whatever it now covers - a note
    // owns the steps it spans, the way a block does in a piano roll. Lengths
    // are in ticks, so a note can be shorter than the step it starts in.
    void placeNote(int stepIndex, int degree, int lengthInTicks, float velocity);

    // Placing a note the way drawing one does: one whole step long. The default
    // lives here rather than in the UI because it is expressed in ticks, and a
    // caller that wrote 1 meaning "one step" would silently get a sixteenth of
    // one - which is exactly what happened once.
    void placeNote(int stepIndex, int degree, float velocity);
    void setNoteLength(int stepIndex, int lengthInTicks);

    // Where a note starts and stops on the tick grid, which is what both the
    // drawing and the overlap rules work from.
    static int startTickOf(int stepIndex, const Step& step) { return stepIndex * ticksPerStep + step.tickOffset; }
    static int endTickOf(int stepIndex, const Step& step) { return startTickOf(stepIndex, step) + juce::jmax(1, step.length); }
    void clearNote(int stepIndex);

    // The step whose note is sounding across this one, or -1. Used by the grid
    // to draw a block across the steps it covers.
    int noteCovering(int stepIndex) const;

    // ---- The drum pattern, beside the melodic one ------------------------
    //
    // A second pattern on the same transport, not a second mode. Both exist at
    // once and both will play; the tab in the editor chooses which one you are
    // EDITING. That is how a groovebox behaves, and it is what lets a bassline
    // and a beat run together - which a mode switch forbids.
    //
    // Length and division are not repeated here. They belong to the transport,
    // which both patterns share, so a beat cannot drift out of step with the
    // line above it by construction rather than by being kept in sync.
    DrumCell getDrumCell(int voice, int cell) const;
    void setDrumCell(int voice, int cell, const DrumCell& value);

    // Toggling a cell, which is the whole of what drawing on the grid does.
    void setDrumCellActive(int voice, int cell, bool active);

    // Pushing a placed hit off its beat. Silently ignores a cell with nothing
    // on it: nudging an empty cell would store feel for a hit that does not
    // exist, and it would come back the next time one was drawn there.
    void setDrumCellNudge(int voice, int cell, int nudge);

    // Whether a voice has anything on it at all. What the grid dims a row by,
    // and what decides whether the voice is worth saving.
    bool isDrumVoiceUsed(int voice) const;
    void clearDrumVoice(int voice);
    void clearDrumPattern();
    int getNumUsedDrumVoices() const;

    // ---- What the drum pattern produced in the last block -----------------
    //
    // A hit, and where in the block it lands.
    //
    // The sequencer hands these out rather than striking a drum itself, and
    // that separation is the point. The drum kit is a library that knows
    // nothing about patterns; the sequencer is a pattern that knows nothing
    // about synthesis; the processor owns both and joins them. A sequencer
    // that called into the kit would drag the DSP into every target that
    // compiles this file - five test suites that have no interest in it - and
    // would make "does a nudged hit land where it should" a question you could
    // only answer by rendering audio.
    struct DrumHit
    {
        int voice = 0;
        float velocity = 0.85f;
        bool accent = false;

        // Samples from the start of the block. NOT the tick: the whole reason
        // phase 2a gave hits a signed nudge is that they do not sit on beats,
        // and a whole-block offset would round that away.
        int sampleOffset = 0;
    };

    // Sixteen voices could in principle all be hit on the same cell, and a
    // long block at a fast tempo can hold several cells. Sixty-four is past
    // anything real; past that they are counted rather than dropped in silence.
    static constexpr int maxDrumHitsPerBlock = 64;

    // Filled by process() and cleared at the top of every call, so these are
    // always the hits belonging to the block that just ran. Read on the audio
    // thread, by whoever called process, before the next call.
    int getNumDrumHits() const noexcept { return numDrumHits; }
    DrumHit getDrumHit(int index) const;

    // Hits that did not fit, taken and cleared. Never non-zero in practice;
    // if it ever is, a pattern is losing notes and this is the only way anyone
    // would find out.
    int takeDroppedDrumHits() noexcept;

    // Euclidean rhythm plus a melodic shape, leaving locked steps untouched.
    // Density is the number of steps that sound; range is how far the line is
    // allowed to wander in scale degrees.
    void generate(int pulses, int range, bool resolveToRoot, int patternSteps);

    // ---- Transport -------------------------------------------------------
    int getCurrentStep() const { return currentStep.load(); }
    int getCurrentTick() const { return currentTick.load(); }
    bool isPlaying() const { return playing.load(); }

    // ---- Automation ------------------------------------------------------
    // Marks a parameter as being held by the user, so recording captures it and
    // playback does not fight it. Cleared when the dial is let go.
    //
    // Two ways to be held, kept in separate sets because they are written by
    // different threads and neither may clear the other's answer: the editor
    // reports a hand on the dial from the message thread, the audio thread
    // reports a learned controller moving it. One set written by both would
    // have each of them switching the other off every frame.
    void setParameterTouched(int parameterId, bool touched);
    void setParameterDrivenByController(int parameterId, bool driven);
    bool isLaneUsed(int parameterId) const;
    bool isLaneTouched(int parameterId) const;
    void clearLane(int parameterId);
    void clearAllLanes();
    int getNumUsedLanes() const;

    // Reading and drawing a lane by hand, so automation can be edited on the
    // timeline as well as performed onto it.
    float getLaneValue(int parameterId, int tick) const;
    void setLaneValue(int parameterId, int tick, float value);

    // Starts a lane flat at the parameter's current value, which is what makes
    // a hand-drawn lane begin somewhere sensible rather than at zero.
    void createLane(int parameterId, const SynthParameters& params);

    // ---- Saving and restoring the pattern --------------------------------
    // Only lanes that are actually in use are captured, so a project carries
    // what was drawn rather than thirty-two empty lanes.
    SequencerState captureState() const;
    void restoreState(const SequencerState& state);

private:
    void triggerStep(juce::MidiBuffer& midi, int samplePosition, int stepIndex, int stepSamples,
                     const SynthParameters& params);

    // Capturing what is played onto the grid while REC is armed.
    void captureIncomingNotes(const juce::MidiBuffer& midi, double blockStartTick, double ticksPerSample,
                              int patternSteps, const SynthParameters& params);
    void beginRecordedNote(int note, double atTick, float velocity, int patternSteps,
                           const SynthParameters& params);
    void endRecordedNote(int note, double atTick, int patternSteps);

    struct RecordedNote
    {
        bool active = false;
        int startStep = 0;
        double startTick = 0.0;
    };

    RecordedNote recordedNotes[128];
    void stopSoundingNote(juce::MidiBuffer& midi, int samplePosition);
    void applyAutomation(SynthParameters& params, int tick);
    void recordAutomation(const SynthParameters& params, int tick);

    // Every drum cell that fires on this tick, queued at this sample.
    //
    // At most one CELL can fire on any tick, which is what makes this cheap:
    // cell bases are ticksPerDrumCell apart and a nudge is strictly less than
    // half of that either way, so the tick names its cell and the scan is over
    // twelve voices rather than over the grid. That "strictly less" is
    // maxDrumNudge being n/2 - 1 rather than n/2 - the property a test in
    // phase 2a asserted before the -1 existed.
    void scanDrumCells(int tick, int samplePosition, int ticksInPattern);

    double sampleRate = 44100.0;

    // Guards the pattern and the lanes. Both are edited from the message thread
    // in very short bursts, so the audio thread's wait is negligible.
    mutable juce::SpinLock patternLock;
    std::array<Step, maxSteps> steps;

    // Sixteen rows of sixty-four cells, guarded by the same lock as the melodic
    // pattern because the two are edited by the same hands and read by the same
    // block.
    //
    // On the heap, allocated once here, for the same reason the lanes are.
    //
    // This was an inline std::array, with a comment saying 40 KB was far too
    // small to be worth an indirection. Per object that was true. In aggregate
    // it was not: SequencerTest declares about thirty sequencers as locals and
    // MSVC reserves a function's whole frame at once, so 30 x 40 KB went
    // straight back over the 1 MB default stack and the suite died before
    // printing a single line. The same trap the lanes had, at a tenth the size.
    //
    // No allocation on the audio thread either way - this is sized once, in the
    // constructor, and only ever indexed afterwards.
    std::vector<std::array<DrumCell, numDrumCells>> drumCells =
        std::vector<std::array<DrumCell, numDrumCells>>((size_t) numDrumVoices);

    // Inline rather than on the heap, unlike the lanes above: a kilobyte is
    // nothing beside the forty this object already carries, and it must never
    // be allocated from the audio thread that fills it.
    std::array<DrumHit, maxDrumHitsPerBlock> drumHits{};
    int numDrumHits = 0;
    int droppedDrumHits = 0;

    struct Lane
    {
        bool used = false;
        std::array<float, maxTicks> values{};
    };
    // One lane per parameter id, each carrying a full bar of tick values
    // inline - about 4 KB apiece, so half a megabyte of lanes once the cap is
    // 128. On the heap rather than inside the object, allocated once here and
    // never again.
    //
    // It was a std::array, and the reason written down for keeping it one was
    // that recordAutomation "creates lanes on the audio thread", where a
    // vector would allocate. That was a misreading. Nothing ever creates a
    // Lane: all of them exist from construction, and recordAutomation only
    // sets used = true on one that is already there. Every access in this file
    // is an index or an iteration - there is no push_back, emplace or resize
    // anywhere - so the audio thread allocates exactly as often as it did
    // before, which is never.
    //
    // What it buys is that a StepSequencer is a few dozen bytes again instead
    // of half a megabyte. SequencerTest declares thirty of them as locals and
    // was already asking for a 16 MB stack at the old size.
    std::vector<Lane> lanes = std::vector<Lane>((size_t) paramreg::maxParameters);

    // One bit per parameter id, so the audio thread can ask "is a hand on this
    // dial?" without taking a lock. Widens with paramreg::maxParameters rather
    // than capping it - see ParameterMask.h.
    AtomicParameterMask touchedParameters;

    // The same, for parameters a learned MIDI controller is moving. A control
    // has no "let go" message, so the processor holds this on for a moment
    // after each one arrives and the two sets are read together.
    AtomicParameterMask controllerTouched;

    // Latched: parameters moved at any point during the current recording pass.
    // A control is let go of the moment you stop moving it, which is right for
    // auditioning and wrong for recording - it hands the parameter back to its
    // lane between one adjustment and the next, so a pass made of several comes
    // out as the new value, then the old one, then the new one again. Once
    // something is moved with REC armed it stays yours until the pass ends.
    AtomicParameterMask latchedParameters;

    // Whether the last block was recording, so the latch can be emptied at the
    // edges of a pass rather than accumulating across all of them.
    bool wasRecording = false;

    // Takes the named parameters out of all three held sets. Used by the clear
    // actions: throwing a lane away has to end the recording pass for it as
    // well, or the latch writes the lane back on the next tick.
    void releaseFromRecording(const ParameterMask& parameterMask);

    // Held by any of the three, which is what everything downstream actually
    // wants: the question is never "was it the mouse", it is "is something
    // other than the lane in charge of this right now".
    //
    // Three separate loads, so this was never an instantaneous snapshot of all
    // three sets and callers have always tolerated that. Widening the masks
    // makes it six loads of the same kind rather than a new hazard.
    ParameterMask heldParameters() const
    {
        return touchedParameters.load() | controllerTouched.load() | latchedParameters.load();
    }

    std::atomic<int> currentStep{0};
    std::atomic<int> currentTick{0};
    std::atomic<bool> playing{false};

    int lastTick = -1;
    int soundingNote = -1;
    int samplesUntilNoteOff = 0;

    // Whether the note now sounding is tied into the step that follows it. It
    // is held past its own gate, and the next step's note arrives on top of it
    // rather than after it, which is the whole of what makes a slide a slide.
    bool sustainingForSlide = false;
    bool wasPlaying = false;

    juce::Random random;
};
} // namespace wavelathe
