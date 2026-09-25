// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "StepSequencer.h"
#include <algorithm>
#include <cmath>

namespace wavelathe
{
namespace
{
// The classic even-distribution rhythm: spreading N pulses as evenly as
// possible over M steps produces most of the rhythms people actually play,
// which is why two numbers are a better control than sixteen switches.
std::array<bool, StepSequencer::maxSteps> euclideanRhythm(int steps, int pulses)
{
    std::array<bool, StepSequencer::maxSteps> pattern{};

    steps = juce::jlimit(1, StepSequencer::maxSteps, steps);
    pulses = juce::jlimit(0, steps, pulses);

    for (int i = 0; i < steps; ++i)
        pattern[(size_t) i] = ((i * pulses) % steps) < pulses;

    return pattern;
}
} // namespace

void StepSequencer::prepare(double sampleRateToUse)
{
    sampleRate = sampleRateToUse;
    reset();
}

void StepSequencer::reset()
{
    lastTick = -1;
    soundingNote = -1;
    samplesUntilNoteOff = 0;
    sustainingForSlide = false;
    currentStep.store(0);
}

StepSequencer::Step StepSequencer::getStep(int index) const
{
    if (index < 0 || index >= maxSteps)
        return {};

    const juce::SpinLock::ScopedLockType lock(patternLock);
    return steps[(size_t) index];
}

void StepSequencer::setStep(int index, const Step& step)
{
    if (index < 0 || index >= maxSteps)
        return;

    const juce::SpinLock::ScopedLockType lock(patternLock);
    steps[(size_t) index] = step;
}

void StepSequencer::clearPattern()
{
    const juce::SpinLock::ScopedLockType lock(patternLock);
    for (auto& step : steps)
        step = Step{};
}

const char* StepSequencer::drumVoiceName(int voice)
{
    // The list itself moved to project:: when the parameter registry needed it
    // too - three copies of sixteen names was one more than the number that can
    // be kept honest. This stays as the name the grid and the editor already
    // call, forwarding rather than repeating.
    return project::drumVoiceName(voice);
}

StepSequencer::DrumHit StepSequencer::getDrumHit(int index) const
{
    if (index < 0 || index >= numDrumHits)
        return {};

    return drumHits[(size_t) index];
}

int StepSequencer::takeDroppedDrumHits() noexcept
{
    const auto count = droppedDrumHits;
    droppedDrumHits = 0;
    return count;
}

// ---- The drum pattern ------------------------------------------------------
//
// Same shape as the melodic accessors above: bounds-check, take the lock, copy.
// Two indices instead of one, so the guard checks both - a voice out of range
// is exactly as much of a mistake as a step out of range, and neither may be
// allowed to land on a neighbouring row.
StepSequencer::DrumCell StepSequencer::getDrumCell(int voice, int cell) const
{
    if (voice < 0 || voice >= numDrumVoices || cell < 0 || cell >= numDrumCells)
        return {};

    const juce::SpinLock::ScopedLockType lock(patternLock);
    return drumCells[(size_t) voice][(size_t) cell];
}

void StepSequencer::setDrumCell(int voice, int cell, const DrumCell& value)
{
    if (voice < 0 || voice >= numDrumVoices || cell < 0 || cell >= numDrumCells)
        return;

    const juce::SpinLock::ScopedLockType lock(patternLock);

    auto& target = drumCells[(size_t) voice][(size_t) cell];
    target = value;

    // Clamped here rather than trusted from the caller, because this is also
    // the path a loaded project comes in by and a file can say anything.
    target.nudge = juce::jlimit(-maxDrumNudge, maxDrumNudge, target.nudge);
}

void StepSequencer::setDrumCellActive(int voice, int cell, bool active)
{
    if (voice < 0 || voice >= numDrumVoices || cell < 0 || cell >= numDrumCells)
        return;

    const juce::SpinLock::ScopedLockType lock(patternLock);

    // Only the flag. Velocity, probability, accent and nudge are properties of
    // the cell that survive it being switched off and on again - so a hit tuned
    // quiet and pushed early comes back quiet and early, rather than resetting
    // and having to be tuned a second time.
    drumCells[(size_t) voice][(size_t) cell].active = active;
}

void StepSequencer::setDrumCellNudge(int voice, int cell, int nudge)
{
    if (voice < 0 || voice >= numDrumVoices || cell < 0 || cell >= numDrumCells)
        return;

    const juce::SpinLock::ScopedLockType lock(patternLock);

    auto& target = drumCells[(size_t) voice][(size_t) cell];

    // Feel belongs to a hit. Storing it on an empty cell would keep it for a
    // hit that does not exist and hand it to the next one drawn there, which
    // reads as a grid that remembers things it was never told.
    if (!target.active)
        return;

    target.nudge = juce::jlimit(-maxDrumNudge, maxDrumNudge, nudge);
}

bool StepSequencer::isDrumVoiceUsed(int voice) const
{
    if (voice < 0 || voice >= numDrumVoices)
        return false;

    const juce::SpinLock::ScopedLockType lock(patternLock);

    for (const auto& cell : drumCells[(size_t) voice])
        if (cell.active)
            return true;

    return false;
}

void StepSequencer::clearDrumVoice(int voice)
{
    if (voice < 0 || voice >= numDrumVoices)
        return;

    const juce::SpinLock::ScopedLockType lock(patternLock);
    for (auto& cell : drumCells[(size_t) voice])
        cell = DrumCell{};
}

void StepSequencer::clearDrumPattern()
{
    const juce::SpinLock::ScopedLockType lock(patternLock);
    for (auto& voice : drumCells)
        for (auto& cell : voice)
            cell = DrumCell{};
}

int StepSequencer::getNumUsedDrumVoices() const
{
    const juce::SpinLock::ScopedLockType lock(patternLock);

    int used = 0;
    for (const auto& voice : drumCells)
        for (const auto& cell : voice)
            if (cell.active)
            {
                ++used;
                break;
            }

    return used;
}

int StepSequencer::noteCovering(int stepIndex) const
{
    if (stepIndex < 0 || stepIndex >= maxSteps)
        return -1;

    const juce::SpinLock::ScopedLockType lock(patternLock);

    // Walk back from here until a note is found that reaches this far.
    for (int start = stepIndex; start >= 0; --start)
    {
        const auto& step = steps[(size_t) start];
        if (!step.active)
            continue;

        return endTickOf(start, step) > stepIndex * ticksPerStep ? start : -1;
    }

    return -1;
}

void StepSequencer::placeNote(int stepIndex, int degree, int lengthInTicks, float velocity)
{
    if (stepIndex < 0 || stepIndex >= maxSteps)
        return;

    const juce::SpinLock::ScopedLockType lock(patternLock);

    auto& step = steps[(size_t) stepIndex];
    bool wasActive = step.active;
    int offset = wasActive ? step.tickOffset : 0;
    int startTick = stepIndex * ticksPerStep + offset;

    lengthInTicks = juce::jlimit(1, maxTicks - startTick, lengthInTicks);
    int endTick = startTick + lengthInTicks;

    // A note owns the steps it spans, so anything starting underneath it goes,
    // and a note reaching into it from the left is shortened to stop here.
    for (int i = stepIndex + 1; i < maxSteps && i * ticksPerStep < endTick; ++i)
        steps[(size_t) i] = Step{};

    for (int before = stepIndex - 1; before >= 0; --before)
    {
        auto& earlier = steps[(size_t) before];
        if (!earlier.active)
            continue;

        if (endTickOf(before, earlier) > startTick)
            earlier.length = juce::jmax(1, startTick - startTickOf(before, earlier));

        break;
    }

    step.active = true;
    step.degree = degree;
    step.length = lengthInTicks;

    // Only a brand new note gets default timing and feel. Resizing one has to
    // leave them alone, or setting a recorded note's length would flatten the
    // very timing that was captured by playing it.
    if (!wasActive)
    {
        step.tickOffset = 0;
        step.velocity = velocity;
        step.gate = 0.9f;
        step.probability = 1.0f;
    }
}

void StepSequencer::placeNote(int stepIndex, int degree, float velocity)
{
    placeNote(stepIndex, degree, ticksPerStep, velocity);
}

void StepSequencer::setNoteLength(int stepIndex, int lengthInTicks)
{
    if (stepIndex < 0 || stepIndex >= maxSteps)
        return;

    Step current;
    {
        const juce::SpinLock::ScopedLockType lock(patternLock);
        current = steps[(size_t) stepIndex];
    }

    if (!current.active)
        return;

    placeNote(stepIndex, current.degree, lengthInTicks, current.velocity);
}

void StepSequencer::clearNote(int stepIndex)
{
    if (stepIndex < 0 || stepIndex >= maxSteps)
        return;

    const juce::SpinLock::ScopedLockType lock(patternLock);
    steps[(size_t) stepIndex] = Step{};
}

void StepSequencer::generate(int pulses, int range, bool resolveToRoot, int patternSteps)
{
    const juce::SpinLock::ScopedLockType lock(patternLock);

    patternSteps = juce::jlimit(1, maxSteps, patternSteps);

    auto rhythm = euclideanRhythm(patternSteps, pulses);
    range = juce::jmax(1, range);

    int previousDegree = 0;

    // Anything past the pattern's length is cleared, so shortening the pattern
    // and generating again does not leave notes hiding off the end.
    for (int i = patternSteps; i < maxSteps; ++i)
        if (!steps[(size_t) i].locked)
            steps[(size_t) i] = Step{};

    for (int i = 0; i < patternSteps; ++i)
    {
        auto& step = steps[(size_t) i];

        if (step.locked)
        {
            previousDegree = step.degree;
            continue;
        }

        step.active = rhythm[(size_t) i];

        // Mostly steps, occasionally a leap: a line that only ever jumps at
        // random sounds like noise, and one that only steps sounds like a
        // scale exercise.
        int move = random.nextInt(10) < 7 ? random.nextInt(3) - 1
                                          : random.nextInt(2 * range + 1) - range;

        int degree = juce::jlimit(-range, range, previousDegree + move);

        // Landing on the root at the top of the bar is what makes a generated
        // line sound like it belongs somewhere rather than wandering.
        if (resolveToRoot && i == 0)
            degree = 0;

        step.degree = degree;
        step.octave = 0;
        step.length = ticksPerStep;
        step.tickOffset = 0;
        step.velocity = (i % 4 == 0) ? 1.0f : 0.7f + random.nextFloat() * 0.2f;
        step.gate = 0.4f + random.nextFloat() * 0.3f;
        step.probability = (i % 4 == 0) ? 1.0f : (random.nextInt(10) < 8 ? 1.0f : 0.6f);

        previousDegree = degree;
    }

    // Now the lengths. A note runs until the next one starts, so a sparse
    // pattern gives long held notes and a dense one gives short ones - which is
    // what density ought to mean. Every note the same length regardless is why
    // a generated line used to sound like a typewriter.
    for (int i = 0; i < patternSteps; ++i)
    {
        auto& step = steps[(size_t) i];
        if (!step.active || step.locked)
            continue;

        // How far it is to whatever sounds next, wrapping round the pattern so
        // the last note can hold to the end of the bar.
        int gap = patternSteps;
        for (int ahead = 1; ahead <= patternSteps; ++ahead)
        {
            if (steps[(size_t) ((i + ahead) % patternSteps)].active)
            {
                gap = ahead;
                break;
            }
        }

        int available = gap * ticksPerStep;

        // Most notes take the whole gap; some stop short, which is what stops a
        // sparse pattern from being one unbroken drone.
        int roll = random.nextInt(10);
        if (roll < 5)
            step.length = available;
        else if (roll < 8)
            step.length = juce::jmax(ticksPerStep, available / 2);
        else
            step.length = juce::jmax(ticksPerStep / 4, available / 4);

        step.length = juce::jlimit(1, maxTicks - i * ticksPerStep, step.length);
    }
}

void StepSequencer::setParameterTouched(int parameterId, bool touched)
{
    if (parameterId < 0 || parameterId >= paramreg::maxParameters)
        return;

    touchedParameters.set(parameterId, touched);
}

void StepSequencer::setParameterDrivenByController(int parameterId, bool driven)
{
    if (parameterId < 0 || parameterId >= paramreg::maxParameters)
        return;

    controllerTouched.set(parameterId, driven);
}

bool StepSequencer::isLaneUsed(int parameterId) const
{
    if (parameterId < 0 || parameterId >= paramreg::maxParameters)
        return false;

    const juce::SpinLock::ScopedLockType lock(patternLock);
    return lanes[(size_t) parameterId].used;
}

bool StepSequencer::isLaneTouched(int parameterId) const
{
    if (parameterId < 0 || parameterId >= paramreg::maxParameters)
        return false;

    return heldParameters().test(parameterId);
}

void StepSequencer::clearLane(int parameterId)
{
    if (parameterId < 0 || parameterId >= paramreg::maxParameters)
        return;

    releaseFromRecording(ParameterMask::forParameter(parameterId));

    const juce::SpinLock::ScopedLockType lock(patternLock);
    lanes[(size_t) parameterId].used = false;
}

void StepSequencer::clearAllLanes()
{
    releaseFromRecording(ParameterMask::all());

    const juce::SpinLock::ScopedLockType lock(patternLock);
    for (auto& lane : lanes)
        lane.used = false;
}

void StepSequencer::releaseFromRecording(const ParameterMask& parameterMask)
{
    // Clearing a lane while the pass is still running has to end the pass for
    // that parameter too, or the very next tick writes the lane straight back.
    // The latch is the memory of "this was moved during this recording pass",
    // and it outlives letting go of the dial on purpose - so after a clear it
    // is a memory of something that has just been thrown away, and acting on it
    // recreates the lane, flat, at wherever the dial happens to be sitting. The
    // values look cleared because they ARE all the same; the entry comes back
    // because nothing said the pass was over.
    //
    // The other two sets are reconciled every frame from whether a hand or a
    // controller is actually moving something, so clearing them costs nothing:
    // anything genuinely being moved right now is back within a frame, which is
    // correct - that is a new movement, made after the clear.
    latchedParameters.andNot(parameterMask);
    touchedParameters.andNot(parameterMask);
    controllerTouched.andNot(parameterMask);
}

int StepSequencer::getNumUsedLanes() const
{
    const juce::SpinLock::ScopedLockType lock(patternLock);

    int used = 0;
    for (int i = 0; i < paramreg::count(); ++i)
        if (lanes[(size_t) i].used)
            ++used;

    return used;
}

float StepSequencer::getLaneValue(int parameterId, int tick) const
{
    if (parameterId < 0 || parameterId >= paramreg::maxParameters || tick < 0 || tick >= maxTicks)
        return 0.0f;

    const juce::SpinLock::ScopedLockType lock(patternLock);
    return lanes[(size_t) parameterId].values[(size_t) tick];
}

void StepSequencer::setLaneValue(int parameterId, int tick, float value)
{
    if (parameterId < 0 || parameterId >= paramreg::maxParameters || tick < 0 || tick >= maxTicks)
        return;

    const juce::SpinLock::ScopedLockType lock(patternLock);
    auto& lane = lanes[(size_t) parameterId];
    lane.used = true;
    lane.values[(size_t) tick] = juce::jlimit(0.0f, 1.0f, value);
}

void StepSequencer::createLane(int parameterId, const SynthParameters& params)
{
    if (parameterId < 0 || parameterId >= paramreg::maxParameters)
        return;

    float current = paramreg::readNormalised(params, parameterId);

    const juce::SpinLock::ScopedLockType lock(patternLock);
    auto& lane = lanes[(size_t) parameterId];

    if (!lane.used)
    {
        lane.values.fill(current);
        lane.used = true;
    }
}

void StepSequencer::stopSoundingNote(juce::MidiBuffer& midi, int samplePosition)
{
    if (soundingNote >= 0)
    {
        midi.addEvent(juce::MidiMessage::noteOff(1, soundingNote), samplePosition);
        soundingNote = -1;
    }

    // Nothing sounding means nothing tied into what comes next, which keeps the
    // two in step wherever this is called from - including the cleanup when
    // play stops, where a pattern halted mid-slide would otherwise come back
    // still believing it was in one.
    sustainingForSlide = false;
}

void StepSequencer::scanDrumCells(int tick, int samplePosition, int ticksInPattern)
{
    const auto cellsInPattern = juce::jmax(1, ticksInPattern / ticksPerDrumCell);

    // Which cell could possibly reach this tick. Bases are ticksPerDrumCell
    // apart and a nudge cannot span half of one, so the nearest base is the
    // only candidate and there is no search.
    const auto nearest = (tick + ticksPerDrumCell / 2) / ticksPerDrumCell;

    // The cell is wrapped into the pattern and its base is NOT, and that
    // asymmetry is the whole of how an early nudge works.
    //
    // In the last few ticks of a bar, `nearest` runs one past the final cell.
    // Wrapping it brings the cell round to 0; leaving the base out at
    // ticksInPattern is what then makes cell 0 pushed three ticks early come
    // out as ticksInPattern - 3 - which is exactly where it should sound, at
    // the very end of the bar before. Wrap the base as well, "for tidiness",
    // and it collapses to 0 - 3 and the hit simply never fires.
    //
    // A kick a hair ahead of the downbeat is one of the commonest things
    // anybody does with nudge, so this is the ordinary case rather than an
    // edge one. SequencerTest fails if the base is wrapped.
    const auto cell = nearest % cellsInPattern;
    const auto base = nearest * ticksPerDrumCell;

    for (int voice = 0; voice < numDrumVoices; ++voice)
    {
        DrumCell drum;
        {
            const juce::SpinLock::ScopedLockType lock(patternLock);
            drum = drumCells[(size_t) voice][(size_t) cell];
        }

        if (!drum.active)
            continue;

        // No modulo here, deliberately. This began as
        // `(base + nudge) % ticksInPattern` with a guard for a negative
        // result, both of which turned out to be dead: given an unwrapped
        // base, the sum is already the tick the hit belongs on, and a
        // mutation removing them did not fail a single test. Dead code that
        // looks like the important part is worse than no code at all - it is
        // where the next person looks for the behaviour, and it is not there.
        const auto fireTick = base + juce::jlimit(-maxDrumNudge, maxDrumNudge, drum.nudge);

        if (fireTick != tick)
            continue;

        // Rolled per hit, from the same generator the melodic steps use, so a
        // pattern with probability on it varies as a whole rather than the
        // drums and the line taking turns being the unpredictable one.
        if (drum.probability < 1.0f && random.nextFloat() > drum.probability)
            continue;

        if (numDrumHits >= maxDrumHitsPerBlock)
        {
            ++droppedDrumHits;
            continue;
        }

        DrumHit hit;
        hit.voice = voice;
        hit.velocity = juce::jlimit(0.0f, 1.0f, drum.velocity);
        hit.accent = drum.accent;
        hit.sampleOffset = samplePosition;

        drumHits[(size_t) numDrumHits] = hit;
        ++numDrumHits;
    }
}

void StepSequencer::triggerStep(juce::MidiBuffer& midi, int samplePosition, int stepIndex, int stepSamples,
                                 const SynthParameters& params)
{
    Step step;
    {
        const juce::SpinLock::ScopedLockType lock(patternLock);
        step = steps[(size_t) stepIndex];
    }

    if (!step.active)
        return;

    if (step.probability < 1.0f && random.nextFloat() > step.probability)
        return;

    int key = juce::jlimit(0, music::numKeys - 1, (int) std::round(params.musicalKey.load()));
    auto scale = (music::Scale) juce::jlimit(0, (int) music::Scale::numScales - 1,
                                              (int) std::round(params.musicalScale.load()));

    // A degree is only meaningful inside a scale; with the lock off, fall back
    // to treating degrees as semitones so the sequencer still plays.
    int rootNote = juce::jlimit(0, 127, (int) std::round(params.seqRootNote.load()));
    int note;

    if (scale == music::Scale::chromatic)
    {
        note = rootNote + step.degree + step.octave * 12;
    }
    else
    {
        int rootDegree = music::degreeForNote(music::snapToScale(rootNote, key, scale), key, scale);
        note = music::noteForDegree(rootDegree + step.degree, key, scale) + step.octave * 12;
    }

    note = juce::jlimit(0, 127, note);

    // An accented step plays at the pattern's accent level rather than its own
    // velocity. Everything an accent does downstream - louder, brighter, more
    // resonant, further through the wavetable - happens because velocity is
    // already wired to all of those; nothing here needs to know about any of it.
    float velocity = step.accent ? params.accentAmount.load() : step.velocity;

    midi.addEvent(juce::MidiMessage::noteOn(1, note, juce::jlimit(0.05f, 1.0f, velocity)),
                  samplePosition);
    soundingNote = note;

    // A note lasts exactly as long as it was drawn, down to a sixteenth of a
    // step - so a block across four steps sounds for four, and a stab a
    // fraction of a step long sounds for that.
    double tickSamples = (double) stepSamples / (double) ticksPerStep;
    double lengthInSamples = (double) juce::jmax(1, step.length) * tickSamples;
    samplesUntilNoteOff = juce::jmax(1, (int) (lengthInSamples * juce::jlimit(0.05f, 1.0f, step.gate)));
}

void StepSequencer::beginRecordedNote(int note, double atTick, float velocity, int patternSteps,
                                       const SynthParameters& params)
{
    bool quantise = params.seqQuantise.load() > 0.5f;

    double stepPosition = atTick / (double) ticksPerStep;
    int startStep;
    int tickOffset = 0;

    if (quantise)
    {
        // Snapped to the nearest step, which is what input quantise does: play
        // slightly late and the note still lands on the beat.
        startStep = ((int) std::llround(stepPosition)) % patternSteps;
    }
    else
    {
        // Left where it fell, down to the tick: the step it happened in, plus
        // how late inside that step it was.
        startStep = ((int) std::floor(stepPosition)) % patternSteps;
        tickOffset = juce::jlimit(0, ticksPerStep - 1,
                                   (int) std::floor(atTick) - startStep * ticksPerStep);
    }

    startStep = juce::jlimit(0, patternSteps - 1, startStep);

    int key = juce::jlimit(0, music::numKeys - 1, (int) std::round(params.musicalKey.load()));
    auto scale = (music::Scale) juce::jlimit(0, (int) music::Scale::numScales - 1,
                                              (int) std::round(params.musicalScale.load()));
    int rootNote = juce::jlimit(0, 127, (int) std::round(params.seqRootNote.load()));

    // Played notes arrive as pitches and have to become degrees, so they land
    // on a row of the grid and move with the key like everything else.
    int degree;
    if (scale == music::Scale::chromatic)
    {
        degree = note - rootNote;
    }
    else
    {
        int rootDegree = music::degreeForNote(music::snapToScale(rootNote, key, scale), key, scale);
        int playedDegree = music::degreeForNote(music::snapToScale(note, key, scale), key, scale);
        degree = playedDegree - rootDegree;
    }

    placeNote(startStep, degree, 1, juce::jlimit(0.05f, 1.0f, velocity));

    {
        const juce::SpinLock::ScopedLockType lock(patternLock);
        steps[(size_t) startStep].tickOffset = tickOffset;
    }

    auto& recorded = recordedNotes[juce::jlimit(0, 127, note)];
    recorded.active = true;
    recorded.startStep = startStep;
    recorded.startTick = atTick;
}

void StepSequencer::endRecordedNote(int note, double atTick, int patternSteps)
{
    auto& recorded = recordedNotes[juce::jlimit(0, 127, note)];
    if (!recorded.active)
        return;

    recorded.active = false;

    double patternTicks = (double) patternSteps * ticksPerStep;
    double held = atTick - recorded.startTick;
    if (held < 0.0)
        held += patternTicks; // the note was still down when the loop came round

    // Kept in ticks, so a note played short stays short instead of being
    // rounded up to a whole step.
    int length = juce::jlimit(1, patternSteps * ticksPerStep, (int) std::llround(held));

    setNoteLength(recorded.startStep, length);
}

void StepSequencer::captureIncomingNotes(const juce::MidiBuffer& midi, double blockStartTick,
                                          double ticksPerSample, int patternSteps,
                                          const SynthParameters& params)
{
    double patternTicks = (double) patternSteps * ticksPerStep;

    for (const auto metadata : midi)
    {
        auto message = metadata.getMessage();
        if (!message.isNoteOn() && !message.isNoteOff())
            continue;

        double atTick = blockStartTick + metadata.samplePosition * ticksPerSample;
        atTick = std::fmod(atTick, patternTicks);
        if (atTick < 0.0)
            atTick += patternTicks;

        if (message.isNoteOn())
            beginRecordedNote(message.getNoteNumber(), atTick, message.getFloatVelocity(),
                              patternSteps, params);
        else
            endRecordedNote(message.getNoteNumber(), atTick, patternSteps);
    }
}

void StepSequencer::applyAutomation(SynthParameters& params, int tick)
{
    const ParameterMask touched = heldParameters();

    const juce::SpinLock::ScopedLockType lock(patternLock);

    for (int id = 0; id < paramreg::count(); ++id)
    {
        // A dial the user is holding wins over what was recorded, so grabbing a
        // knob during playback takes over rather than fighting the lane.
        if (!lanes[(size_t) id].used || touched.test(id))
            continue;

        paramreg::writeNormalised(params, id, lanes[(size_t) id].values[(size_t) tick]);
    }
}

void StepSequencer::recordAutomation(const SynthParameters& params, int tick)
{
    // Anything being moved right now joins the latch and stays in it until the
    // pass ends. Without this a parameter is handed back to its lane the moment
    // you stop moving it, so a pass made of several adjustments plays back as
    // the new value, then whatever was recorded before, then the new value
    // again - which is the jerkiness. Having moved a control once during a
    // recording pass is a statement about the whole pass.
    latchedParameters.merge(touchedParameters.load() | controllerTouched.load());

    const ParameterMask touched = heldParameters();
    if (!touched.any())
        return;

    const juce::SpinLock::ScopedLockType lock(patternLock);

    for (int id = 0; id < paramreg::count(); ++id)
    {
        if (!touched.test(id))
            continue;

        auto& lane = lanes[(size_t) id];

        // First touch of an empty lane fills the whole bar with where the dial
        // is now, so the parameter does not snap back to zero on the steps that
        // were never passed over.
        if (!lane.used)
        {
            lane.values.fill(paramreg::readNormalised(params, id));
            lane.used = true;
        }

        // Writing over the tick as it passes is what makes a second pass
        // overwrite the first.
        lane.values[(size_t) tick] = paramreg::readNormalised(params, id);
    }
}

SequencerState StepSequencer::captureState() const
{
    SequencerState state;
    const juce::SpinLock::ScopedLockType lock(patternLock);

    state.steps.reserve((size_t) maxSteps);
    for (const auto& step : steps)
    {
        SequencerState::StepData data;
        data.active = step.active ? 1 : 0;
        data.degree = step.degree;
        data.octave = step.octave;
        data.length = step.length;
        data.tickOffset = step.tickOffset;
        data.locked = step.locked ? 1 : 0;
        data.velocity = step.velocity;
        data.gate = step.gate;
        data.probability = step.probability;
        data.slide = step.slide ? 1 : 0;
        data.accent = step.accent ? 1 : 0;
        state.steps.push_back(data);
    }

    for (int id = 0; id < paramreg::maxParameters; ++id)
    {
        if (!lanes[(size_t) id].used)
            continue;

        SequencerState::LaneData lane;
        lane.parameterId = id;
        lane.values.assign(lanes[(size_t) id].values.begin(), lanes[(size_t) id].values.end());
        state.lanes.push_back(std::move(lane));
    }

    // Only voices with something on them, the same rule the lanes follow. A
    // project carries the beat that was written rather than sixteen empty rows,
    // and a patch with no drums in it adds nothing to the file at all.
    for (int v = 0; v < numDrumVoices; ++v)
    {
        bool used = false;
        for (const auto& cell : drumCells[(size_t) v])
            if (cell.active)
            {
                used = true;
                break;
            }

        if (!used)
            continue;

        SequencerState::DrumVoiceData saved;
        saved.voice = v;
        saved.cells.reserve((size_t) numDrumCells);

        for (const auto& cell : drumCells[(size_t) v])
        {
            SequencerState::DrumCellData data;
            data.active = cell.active ? 1 : 0;
            data.velocity = cell.velocity;
            data.probability = cell.probability;
            data.accent = cell.accent ? 1 : 0;
            data.nudge = cell.nudge;
            saved.cells.push_back(data);
        }

        state.drumVoices.push_back(std::move(saved));
    }

    return state;
}

void StepSequencer::restoreState(const SequencerState& state)
{
    if (state.isEmpty())
        return;

    const juce::SpinLock::ScopedLockType lock(patternLock);

    for (size_t i = 0; i < steps.size(); ++i)
    {
        if (i < state.steps.size())
        {
            const auto& data = state.steps[i];
            auto& step = steps[i];
            step.active = data.active != 0;
            step.degree = data.degree;
            step.octave = data.octave;
            step.length = juce::jlimit(1, maxTicks, data.length);
            step.tickOffset = juce::jlimit(0, ticksPerStep - 1, data.tickOffset);
            step.locked = data.locked != 0;
            step.velocity = juce::jlimit(0.0f, 1.0f, data.velocity);
            step.gate = juce::jlimit(0.05f, 1.0f, data.gate);
            step.probability = juce::jlimit(0.0f, 1.0f, data.probability);
            step.slide = data.slide != 0;
            step.accent = data.accent != 0;
        }
        else
        {
            steps[i] = Step{};
        }
    }

    // Every row emptied first, so loading a project replaces the beat rather
    // than merging into whatever was there. A saved pattern names the voices it
    // uses, so a voice it does not name has to be silenced explicitly.
    for (auto& voice : drumCells)
        for (auto& cell : voice)
            cell = DrumCell{};

    for (const auto& saved : state.drumVoices)
    {
        if (saved.voice < 0 || saved.voice >= numDrumVoices)
            continue;

        auto& row = drumCells[(size_t) saved.voice];

        for (size_t i = 0; i < row.size() && i < saved.cells.size(); ++i)
        {
            const auto& data = saved.cells[i];
            row[i].active = data.active != 0;
            row[i].velocity = juce::jlimit(0.0f, 1.0f, data.velocity);
            row[i].probability = juce::jlimit(0.0f, 1.0f, data.probability);
            row[i].accent = data.accent != 0;
            row[i].nudge = juce::jlimit(-maxDrumNudge, maxDrumNudge, data.nudge);
        }
    }

    for (auto& lane : lanes)
    {
        lane.used = false;
        lane.values.fill(0.0f);
    }

    for (const auto& saved : state.lanes)
    {
        if (saved.parameterId < 0 || saved.parameterId >= paramreg::maxParameters)
            continue;

        auto& lane = lanes[(size_t) saved.parameterId];
        lane.used = true;

        // Stretched rather than copied, so a lane drawn when the timeline had a
        // coarser tick still covers the same stretch of music instead of being
        // squashed into the opening bars.
        for (size_t t = 0; t < lane.values.size(); ++t)
        {
            if (saved.values.empty())
            {
                lane.values[t] = 0.0f;
                continue;
            }

            size_t source = t * saved.values.size() / lane.values.size();
            lane.values[t] = juce::jlimit(0.0f, 1.0f, saved.values[juce::jmin(source, saved.values.size() - 1)]);
        }
    }
}

void StepSequencer::process(juce::MidiBuffer& midi, int numSamples, SynthParameters& params,
                            const tempo::MusicalClock& clock)
{
    // Before the transport is even looked at, so a stopped sequencer cannot
    // hand back the hits from the last block it played. Whoever drains these
    // does so after every call, and "the block that just ran" has to mean that
    // even when the block that just ran produced nothing.
    numDrumHits = 0;

    bool shouldPlay = params.seqPlaying.load() > 0.5f;
    playing.store(shouldPlay);

    if (!shouldPlay)
    {
        if (wasPlaying)
        {
            juce::MidiBuffer cleanup;
            stopSoundingNote(cleanup, 0);
            for (const auto metadata : midi)
                cleanup.addEvent(metadata.getMessage(), metadata.samplePosition);
            midi.swapWith(cleanup);

            // Back to the top. The clock itself is rewound by the processor
            // when play starts, so both this and the arpeggiator begin the
            // next take from the same instant.
            lastTick = -1;
            currentStep.store(0);
        }

        // A pass ends when the transport does, so nothing stays latched into the
        // next one - otherwise pressing play again would find half the panel
        // already claimed by whatever was moved last time.
        latchedParameters.clearAll();
        wasRecording = false;

        wasPlaying = false;
        return;
    }

    wasPlaying = true;

    int length = juce::jlimit(1, maxSteps, (int) std::round(params.seqLength.load()));
    int division = juce::jlimit(0, tempo::getNumDivisions() - 1, (int) std::round(params.seqDivision.load()));
    bool recording = params.seqRecord.load() > 0.5f;

    // Arming or disarming REC starts a new pass, and a pass starts with nothing
    // claimed. Otherwise the first thing recorded last time would still be
    // overriding its own lane the next time round.
    if (recording != wasRecording)
    {
        wasRecording = recording;
        latchedParameters.clearAll();
    }

    // Position comes from the shared clock rather than a counter of our own,
    // so the grid sits at fixed musical instants and anything else running at
    // a linked rate lands on exactly those instants too.
    double beatsPerStep = juce::jmax(1.0e-6, tempo::divisionBeats(division));
    double beatsPerTick = beatsPerStep / (double) ticksPerStep;
    double ticksPerSample = clock.getBeatsPerSample() / beatsPerTick;
    int stepSamples = juce::jmax(1, (int) (beatsPerStep * clock.samplesPerBeat()));

    int ticksInPattern = length * ticksPerStep;

    // Where the pattern is now, wrapped into its own length.
    double absoluteTick = clock.getBlockStartBeat() / beatsPerTick;
    double tickPosition = std::fmod(absoluteTick, (double) ticksInPattern);
    if (tickPosition < 0.0)
        tickPosition += (double) ticksInPattern;

    // What is being played is captured onto the grid before the sequencer adds
    // its own notes, so the two never get confused for each other.
    if (recording && params.seqRecordNotes.load() > 0.5f)
        captureIncomingNotes(midi, tickPosition, ticksPerSample, length, params);

    juce::MidiBuffer generated;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // A note tied into the next step outlasts its own gate: it has to still
        // be sounding when the next one arrives, or there is nothing to slide
        // from. The countdown is left alone too, since triggerStep resets it
        // for whatever comes next.
        if (soundingNote >= 0 && !sustainingForSlide && --samplesUntilNoteOff <= 0)
            stopSoundingNote(generated, sample);

        int tick = ((int) tickPosition) % ticksInPattern;

        if (tick != lastTick)
        {
            lastTick = tick;
            currentTick.store(tick);

            if (recording)
                recordAutomation(params, tick);

            applyAutomation(params, tick);

            int stepIndex = tick / ticksPerStep;
            currentStep.store(stepIndex);

            // A step fires at its own offset inside its step rather than always
            // on the boundary, which is what lets an unquantised recording keep
            // the timing it was played with.
            auto step = getStep(stepIndex);
            if (step.active && (tick % ticksPerStep) == juce::jlimit(0, ticksPerStep - 1, step.tickOffset))
            {
                // The ordering here is the whole feature, so it is worth being
                // explicit about why it is this way round.
                //
                // Normally a step silences whatever was playing and then plays
                // its own note. A step that the PREVIOUS one slid into must not:
                // MonoVoice only bends a sounding note when the new note-on
                // arrives while the old note is still held. Release first and it
                // sees the line end and a fresh one begin, and starts a new note
                // at the new pitch - which is exactly what every sequencer note
                // did before this, whatever the glide and legato dials said.
                const int slidFrom = sustainingForSlide ? soundingNote : -1;

                if (slidFrom < 0)
                    stopSoundingNote(generated, sample);

                triggerStep(generated, sample, stepIndex, stepSamples, params);

                // ...and the note being slid out of goes AFTER the new one, at
                // the same instant, so that for that instant both are held.
                // MidiBuffer keeps events in the order they were added within a
                // sample, so "on then off" survives to MonoVoice, which then
                // has the newer note as its target and lets the older one go.
                //
                // Leave this out and the old note never lifts: MonoVoice would
                // still be holding it, and when the new note's own gate ended it
                // would slide BACK to it and sustain there for good.
                //
                // It also covers the step a slide was aiming at not sounding at
                // all - a chance roll can skip it - in which case this is simply
                // the release that was held back, arriving a step late.
                if (slidFrom >= 0)
                {
                    generated.addEvent(juce::MidiMessage::noteOff(1, slidFrom), sample);

                    if (soundingNote == slidFrom)
                        soundingNote = -1; // nothing new started; that was a plain release
                }

                // Slide needs somewhere to slide to and a voice that bends
                // rather than restarts. Without mono and legato the held note
                // would simply overlap the next one and sound as a chord, so
                // the step is played straight instead - wrong is better than
                // surprising.
                const bool canSlide = params.monoMode.load() > 0.5f && params.legatoMode.load() > 0.5f;
                sustainingForSlide = soundingNote >= 0 && step.slide && canSlide;
            }

            // The drum pattern, on the same tick and the same transport. It
            // runs whether or not the melodic step above it sounded: both
            // patterns are always playing, and the tab in the editor only
            // chooses which one you are drawing on.
            scanDrumCells(tick, sample, ticksInPattern);
        }

        tickPosition += ticksPerSample;
        if (tickPosition >= (double) ticksInPattern)
            tickPosition -= (double) ticksInPattern;
    }

    // The sequencer plays alongside the keyboard rather than replacing it, so
    // its notes are merged in rather than swapped over.
    for (const auto metadata : generated)
        midi.addEvent(metadata.getMessage(), metadata.samplePosition);
}
} // namespace wavelathe
