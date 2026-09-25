// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <algorithm>
#include "SequencerPanel.h"
#include "ParameterRegistry.h"
#include "Scales.h"
#include "TempoSync.h"
#include <limits>

namespace wavelathe
{
namespace
{
constexpr int rulerHeight = 18;
constexpr int velocityHeight = 40;
constexpr int probabilityHeight = 32;
constexpr int octaveHeight = 15;
constexpr int slideHeight = 15;
constexpr int accentHeight = 15;
constexpr int lockHeight = 15;
constexpr int footerHeight =
    velocityHeight + probabilityHeight + octaveHeight + accentHeight + slideHeight + lockHeight;

// Reading the key and scale the grid is currently drawn in.
music::Scale scaleOf(const SynthParameters& params)
{
    return (music::Scale) juce::jlimit(0, (int) music::Scale::numScales - 1,
                                        (int) std::round(params.musicalScale.load()));
}

int keyOf(const SynthParameters& params)
{
    return juce::jlimit(0, music::numKeys - 1, (int) std::round(params.musicalKey.load()));
}

// The MIDI note a degree plays, given where the pattern is rooted.
int noteForDegreeInPattern(const SynthParameters& params, int degree)
{
    int key = keyOf(params);
    auto scale = scaleOf(params);
    int rootNote = juce::jlimit(0, 127, (int) std::round(params.seqRootNote.load()));

    if (scale == music::Scale::chromatic)
        return juce::jlimit(0, 127, rootNote + degree);

    int rootDegree = music::degreeForNote(music::snapToScale(rootNote, key, scale), key, scale);
    return juce::jlimit(0, 127, music::noteForDegree(rootDegree + degree, key, scale));
}
} // namespace

// ============================================================================
// StepGrid
// ============================================================================

StepGrid::StepGrid(WaveLatheProcessor& processor, TimeView& view)
    : processorRef(processor), timeView(view) {}

void StepGrid::mouseUp(const juce::MouseEvent&)
{
    dragZone = Zone::none;
    resizingNote = -1;
}

void StepGrid::mouseMove(const juce::MouseEvent& event)
{
    // The cursor says what a drag here would do: a resize arrow over the right
    // edge of a note, the normal pointer everywhere else.
    setMouseCursor(noteEdgeAt(event.getPosition()) >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                                        : juce::MouseCursor::NormalCursor);
}

void StepGrid::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    int direction = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
    if (direction == 0)
        return;

    // The arrangement every piano roll uses: the wheel moves up and down the
    // keyboard, shift turns it sideways through time, ctrl zooms time.
    bool zoom = event.mods.isCtrlDown() || event.mods.isCommandDown();
    bool horizontal = event.mods.isShiftDown();

    if (!zoom && !horizontal)
    {
        setTopDegree(topDegree() + direction * juce::jmax(1, numRows() / 4));
        return;
    }

    if (onViewShouldChange)
        onViewShouldChange(direction, zoom);
}

int StepGrid::noteEdgeAt(juce::Point<int> position) const
{
    if (!noteArea().contains(position))
        return -1;

    int usable = getWidth() - gutterWidth;
    if (usable <= 0 || timeView.visibleSteps <= 0)
        return -1;

    float stepWidth = (float) usable / (float) timeView.visibleSteps;
    int grabWidth = juce::jlimit(4, 10, (int) (stepWidth * 0.3f));

    auto& sequencer = processorRef.getSequencer();
    int rows = numRows();
    int rowHeight = juce::jmax(1, noteArea().getHeight() / rows);
    int row = juce::jlimit(0, rows - 1, (position.y - noteArea().getY()) / rowHeight);
    int degree = degreeForRow(row);

    for (int step = 0; step < StepSequencer::maxSteps; ++step)
    {
        auto data = sequencer.getStep(step);
        if (!data.active || data.degree != degree)
            continue;

        float endInSteps = (float) StepSequencer::endTickOf(step, data) / (float) StepSequencer::ticksPerStep;
        int endX = gutterWidth + (int) ((endInSteps - (float) timeView.firstStep) * stepWidth);

        if (std::abs(position.x - endX) <= grabWidth)
            return step;
    }

    return -1;
}

namespace
{
// How tall a row wants to be to stay readable. The number of rows on screen
// follows from the space available rather than the other way round, which is
// what stops a short window from squeezing three octaves into a smear.
constexpr int preferredRowHeight = 13;
constexpr int minimumVisibleRows = 6;

// Pattern lengths: every step up to a bar of sixteen, whole bars beyond that.
int snapPatternLength(double value)
{
    int steps = juce::jlimit(1, StepSequencer::maxSteps, (int) std::round(value));

    if (steps <= 16)
        return steps;

    constexpr int stepsPerBar = 4;
    return juce::jlimit(16, StepSequencer::maxSteps,
                        ((steps + stepsPerBar / 2) / stepsPerBar) * stepsPerBar);
}

int degreesPerOctave(music::Scale scale)
{
    return scale == music::Scale::chromatic ? 12 : (int) music::scaleIntervals(scale).size();
}
} // namespace

int StepGrid::highestDegree() const
{
    return degreesPerOctave(scaleOf(processorRef.getParameters())) * 3;
}

int StepGrid::lowestDegree() const
{
    return -degreesPerOctave(scaleOf(processorRef.getParameters())) * 2;
}

int StepGrid::totalRows() const
{
    return highestDegree() - lowestDegree() + 1;
}

int StepGrid::numRows() const
{
    auto area = getLocalBounds().withTrimmedTop(rulerHeight);
    area.removeFromBottom(footerHeight);

    int fits = area.getHeight() / preferredRowHeight;
    return juce::jlimit(minimumVisibleRows, totalRows(), juce::jmax(1, fits));
}

void StepGrid::initialisePitch()
{
    if (pitchInitialised)
        return;

    pitchInitialised = true;

    // Opens looking at the octave above the root, which is where a pattern
    // written by Generate mostly sits.
    int perOctave = degreesPerOctave(scaleOf(processorRef.getParameters()));
    setTopDegree(perOctave + numRows() / 3);
}

int StepGrid::topDegree() const
{
    return juce::jlimit(lowestDegree() + numRows() - 1, highestDegree(), pitchTop);
}

void StepGrid::setTopDegree(int degree)
{
    int clamped = juce::jlimit(lowestDegree() + numRows() - 1, highestDegree(), degree);
    if (clamped == pitchTop && pitchInitialised)
        return;

    pitchTop = clamped;
    pitchInitialised = true;
    repaint();

    if (onPitchViewChanged)
        onPitchViewChanged();
}

void StepGrid::scrollToShowPattern()
{
    auto& sequencer = processorRef.getSequencer();

    int highest = std::numeric_limits<int>::min();
    int lowest = std::numeric_limits<int>::max();

    for (int step = 0; step < StepSequencer::maxSteps; ++step)
    {
        auto data = sequencer.getStep(step);
        if (!data.active)
            continue;

        highest = juce::jmax(highest, data.degree);
        lowest = juce::jmin(lowest, data.degree);
    }

    if (highest == std::numeric_limits<int>::min())
        return; // nothing written yet, so nothing to look at

    // Already visible: leave the view where the user put it.
    int top = topDegree();
    int bottom = top - numRows() + 1;
    if (highest <= top && lowest >= bottom)
        return;

    // Centre what is there, so a pattern spanning more than fits still opens
    // showing its middle rather than one end of it.
    setTopDegree((highest + lowest) / 2 + numRows() / 2);
}

int StepGrid::degreeForRow(int row) const
{
    return topDegree() - row;
}

int StepGrid::rowForDegree(int degree) const
{
    return topDegree() - degree;
}

bool StepGrid::isRootRow(int degree) const
{
    auto scale = scaleOf(processorRef.getParameters());
    int perOctave = scale == music::Scale::chromatic ? 12 : (int) music::scaleIntervals(scale).size();
    return ((degree % perOctave) + perOctave) % perOctave == 0;
}

juce::String StepGrid::labelForDegree(int degree) const
{
    int note = noteForDegreeInPattern(processorRef.getParameters(), degree);
    return juce::MidiMessage::getMidiNoteName(note, true, true, 3);
}

juce::Rectangle<int> StepGrid::rulerArea() const
{
    return getLocalBounds().removeFromTop(rulerHeight);
}

juce::Rectangle<int> StepGrid::noteArea() const
{
    auto area = getLocalBounds().withTrimmedTop(rulerHeight);
    area.removeFromBottom(footerHeight);
    return area.withTrimmedLeft(gutterWidth);
}

// The six footer rows stack upward from the bottom edge, so each one only has
// to know what sits below it. Accent and slide sit together above the lock:
// they are the two things a step does when it PLAYS, where the lock is about
// editing and belongs at the end.
juce::Rectangle<int> StepGrid::velocityArea() const
{
    auto area = getLocalBounds();
    area.removeFromBottom(lockHeight + slideHeight + accentHeight + octaveHeight + probabilityHeight);
    return area.removeFromBottom(velocityHeight).withTrimmedLeft(gutterWidth);
}

juce::Rectangle<int> StepGrid::probabilityArea() const
{
    auto area = getLocalBounds();
    area.removeFromBottom(lockHeight + slideHeight + accentHeight + octaveHeight);
    return area.removeFromBottom(probabilityHeight).withTrimmedLeft(gutterWidth);
}

juce::Rectangle<int> StepGrid::octaveArea() const
{
    auto area = getLocalBounds();
    area.removeFromBottom(lockHeight + slideHeight + accentHeight);
    return area.removeFromBottom(octaveHeight).withTrimmedLeft(gutterWidth);
}

juce::Rectangle<int> StepGrid::accentArea() const
{
    auto area = getLocalBounds();
    area.removeFromBottom(lockHeight + slideHeight);
    return area.removeFromBottom(accentHeight).withTrimmedLeft(gutterWidth);
}

juce::Rectangle<int> StepGrid::slideArea() const
{
    auto area = getLocalBounds();
    area.removeFromBottom(lockHeight);
    return area.removeFromBottom(slideHeight).withTrimmedLeft(gutterWidth);
}

juce::Rectangle<int> StepGrid::lockArea() const
{
    return getLocalBounds().removeFromBottom(lockHeight).withTrimmedLeft(gutterWidth);
}

int StepGrid::stepForX(int x) const
{
    int usable = getWidth() - gutterWidth;
    if (usable <= 0 || timeView.visibleSteps <= 0)
        return -1;

    int step = timeView.firstStep + ((x - gutterWidth) * timeView.visibleSteps) / usable;
    return juce::jlimit(0, StepSequencer::maxSteps - 1, step);
}

// The tick under a point, which is what note lengths are measured in - a step
// alone is too coarse now that a note can be a fraction of one.
int StepGrid::tickForX(int x) const
{
    int usable = getWidth() - gutterWidth;
    if (usable <= 0 || timeView.visibleSteps <= 0)
        return 0;

    double intoView = (double) (x - gutterWidth) * (double) (timeView.visibleSteps * StepSequencer::ticksPerStep)
                      / (double) usable;

    return juce::jlimit(0, StepSequencer::maxTicks - 1,
                        timeView.firstStep * StepSequencer::ticksPerStep + (int) std::floor(intoView));
}

juce::String StepGrid::getTooltip()
{
    // Every row rect is trimmed of the gutter, because that is where the cells
    // are drawn and where clicks land. A hover is not a click: the label - and
    // the [i] beside SLIDE - live IN the gutter, so asking the cell rects
    // whether they contain the pointer answered no for the one part of the row
    // a person points at to find out what it is. Put the gutter back for this.
    auto wholeRow = [](juce::Rectangle<int> cells) { return cells.withLeft(0); };
    const auto at = getMouseXYRelative();
    // Slide is the one row whose behaviour depends on something outside the
    // sequencer, and there is no room in a 62-pixel gutter to say so. The
    // marker beside the label says an explanation exists; this is it.
    if (wholeRow(slideArea()).contains(at))
    {
        const auto& params = processorRef.getParameters();
        bool ready = params.monoMode.load() > 0.5f && params.legatoMode.load() > 0.5f;

        // Written as running sentences with no line breaks of their own - the
        // tooltip wraps them to its own width, and a newline put in here would
        // break the text somewhere that has nothing to do with how wide the
        // box turns out to be.
        // Both branches name where the switches are. Saying it only when they
        // are off tells you nothing on the day you turn one off and wonder why
        // the ties went hollow - and Mono and Legato are two sections and a
        // page away from the row that depends on them.
        if (ready)
            return "SLIDE ties this step to the next, so the line bends between them instead "
                   "of playing the next note afresh. It works here because Mono and Legato are "
                   "both on in the Oscillator 1 section of the Synth page - switch either off and "
                   "ties stop bending. The Glide dial beside them sets how long the bend takes.";

        return "SLIDE needs Mono and Legato, the two switches in the Oscillator 1 section of the "
               "Synth page, beside the Glide dial. Both must be on. Without them there is no "
               "single line to bend, so a tied step just plays normally - which is why these ties "
               "are drawn hollow.";
    }

    if (wholeRow(accentArea()).contains(at))
        return "ACCENT plays this step at the pattern's accent level instead of its own velocity. "
               "The Accent dial sets how hard, and Vel>Cut and Vel>Res decide how much of that "
               "becomes brightness and bite rather than loudness.";

    if (wholeRow(lockArea()).contains(at))
        return "LOCK protects this step from Generate. Its note, length, timing, velocity, accent "
               "and tie are all kept while Generate rewrites the steps around it, and the new line "
               "still carries on from its pitch. Write a bar, lock the parts that work, and keep "
               "pressing Generate for the rest.";

    if (wholeRow(octaveArea()).contains(at))
        return "OCTAVE shifts this step up or down, from one octave below to two above. Click to "
               "cycle. A step at the middle octave is drawn as a quiet dash.";

    if (wholeRow(probabilityArea()).contains(at))
        return "CHANCE is how likely this step is to sound each time round. Drag up for certain, "
               "down for never. Anything below certain makes the pattern vary as it repeats rather "
               "than playing the same bar forever.";

    if (wholeRow(velocityArea()).contains(at))
        return "VELOCITY is how hard this step plays. Drag to shape it, or across several steps to "
               "draw a run. Velocity also opens the filter and moves the wavetable, by as much as "
               "the Vel> dials on the synth page allow.";

    return {};
}

StepGrid::Zone StepGrid::zoneFor(juce::Point<int> position) const
{
    if (position.x < gutterWidth)
        return Zone::none;

    if (lockArea().contains(position))
        return Zone::lock;

    if (accentArea().contains(position))
        return Zone::accent;

    if (slideArea().contains(position))
        return Zone::slide;

    if (octaveArea().contains(position))
        return Zone::octave;

    if (probabilityArea().contains(position))
        return Zone::probability;

    if (velocityArea().contains(position))
        return Zone::velocity;

    if (noteArea().contains(position))
        return Zone::notes;

    return Zone::none;
}

void StepGrid::mouseDown(const juce::MouseEvent& event)
{
    // One undo entry per gesture, taken before anything changes - a drag across
    // the velocity row is one thing you did, not forty.
    processorRef.recordUndoPoint("Edit Pattern");

    // Grabbing the right edge of a note resizes it rather than placing a new
    // one, so the same click has two meanings depending on where it lands.
    resizingNote = noteEdgeAt(event.getPosition());

    if (resizingNote >= 0)
    {
        dragZone = Zone::none;
        return;
    }

    dragZone = zoneFor(event.getPosition());
    handleMouse(event, false);
}

void StepGrid::mouseDrag(const juce::MouseEvent& event)
{
    if (resizingNote >= 0)
    {
        auto data = processorRef.getSequencer().getStep(resizingNote);
        int startTick = StepSequencer::startTickOf(resizingNote, data);
        int length = juce::jlimit(1, StepSequencer::maxTicks - startTick,
                                   tickForX(event.x) - startTick + 1);

        // Magnetic to whole steps: within a tick of one, it takes it. Fine
        // control is what the ticks are for, but a plain one-step note should
        // not need a steady hand.
        int nearestWholeStep = juce::jmax(1, ((length + StepSequencer::ticksPerStep / 2)
                                              / StepSequencer::ticksPerStep) * StepSequencer::ticksPerStep);
        if (std::abs(nearestWholeStep - length) <= 1)
            length = nearestWholeStep;

        processorRef.getSequencer().setNoteLength(resizingNote, length);

        if (onPatternChanged)
            onPatternChanged();

        repaint();
        return;
    }

    handleMouse(event, true);
}

void StepGrid::handleMouse(const juce::MouseEvent& event, bool isDrag)
{
    if (dragZone == Zone::none)
        return;

    int stepIndex = stepForX(event.x);
    if (stepIndex < 0)
        return;

    auto& sequencer = processorRef.getSequencer();
    auto step = sequencer.getStep(stepIndex);

    if (dragZone == Zone::notes)
    {
        auto area = noteArea();
        int rows = numRows();
        int rowHeight = juce::jmax(1, area.getHeight() / rows);
        int row = juce::jlimit(0, rows - 1, (event.y - area.getY()) / rowHeight);
        int degree = degreeForRow(row);

        // Clicking the block that is already there removes it, the way every
        // step grid behaves; dragging only ever paints.
        if (!isDrag && step.active && step.degree == degree)
        {
            sequencer.clearNote(stepIndex);
        }
        else
        {
            // Placing goes through the sequencer so the new note takes over the
            // steps it covers instead of overlapping what is already there.
            // A note drawn by hand is one whole step; one that already exists
            // keeps the length it was given. The tick grid is for shaping a note
            // after it exists, not for what a click on an empty square gives.
            if (step.active)
                sequencer.placeNote(stepIndex, degree, juce::jmax(1, step.length), step.velocity);
            else
                sequencer.placeNote(stepIndex, degree, 0.85f);
        }

        if (onPatternChanged)
            onPatternChanged();

        repaint();
        return;
    }

    if (dragZone == Zone::velocity)
    {
        auto area = velocityArea();
        float fromTop = (float) (event.y - area.getY()) / juce::jmax(1.0f, (float) area.getHeight());
        step.velocity = juce::jlimit(0.05f, 1.0f, 1.0f - fromTop);
    }
    else if (dragZone == Zone::probability)
    {
        // Painted the same way velocity is, because it is the same kind of
        // thing: a value per step you want to shape across a bar, not set one
        // step at a time.
        float fromTop = (float) (event.y - probabilityArea().getY())
                        / juce::jmax(1.0f, (float) probabilityArea().getHeight());
        step.probability = juce::jlimit(0.0f, 1.0f, 1.0f - fromTop);
    }
    else if (dragZone == Zone::octave)
    {
        if (isDrag)
            return; // a click cycles it; dragging across would be an accident

        // Up through the useful range and back to the middle, so one control
        // reaches everything without needing a direction.
        step.octave = step.octave >= 2 ? -1 : step.octave + 1;
    }
    else if (dragZone == Zone::accent)
    {
        if (isDrag)
            return; // a switch, not a value to paint across

        step.accent = !step.accent;
    }
    else if (dragZone == Zone::slide)
    {
        if (isDrag)
            return; // a switch, like the lock below it, not a value to paint across

        step.slide = !step.slide;
    }
    else if (dragZone == Zone::lock)
    {
        if (isDrag)
            return; // a lock is a switch, not something to paint across

        step.locked = !step.locked;
    }

    sequencer.setStep(stepIndex, step);

    if (onPatternChanged)
        onPatternChanged();

    repaint();
}

void StepGrid::paint(juce::Graphics& g)
{
    initialisePitch();

    const auto& params = processorRef.getParameters();
    auto& sequencer = processorRef.getSequencer();

    int playingStep = sequencer.isPlaying() ? sequencer.getCurrentStep() : -1;

    auto notes = noteArea();
    auto velocity = velocityArea();
    auto probability = probabilityArea();
    auto octaves = octaveArea();
    auto accents = accentArea();
    auto slides = slideArea();
    auto locks = lockArea();
    int usable = getWidth() - gutterWidth;
    if (usable <= 0)
        return;

    // Only the steps inside the view are drawn, and a step's column position
    // is measured from the first visible one.
    const int firstStep = timeView.firstStep;
    const int visibleSteps = juce::jmax(1, timeView.visibleSteps);
    const int lastStep = juce::jmin(StepSequencer::maxSteps, firstStep + visibleSteps);

    auto columnFor = [this, usable, firstStep, visibleSteps](int step, int y, int height)
    {
        int x = gutterWidth + ((step - firstStep) * usable) / visibleSteps;
        int next = gutterWidth + ((step + 1 - firstStep) * usable) / visibleSteps;
        return juce::Rectangle<int>(x, y, next - x, height);
    };

    int patternLength = juce::jlimit(1, StepSequencer::maxSteps,
                                      (int) std::round(params.seqLength.load()));

    // ---- Ruler -----------------------------------------------------------
    g.setColour(ui::colours::panel);
    g.fillRect(rulerArea());

    for (int step = firstStep; step < lastStep; ++step)
    {
        auto column = columnFor(step, 0, rulerHeight);

        if (step == playingStep)
        {
            g.setColour(ui::colours::accent.withAlpha(0.35f));
            g.fillRect(column);
        }

        // Beats are numbered, the sixteenths between them are dots: the same
        // hierarchy a bar ruler uses, so four groups of four read at a glance.
        bool onBeat = (step % 4 == 0);
        g.setColour(step == playingStep ? ui::colours::accent
                                        : (onBeat ? ui::colours::textPrimary : ui::colours::textDim));
        g.setFont(juce::Font(juce::FontOptions(onBeat ? 11.0f : 9.0f, onBeat ? juce::Font::bold : juce::Font::plain)));
        g.drawText(onBeat ? juce::String(step / 4 + 1) : juce::String("."), column,
                   juce::Justification::centred);
    }

    // ---- Note grid -------------------------------------------------------
    int rows = numRows();
    int rowHeight = juce::jmax(1, notes.getHeight() / rows);

    g.setColour(ui::colours::background.darker(0.3f));
    g.fillRect(notes);

    for (int row = 0; row < rows; ++row)
    {
        int degree = degreeForRow(row);
        auto rowRect = juce::Rectangle<int>(notes.getX(), notes.getY() + row * rowHeight,
                                             notes.getWidth(), rowHeight);

        // The key strip: root rows are the anchors, the equivalent of the C
        // rows a piano roll marks.
        bool root = isRootRow(degree);
        g.setColour(root ? ui::colours::panel.brighter(0.12f) : ui::colours::panel.withAlpha(0.35f));
        g.fillRect(rowRect);

        g.setColour(ui::colours::background.withAlpha(0.6f));
        g.drawHorizontalLine(rowRect.getBottom() - 1, (float) rowRect.getX(), (float) rowRect.getRight());

        auto labelRect = juce::Rectangle<int>(0, rowRect.getY(), gutterWidth, rowHeight);
        g.setColour(root ? ui::colours::panel.brighter(0.2f) : ui::colours::panel);
        g.fillRect(labelRect);
        g.setColour(root ? ui::colours::textPrimary : ui::colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(juce::jmin(11.0f, (float) rowHeight - 1.0f))));
        g.drawText(labelForDegree(degree), labelRect.reduced(5, 0), juce::Justification::centredLeft);
    }

    // Column lines, heavier on the beat.
    for (int step = firstStep; step <= lastStep; ++step)
    {
        int x = gutterWidth + ((step - firstStep) * usable) / visibleSteps;
        g.setColour(step % 4 == 0 ? ui::colours::flowLine : ui::colours::flowLine.withAlpha(0.4f));
        g.drawVerticalLine(x, (float) notes.getY(), (float) notes.getBottom());
    }

    // Anything past the pattern's length is shaded out, so a 12-step pattern in
    // a 16-step view reads as ending rather than as four empty steps.
    if (patternLength < lastStep)
    {
        int x = gutterWidth + ((juce::jmax(firstStep, patternLength) - firstStep) * usable) / visibleSteps;
        g.setColour(ui::colours::background.withAlpha(0.55f));
        g.fillRect(juce::Rectangle<int>(x, notes.getY(), notes.getRight() - x, notes.getHeight()));
    }

    if (playingStep >= 0)
    {
        g.setColour(ui::colours::accent.withAlpha(0.12f));
        g.fillRect(columnFor(playingStep, notes.getY(), notes.getHeight()));
    }

    // ---- Note blocks -----------------------------------------------------
    // Drawn from before the view as well, since a long note can start off the
    // left-hand edge and still cover what is on screen.
    for (int step = 0; step < StepSequencer::maxSteps; ++step)
    {
        auto stepData = sequencer.getStep(step);
        if (!stepData.active)
            continue;

        // Compared on the tick grid, since a note can start late in its step and
        // end part-way through another.
        int startTick = StepSequencer::startTickOf(step, stepData);
        int endTick = StepSequencer::endTickOf(step, stepData);

        if (endTick <= firstStep * StepSequencer::ticksPerStep
            || startTick >= lastStep * StepSequencer::ticksPerStep)
            continue;

        int row = rowForDegree(stepData.degree);
        if (row < 0 || row >= rows)
            continue;

        // A note is one block spanning exactly the time it sounds for, which is
        // what makes its length readable at a glance - including a note that
        // lasts only part of the step it starts in.
        float ticksAcross = (float) (visibleSteps * StepSequencer::ticksPerStep);
        float viewStartTick = (float) (firstStep * StepSequencer::ticksPerStep);
        auto xFor = [&](float tick)
        {
            return gutterWidth + juce::roundToInt((tick - viewStartTick) * (float) usable / ticksAcross);
        };

        int blockY = notes.getY() + row * rowHeight;
        int blockHeight = juce::jmax(5, rowHeight);
        int left = xFor((float) startTick);
        int right = juce::jmax(left + 3, xFor((float) endTick));

        auto block = juce::Rectangle<int>(left, blockY, right - left, blockHeight).reduced(1, 1);

        // Velocity shades the block, the way a piano roll shows it, so the
        // grid alone tells you where the accents are.
        auto colour = ui::colours::accent.withBrightness(0.45f + 0.55f * stepData.velocity);
        g.setColour(colour);
        g.fillRoundedRectangle(block.toFloat(), 2.0f);

        // A step that will not always sound is drawn half-hollow, so the
        // pattern shows at a glance which notes are the certain ones.
        if (stepData.probability < 1.0f)
        {
            g.setColour(ui::colours::background.withAlpha(0.55f));
            g.fillRect(block.withTrimmedLeft(block.getWidth() / 2));
        }

        // An octave shift moves the note off the row it is drawn on, so the
        // block has to say so or the grid would be lying about the pitch.
        if (stepData.octave != 0 && block.getWidth() > 22)
        {
            g.setColour(ui::colours::background.withAlpha(0.85f));
            g.setFont(juce::Font(juce::FontOptions(juce::jmin(9.0f, (float) block.getHeight()),
                                                    juce::Font::bold)));
            g.drawText((stepData.octave > 0 ? "+" : juce::String()) + juce::String(stepData.octave),
                       block.reduced(3, 0), juce::Justification::centredLeft);
        }

        g.setColour(ui::colours::background.withAlpha(0.7f));
        g.drawRoundedRectangle(block.toFloat(), 2.0f, 1.0f);

        // The grab handle for resizing, so the edge is visibly a thing to pull.
        if (block.getWidth() > 10)
        {
            g.setColour(ui::colours::background.withAlpha(0.5f));
            g.fillRect(block.removeFromRight(2));
        }
    }

    // ---- Velocity footer -------------------------------------------------
    g.setColour(ui::colours::panel.withAlpha(0.5f));
    g.fillRect(velocity);
    // Every footer row carries the same label plus an [i], because every one of
    // them now answers a question on hover and a marker only some rows have
    // reads as "that row is special" rather than "there is more to read here".
    //
    // The marker takes its space out of the label's rather than being drawn over
    // it, so the longest label - VELOCITY - shortens instead of colliding.
    auto drawRowLabel = [&](juce::Rectangle<int> row, const juce::String& text, float fontSize,
                            juce::Colour labelColour, juce::Colour markColour)
    {
        auto label = juce::Rectangle<int>(0, row.getY(), gutterWidth, row.getHeight()).reduced(5, 0);
        auto mark = label.removeFromRight(11).withSizeKeepingCentre(9, 9);

        g.setColour(labelColour);
        g.setFont(juce::Font(juce::FontOptions(fontSize)));
        g.drawText(text, label, juce::Justification::centredLeft);

        g.setColour(markColour);
        g.drawEllipse(mark.toFloat().reduced(0.5f), 1.0f);
        g.setFont(juce::Font(juce::FontOptions(7.0f)));
        g.drawText("i", mark, juce::Justification::centred);
    };

    drawRowLabel(velocity, "VELOCITY", 10.0f, ui::colours::textDim, ui::colours::textDim.withAlpha(0.45f));

    for (int step = firstStep; step < lastStep; ++step)
    {
        auto stepData = sequencer.getStep(step);
        auto column = columnFor(step, velocity.getY(), velocity.getHeight());

        g.setColour(ui::colours::background.withAlpha(0.5f));
        g.drawVerticalLine(column.getX(), (float) column.getY(), (float) column.getBottom());

        if (!stepData.active)
            continue;

        int barHeight = juce::roundToInt(stepData.velocity * (velocity.getHeight() - 6));
        auto bar = column.reduced(3, 0).withHeight(barHeight).withBottomY(velocity.getBottom() - 3);

        g.setColour(step == playingStep ? ui::colours::accentWarm : ui::colours::accentWarm.withAlpha(0.65f));
        g.fillRoundedRectangle(bar.toFloat(), 1.5f);
    }

    // ---- Probability lane ------------------------------------------------
    // Drawn as a filled curve rather than bars, so a run of steps reads as a
    // shape you can see rising or falling across the bar.
    g.setColour(ui::colours::panel.withAlpha(0.4f));
    g.fillRect(probability);
    drawRowLabel(probability, "CHANCE", 10.0f, ui::colours::textDim, ui::colours::textDim.withAlpha(0.45f));

    for (int step = firstStep; step < lastStep; ++step)
    {
        auto stepData = sequencer.getStep(step);
        auto column = columnFor(step, probability.getY(), probability.getHeight());

        g.setColour(ui::colours::background.withAlpha(0.5f));
        g.drawVerticalLine(column.getX(), (float) column.getY(), (float) column.getBottom());

        if (!stepData.active)
            continue;

        int barHeight = juce::roundToInt(stepData.probability * (probability.getHeight() - 6));
        auto bar = column.reduced(3, 0).withHeight(juce::jmax(2, barHeight))
                       .withBottomY(probability.getBottom() - 3);

        // A step that always sounds is solid; anything less is drawn lighter,
        // which is the same thing the half-hollow note block says above.
        bool certain = stepData.probability >= 0.999f;
        g.setColour(certain ? ui::colours::accentMod.withAlpha(0.75f)
                            : ui::colours::accentMod.withAlpha(0.45f));
        g.fillRoundedRectangle(bar.toFloat(), 1.5f);

        if (!certain && column.getWidth() > 26)
        {
            g.setColour(ui::colours::textDim);
            g.setFont(juce::Font(juce::FontOptions(8.0f)));
            g.drawText(juce::String(juce::roundToInt(stepData.probability * 100.0f)),
                       column.withHeight(10).withY(probability.getY() + 1),
                       juce::Justification::centred);
        }
    }

    // ---- Octave row ------------------------------------------------------
    g.setColour(ui::colours::panel);
    g.fillRect(octaves);
    drawRowLabel(octaves, "OCTAVE", 9.0f, ui::colours::textDim, ui::colours::textDim.withAlpha(0.45f));

    for (int step = firstStep; step < lastStep; ++step)
    {
        auto stepData = sequencer.getStep(step);
        auto cell = columnFor(step, octaves.getY(), octaves.getHeight()).reduced(2, 2);

        if (!stepData.active)
        {
            g.setColour(ui::colours::knobTrack.withAlpha(0.4f));
            g.fillRoundedRectangle(cell.toFloat(), 2.0f);
            continue;
        }

        // At the middle octave the cell is a quiet dash; shifted, it says which
        // way and by how much, so only the steps doing something draw the eye.
        bool shifted = stepData.octave != 0;
        g.setColour(shifted ? ui::colours::accent.withAlpha(0.30f) : ui::colours::knobTrack);
        g.fillRoundedRectangle(cell.toFloat(), 2.0f);

        g.setColour(shifted ? ui::colours::accent : ui::colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(9.0f, shifted ? juce::Font::bold : juce::Font::plain)));
        g.drawText(shifted ? (stepData.octave > 0 ? "+" + juce::String(stepData.octave)
                                                  : juce::String(stepData.octave))
                           : juce::String("-"),
                   cell, juce::Justification::centred);
    }

    // ---- Accent row ------------------------------------------------------
    // A filled cell here, unlike the slide row below, and for the same reason
    // that one is drawn as a span: an accent IS a property of the step. It says
    // this note hits harder, and nothing about the step after it.
    //
    // How much harder is the Accent dial on the synth page, one setting for the
    // whole pattern - so the row says which notes, not by how much.
    g.setColour(ui::colours::panel);
    g.fillRect(accents);
    drawRowLabel(accents, "ACCENT", 9.0f, ui::colours::textDim, ui::colours::textDim.withAlpha(0.45f));

    for (int step = firstStep; step < lastStep; ++step)
    {
        auto stepData = sequencer.getStep(step);
        auto cell = columnFor(step, accents.getY(), accents.getHeight()).reduced(4, 3);

        if (!stepData.active)
        {
            g.setColour(ui::colours::knobTrack.withAlpha(0.4f));
            g.fillRoundedRectangle(cell.toFloat(), 2.0f);
            continue;
        }

        g.setColour(stepData.accent ? ui::colours::accentWarm : ui::colours::knobTrack);
        g.fillRoundedRectangle(cell.toFloat(), 2.0f);
    }

    // ---- Slide row -------------------------------------------------------
    // Drawn differently from the switches around it on purpose. A tie belongs
    // to the GAP between two steps, not to either of them, so a filled cell
    // would say the wrong thing - it would read as a property of the step you
    // clicked. The mark runs from this step across to the next one instead,
    // which is what it actually does.
    //
    // A tie needs mono and legato to mean anything; without them the sequencer
    // plays the step straight. Rather than hide that or quietly switch them on,
    // a tie that cannot sound draws hollow, so it looks exactly like what it
    // is: set, and not doing anything.
    //
    // Hollow rather than a warning marker in the label, because there is
    // nothing to warn about until a tie exists - and the moment one does, the
    // hollow mark is right where the user is already looking.
    const bool tiesSound = params.monoMode.load() > 0.5f && params.legatoMode.load() > 0.5f;

    g.setColour(ui::colours::panel);
    g.fillRect(slides);
    // Slide is the one row whose marker changes: it goes warm when mono or
    // legato are off, so the row asks to be hovered at exactly the moment the
    // answer is worth having.
    drawRowLabel(slides, "SLIDE", 9.0f,
                 tiesSound ? ui::colours::textDim : ui::colours::textDim.withAlpha(0.5f),
                 tiesSound ? ui::colours::textDim.withAlpha(0.45f) : ui::colours::accentWarm);

    for (int step = firstStep; step < lastStep; ++step)
    {
        auto stepData = sequencer.getStep(step);
        auto cell = columnFor(step, slides.getY(), slides.getHeight());

        if (!stepData.slide)
        {
            // A quiet dash across the middle, the way the octave row marks a
            // step sitting at its default - present, but not asking for
            // attention.
            g.setColour(ui::colours::knobTrack.withAlpha(stepData.active ? 0.9f : 0.4f));
            g.fillRect(cell.getCentreX() - 3, cell.getCentreY(), 6, 1);
            continue;
        }

        // From the middle of this step to the middle of the next, so the mark
        // spans the join it describes. The last step of the pattern ties round
        // to the first, and its mark simply runs off the right-hand edge -
        // which is honest: that is where the line goes next.
        auto bar = juce::Rectangle<int>(cell.getCentreX(), cell.getCentreY() - 2,
                                        juce::jmax(6, cell.getWidth()), 4);

        if (tiesSound)
        {
            g.setColour(ui::colours::accent);
            g.fillRoundedRectangle(bar.toFloat(), 2.0f);
        }
        else
        {
            g.setColour(ui::colours::accent.withAlpha(0.55f));
            g.drawRoundedRectangle(bar.toFloat().reduced(0.5f), 2.0f, 1.0f);
        }
    }

    // ---- Lock row --------------------------------------------------------
    g.setColour(ui::colours::panel);
    g.fillRect(locks);
    drawRowLabel(locks, "LOCK", 9.0f, ui::colours::textDim, ui::colours::textDim.withAlpha(0.45f));

    for (int step = firstStep; step < lastStep; ++step)
    {
        auto stepData = sequencer.getStep(step);
        auto cell = columnFor(step, locks.getY(), locks.getHeight()).reduced(4, 3);

        g.setColour(stepData.locked ? ui::colours::accentWarm : ui::colours::knobTrack);
        g.fillRoundedRectangle(cell.toFloat(), 2.0f);
    }
}

// ============================================================================
// AutomationLaneStrip
// ============================================================================

AutomationLaneStrip::AutomationLaneStrip(WaveLatheProcessor& processor, TimeView& view)
    : processorRef(processor), timeView(view)
{
    expandedByParameter.assign((size_t) paramreg::maxParameters, true);
}

int AutomationLaneStrip::refreshLanes()
{
    lanes.clear();

    int y = 0;

    // Through the same function the Clear button asks, so the lanes drawn here
    // are exactly the lanes Clear would remove.
    for (const auto id : shownLanes(processorRef.getSequencer(), view))
    {
        LaneRow row;
        row.parameterId = id;
        row.expanded = expandedByParameter[(size_t) id];
        row.y = y;
        row.height = headerHeight + (row.expanded ? curveHeight : 0);

        y += row.height;
        lanes.push_back(row);
    }

    setSize(getWidth(), juce::jmax(1, y));
    return y;
}

int AutomationLaneStrip::laneAt(juce::Point<int> position) const
{
    for (int i = 0; i < (int) lanes.size(); ++i)
        if (position.y >= lanes[(size_t) i].y && position.y < lanes[(size_t) i].y + lanes[(size_t) i].height)
            return i;

    return -1;
}

void AutomationLaneStrip::mouseDown(const juce::MouseEvent& event)
{
    // Same as the grid: one entry for the whole gesture, taken before it.
    processorRef.recordUndoPoint("Draw Automation");

    draggingLane = -1;

    int index = laneAt(event.getPosition());
    if (index < 0)
        return;

    const auto& lane = lanes[(size_t) index];
    int localY = event.y - lane.y;

    if (localY < headerHeight)
    {
        // The clear button lives at the right-hand end of the header.
        if (event.x > getWidth() - 26)
        {
            processorRef.getSequencer().clearLane(lane.parameterId);
            if (onLanesChanged)
                onLanesChanged();
            return;
        }

        // Anywhere else on the header collapses or expands it, so the lanes you
        // are not working on can be folded away.
        expandedByParameter[(size_t) lane.parameterId] = !lane.expanded;
        if (onLanesChanged)
            onLanesChanged();
        return;
    }

    if (lane.expanded)
    {
        draggingLane = index;
        lastDragTick = -1; // a fresh stroke starts where it was clicked
        writeCurve(event);
    }
}

void AutomationLaneStrip::mouseUp(const juce::MouseEvent&)
{
    draggingLane = -1;
    lastDragTick = -1;
}

void AutomationLaneStrip::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingLane >= 0)
        writeCurve(event);
}

void AutomationLaneStrip::writeCurve(const juce::MouseEvent& event)
{
    if (draggingLane < 0 || draggingLane >= (int) lanes.size())
        return;

    const auto& lane = lanes[(size_t) draggingLane];

    int usable = getWidth() - StepGrid::gutterWidth;
    if (usable <= 0)
        return;

    // Ticks are read through the same window the grid uses, so what you draw
    // lands under the step you drew it beneath.
    int visibleTicks = juce::jmax(1, timeView.visibleSteps * StepSequencer::ticksPerStep);
    int firstTick = timeView.firstStep * StepSequencer::ticksPerStep;

    int tick = firstTick + ((event.x - StepGrid::gutterWidth) * visibleTicks) / usable;
    tick = juce::jlimit(0, StepSequencer::maxTicks - 1, tick);

    int curveTop = lane.y + headerHeight;
    float fromTop = (float) (event.y - curveTop) / (float) curveHeight;
    float value = juce::jlimit(0.0f, 1.0f, 1.0f - fromTop);

    auto& sequencer = processorRef.getSequencer();

    // Fill in every tick between the last point and this one, so how fast you
    // moved the mouse does not decide how much of the lane gets drawn.
    if (lastDragTick >= 0 && lastDragTick != tick)
    {
        int from = lastDragTick;
        int direction = tick > from ? 1 : -1;
        int span = std::abs(tick - from);

        for (int i = 1; i < span; ++i)
        {
            int between = from + i * direction;
            float blend = (float) i / (float) span;
            sequencer.setLaneValue(lane.parameterId, between,
                                   lastDragValue + (value - lastDragValue) * blend);
        }
    }

    sequencer.setLaneValue(lane.parameterId, tick, value);

    lastDragTick = tick;
    lastDragValue = value;
    repaint();
}

void AutomationLaneStrip::paint(juce::Graphics& g)
{
    g.fillAll(ui::colours::background);

    auto& sequencer = processorRef.getSequencer();
    int usable = getWidth() - StepGrid::gutterWidth;
    if (usable <= 0)
        return;

    int playingTick = sequencer.isPlaying() ? sequencer.getCurrentTick() : -1;

    for (const auto& lane : lanes)
    {
        auto header = juce::Rectangle<int>(0, lane.y, getWidth(), headerHeight);

        g.setColour(ui::colours::panel);
        g.fillRect(header);

        g.setColour(ui::colours::accentWarm);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(juce::String(lane.expanded ? "-  " : "+  ") + paramreg::name(lane.parameterId),
                   header.reduced(8, 0), juce::Justification::centredLeft);

        g.setColour(ui::colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("clear", header.removeFromRight(26).translated(-4, 0), juce::Justification::centredLeft);

        if (!lane.expanded)
            continue;

        auto curve = juce::Rectangle<int>(StepGrid::gutterWidth, lane.y + headerHeight,
                                           usable, curveHeight);

        g.setColour(ui::colours::background.darker(0.3f));
        g.fillRect(curve);

        // Same column grid as the notes above, so a value can be read against
        // the step it happens on.
        int firstStep = timeView.firstStep;
        int visibleSteps = juce::jmax(1, timeView.visibleSteps);
        int lastStep = juce::jmin(StepSequencer::maxSteps, firstStep + visibleSteps);

        for (int step = firstStep; step <= lastStep; ++step)
        {
            int x = StepGrid::gutterWidth + ((step - firstStep) * usable) / visibleSteps;
            g.setColour(step % 4 == 0 ? ui::colours::flowLine : ui::colours::flowLine.withAlpha(0.35f));
            g.drawVerticalLine(x, (float) curve.getY(), (float) curve.getBottom());
        }

        // The curve is drawn as filled columns rather than a line, because the
        // lane is stepped: this is what the value actually does.
        int firstTick = firstStep * StepSequencer::ticksPerStep;
        int visibleTicks = visibleSteps * StepSequencer::ticksPerStep;

        juce::Path filled;
        filled.startNewSubPath((float) curve.getX(), (float) curve.getBottom());

        for (int i = 0; i < visibleTicks; ++i)
        {
            int tick = firstTick + i;
            if (tick >= StepSequencer::maxTicks)
                break;

            float value = sequencer.getLaneValue(lane.parameterId, tick);
            float x0 = (float) (curve.getX() + (i * usable) / visibleTicks);
            float x1 = (float) (curve.getX() + ((i + 1) * usable) / visibleTicks);
            float y = (float) curve.getBottom() - value * (float) curve.getHeight();

            filled.lineTo(x0, y);
            filled.lineTo(x1, y);
        }

        filled.lineTo((float) curve.getRight(), (float) curve.getBottom());
        filled.closeSubPath();

        g.setColour(ui::colours::accentWarm.withAlpha(0.28f));
        g.fillPath(filled);
        g.setColour(ui::colours::accentWarm);
        g.strokePath(filled, juce::PathStrokeType(1.4f));

        if (playingTick >= firstTick && playingTick < firstTick + visibleTicks)
        {
            int x = StepGrid::gutterWidth + ((playingTick - firstTick) * usable) / visibleTicks;
            g.setColour(ui::colours::accent.withAlpha(0.8f));
            g.drawVerticalLine(x, (float) curve.getY(), (float) curve.getBottom());
        }
    }
}

// ============================================================================
// DrumGrid
// ============================================================================

DrumGrid::DrumGrid(WaveLatheProcessor& processor, TimeView& view)
    : processorRef(processor), timeView(view)
{
}

int DrumGrid::cellForX(int x) const
{
    // The same span of time as StepGrid, divided more finely. Both grids share
    // a TimeView so a step boundary sits at the same pixel on each; this one
    // just has drumSubdivisions columns inside every one of those steps.
    const int usable = getWidth() - StepGrid::gutterWidth;
    if (usable <= 0 || timeView.visibleSteps <= 0 || x < StepGrid::gutterWidth)
        return -1;

    const int visibleCells = timeView.visibleSteps * StepSequencer::drumSubdivisions;
    const int firstCell = timeView.firstStep * StepSequencer::drumSubdivisions;

    const int cell = firstCell + ((x - StepGrid::gutterWidth) * visibleCells) / usable;

    if (cell < 0 || cell >= StepSequencer::numDrumCells)
        return -1;

    return cell;
}

juce::Rectangle<int> DrumGrid::cellArea(int voice, int cell) const
{
    const auto row = rowArea(voice);
    if (row.getHeight() <= 0)
        return {};

    const int usable = getWidth() - StepGrid::gutterWidth;
    const int visibleCells = juce::jmax(1, timeView.visibleSteps * StepSequencer::drumSubdivisions);
    const int firstCell = timeView.firstStep * StepSequencer::drumSubdivisions;

    const int x = StepGrid::gutterWidth + ((cell - firstCell) * usable) / visibleCells;
    const int next = StepGrid::gutterWidth + ((cell + 1 - firstCell) * usable) / visibleCells;

    return juce::Rectangle<int>(x, row.getY(), next - x, row.getHeight());
}

juce::Rectangle<int> DrumGrid::rowArea(int voice) const
{
    const int rows = StepSequencer::numDrumVoices;
    if (voice < 0 || voice >= rows)
        return {};

    // Measured from the edges rather than by multiplying a rounded height, so
    // twelve rows fill the component exactly and there is no leftover strip at
    // the bottom belonging to no voice.
    const int top = (getHeight() * voice) / rows;
    const int bottom = (getHeight() * (voice + 1)) / rows;

    return juce::Rectangle<int>(0, top, getWidth(), bottom - top);
}

int DrumGrid::voiceForY(int y) const
{
    const int rows = StepSequencer::numDrumVoices;
    if (getHeight() <= 0 || y < 0)
        return -1;

    int voice = (y * rows) / getHeight();
    return (voice >= 0 && voice < rows) ? voice : -1;
}

void DrumGrid::paint(juce::Graphics& g)
{
    const auto& params = processorRef.getParameters();
    auto& sequencer = processorRef.getSequencer();

    g.fillAll(ui::colours::background);

    const int usable = getWidth() - StepGrid::gutterWidth;
    if (usable <= 0)
        return;

    constexpr int sub = StepSequencer::drumSubdivisions;

    const int firstStep = timeView.firstStep;
    const int visibleSteps = juce::jmax(1, timeView.visibleSteps);

    const int firstCell = firstStep * sub;
    const int endCell = juce::jmin(StepSequencer::numDrumCells, (firstStep + visibleSteps) * sub);

    const int patternLength = juce::jlimit(1, StepSequencer::maxSteps,
                                           (int) std::round(params.seqLength.load()));
    const int playingStep = sequencer.isPlaying() ? sequencer.getCurrentStep() : -1;

    // How wide one cell is, as a float, for drawing a nudged hit off its own
    // position. Integer cell edges keep the columns crisp; the nudge is a
    // fraction of a cell and has to be finer than that.
    const float cellWidth = (float) usable / (float) (visibleSteps * sub);
    const float pixelsPerTick = cellWidth / (float) StepSequencer::ticksPerDrumCell;

    for (int voice = 0; voice < StepSequencer::numDrumVoices; ++voice)
    {
        auto row = rowArea(voice);
        if (row.getHeight() <= 0)
            continue;

        const bool used = sequencer.isDrumVoiceUsed(voice);

        // ---- The name gutter ---------------------------------------------
        auto label = juce::Rectangle<int>(0, row.getY(), StepGrid::gutterWidth, row.getHeight());

        g.setColour(voice % 2 == 0 ? ui::colours::panel : ui::colours::background);
        g.fillRect(label);

        // A row with nothing on it is dimmed rather than hidden. Sixteen rows
        // are a lot to read and the ones in use should catch the eye - but a
        // row you cannot see is a row you cannot draw on.
        g.setColour(used ? ui::colours::textPrimary : ui::colours::textDim);
        g.setFont(juce::Font(juce::FontOptions((float) juce::jlimit(9, 12, row.getHeight() - 5))));
        g.drawText(StepSequencer::drumVoiceName(voice), label.reduced(6, 0),
                   juce::Justification::centredLeft, false);

        // ---- The cells ----------------------------------------------------
        for (int cell = firstCell; cell < endCell; ++cell)
        {
            auto area = cellArea(voice, cell).reduced(1);
            if (area.isEmpty())
                continue;

            const auto data = sequencer.getDrumCell(voice, cell);

            // Past the end of the pattern is drawn as out of play rather than
            // hidden, so shortening the bar shows what you have parked instead
            // of appearing to delete it.
            const bool inPlay = (cell / sub) < patternLength;

            // Three weights of column, so the bar has a readable pulse even
            // before anything is written on it: the four, the step, and the
            // subdivision between steps.
            const bool onTheBeat = (cell % (4 * sub)) == 0;
            const bool onTheStep = (cell % sub) == 0;

            if (!data.active)
            {
                g.setColour(onTheBeat  ? ui::colours::panelEdge
                            : onTheStep ? ui::colours::panel
                                        : ui::colours::panel.darker(0.35f));
                g.fillRect(area);

                if (!inPlay)
                {
                    g.setColour(ui::colours::background.withAlpha(0.55f));
                    g.fillRect(area);
                }

                continue;
            }

            // Velocity reads as brightness, which is how the note grid already
            // shows it, so a cell means the same thing on both pages.
            auto colour = data.accent ? ui::colours::accentWarm : ui::colours::accent;
            colour = colour.withMultipliedBrightness(
                0.55f + 0.45f * juce::jlimit(0.0f, 1.0f, data.velocity));

            if (!inPlay)
                colour = colour.withAlpha(0.35f);

            // Drawn where it will actually FIRE, not where its cell is. A hit
            // pushed four ticks early sits visibly left of its column, which is
            // the only way to see feel rather than have to remember it.
            const float shift = (float) data.nudge * pixelsPerTick;
            auto hit = area.toFloat().translated(shift, 0.0f);

            // Clipped to the columns. A hit pushed early on the leftmost
            // visible cell otherwise draws out over the name gutter and the
            // divider line lands across it, which reads as two blocks rather
            // than one hit sitting early. Only visible by looking at it.
            hit = hit.getIntersection(juce::Rectangle<float>(
                (float) StepGrid::gutterWidth, (float) area.getY(),
                (float) usable, (float) area.getHeight()));

            if (hit.isEmpty())
                continue;

            g.setColour(colour);
            g.fillRect(hit);

            // A step that may not sound is drawn hollow, so "sometimes" looks
            // different from "quiet" - two things one brightness could not tell
            // apart.
            if (data.probability < 0.999f)
            {
                g.setColour(ui::colours::background);
                g.fillRect(hit.reduced(juce::jmax(1.0f, hit.getHeight() / 5.0f)));
            }

            // A tick where the cell itself is, whenever the hit has been moved
            // off it. Without it a nudged row is just a wobbly line of blocks
            // with nothing to read the wobble against.
            if (data.nudge != 0)
            {
                g.setColour(ui::colours::textDim.withAlpha(0.65f));
                g.fillRect((float) area.getX(), (float) area.getBottom() - 2.0f,
                           juce::jmax(1.0f, cellWidth * 0.5f), 2.0f);
            }
        }

        g.setColour(ui::colours::panelEdge);
        g.drawHorizontalLine(row.getBottom() - 1, 0.0f, (float) getWidth());
    }

    // ---- The step the transport is on ------------------------------------
    if (playingStep >= firstStep && playingStep < firstStep + visibleSteps)
    {
        const int x = StepGrid::gutterWidth + ((playingStep - firstStep) * usable) / visibleSteps;
        const int next = StepGrid::gutterWidth + ((playingStep + 1 - firstStep) * usable) / visibleSteps;

        g.setColour(ui::colours::textPrimary.withAlpha(0.14f));
        g.fillRect(juce::Rectangle<int>(x, 0, next - x, getHeight()));
    }

    // ---- Where the pattern ends ------------------------------------------
    if (patternLength > firstStep && patternLength < firstStep + visibleSteps)
    {
        int x = StepGrid::gutterWidth + ((patternLength - firstStep) * usable) / visibleSteps;
        g.setColour(ui::colours::accentWarm.withAlpha(0.7f));
        g.drawVerticalLine(x, 0.0f, (float) getHeight());
    }

    g.setColour(ui::colours::panelEdge);
    g.drawVerticalLine(StepGrid::gutterWidth, 0.0f, (float) getHeight());
}
juce::String DrumGrid::getTooltip()
{
    if (hoverVoice < 0)
        return {};

    return juce::String(StepSequencer::drumVoiceName(hoverVoice))
           + ": click to place a hit, drag to draw a run. Right-click a hit to accent it, "
             "shift-drag one to push it off the beat. Click the name to hear the voice.";
}

void DrumGrid::applyAt(juce::Point<int> position, bool firstClick)
{
    const int voice = voiceForY(position.y);
    const int cell = cellForX(position.x);

    if (voice < 0 || cell < 0)
        return;

    // A drag that has not left the cell it started in changes nothing, so
    // holding the mouse still does not flicker a cell on and off.
    if (!firstClick && voice == lastVoice && cell == lastCell)
        return;

    auto& sequencer = processorRef.getSequencer();

    // The first cell decides the whole stroke: starting on an empty cell draws,
    // starting on a filled one erases. Toggling each cell instead would mean a
    // swipe meant to clear four steps left them exactly as they were.
    if (firstClick)
        paintingOn = !sequencer.getDrumCell(voice, cell).active;

    sequencer.setDrumCellActive(voice, cell, paintingOn);

    lastVoice = voice;
    lastCell = cell;

    if (onPatternChanged)
        onPatternChanged();

    repaint();
}

void DrumGrid::nudgeAt(juce::Point<int> position, bool firstClick)
{
    auto& sequencer = processorRef.getSequencer();

    if (firstClick)
    {
        const int voice = voiceForY(position.y);
        const int cell = cellForX(position.x);

        // Only a hit that exists can be pushed off its beat. Nothing to grab
        // means nothing happens, rather than a hit appearing under a gesture
        // that was not asking for one.
        if (voice < 0 || cell < 0 || !sequencer.getDrumCell(voice, cell).active)
        {
            nudging = false;
            return;
        }

        nudging = true;
        nudgeVoice = voice;
        nudgeCell = cell;
        nudgeStartX = position.x;
        nudgeStartValue = sequencer.getDrumCell(voice, cell).nudge;
        return;
    }

    if (!nudging)
        return;

    const int usable = getWidth() - StepGrid::gutterWidth;
    if (usable <= 0)
        return;

    // Pixels back into ticks, against the same scale the hit is drawn at, so
    // the block follows the pointer rather than lagging or racing it.
    const float cellWidth = (float) usable
                            / (float) juce::jmax(1, timeView.visibleSteps
                                                        * StepSequencer::drumSubdivisions);
    const float pixelsPerTick = juce::jmax(0.5f, cellWidth / (float) StepSequencer::ticksPerDrumCell);

    const int moved = juce::roundToInt((float) (position.x - nudgeStartX) / pixelsPerTick);

    sequencer.setDrumCellNudge(nudgeVoice, nudgeCell, nudgeStartValue + moved);

    if (onPatternChanged)
        onPatternChanged();

    repaint();
}

void DrumGrid::mouseDown(const juce::MouseEvent& event)
{
    hoverVoice = voiceForY(event.y);

    // Clicking a row's NAME plays that voice, the way every drum machine lets
    // you hear one without running the pattern. The gutter was dead space -
    // cellForX already returns -1 left of it - and until phase 5 gives the kit
    // a mixer this is the only way to hear a voice on its own.
    if (event.x < StepGrid::gutterWidth && !event.mods.isPopupMenu())
    {
        const int voice = voiceForY(event.y);

        if (voice >= 0)
            processorRef.auditionDrumVoice(voice);

        return;
    }

    // Right-click accents a hit that is already there rather than placing one.
    // Accent belongs to an existing cell, and keeping it on a separate click
    // leaves the common action - drawing - a single unmodified stroke.
    if (event.mods.isPopupMenu())
    {
        const int voice = voiceForY(event.y);
        const int cell = cellForX(event.x);

        if (voice >= 0 && cell >= 0)
        {
            auto& sequencer = processorRef.getSequencer();
            auto data = sequencer.getDrumCell(voice, cell);

            if (data.active)
            {
                data.accent = !data.accent;
                sequencer.setDrumCell(voice, cell, data);

                if (onPatternChanged)
                    onPatternChanged();

                repaint();
            }
        }

        return;
    }

    if (event.mods.isShiftDown())
    {
        nudgeAt(event.getPosition(), true);
        return;
    }

    nudging = false;
    applyAt(event.getPosition(), true);
}

void DrumGrid::mouseDrag(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        return;

    // Which gesture this is was decided on mouseDown. Letting go of shift
    // halfway through a nudge must not turn it into a drawing stroke that
    // paints over the row the hit was being moved along.
    if (nudging)
    {
        nudgeAt(event.getPosition(), false);
        return;
    }

    applyAt(event.getPosition(), false);
}

void DrumGrid::mouseUp(const juce::MouseEvent&)
{
    lastVoice = -1;
    lastCell = -1;
    nudging = false;
    nudgeVoice = -1;
    nudgeCell = -1;
}

void DrumGrid::mouseWheelMove(const juce::MouseEvent& event,
                              const juce::MouseWheelDetails& wheel)
{
    hoverVoice = voiceForY(event.y);

    if (onViewShouldChange == nullptr)
        return;

    // Same gesture as the note grid: the wheel scrolls time, ctrl zooms it.
    const int delta = wheel.deltaY > 0.0f ? -1 : (wheel.deltaY < 0.0f ? 1 : 0);
    if (delta != 0)
        onViewShouldChange(delta, event.mods.isCtrlDown() || event.mods.isCommandDown());
}

// ============================================================================
// SequencerPanel
// ============================================================================

SequencerPanel::SequencerPanel(WaveLatheProcessor& processor, ui::LcdDisplay& lcdToUse)
    : processorRef(processor), lcd(lcdToUse), grid(processor, timeView),
      drumGrid(processor, timeView), automationStrip(processor, timeView)
{
    grid.onViewShouldChange = [this](int delta, bool zoom) { changeView(delta, zoom); };
    drumGrid.onViewShouldChange = [this](int delta, bool zoom) { changeView(delta, zoom); };

    timeScrollBar.setRangeLimits(0.0, (double) StepSequencer::maxSteps);
    timeScrollBar.addListener(this);
    addAndMakeVisible(timeScrollBar);

    pitchScrollBar.addListener(this);
    addAndMakeVisible(pitchScrollBar);
    grid.onPitchViewChanged = [this] { syncPitchScrollBar(); automationStrip.repaint(); };

    addAndMakeVisible(transportSection);
    transportSection.setStyle(ui::SectionStyle::modulator);
    addAndMakeVisible(gridSection);
    gridSection.setStyle(ui::SectionStyle::standalone);
    addAndMakeVisible(automationSection);
    automationSection.setStyle(ui::SectionStyle::standalone);

    addAndMakeVisible(grid);
    grid.onPatternChanged = [this] { repaint(); };

    // Added but not visible: the switch below decides which of the two is
    // showing, and it starts on notes.
    addChildComponent(drumGrid);
    drumGrid.onPatternChanged = [this] { repaint(); };

    automationViewport.setViewedComponent(&automationStrip, false);
    automationViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(automationViewport);

    automationStrip.onLanesChanged = [this] { layoutAutomation(); };

    auto& params = processorRef.getParameters();

    auto styleButton = [this](juce::TextButton& b, std::function<void()> action)
    {
        b.setColour(juce::TextButton::buttonColourId, ui::colours::panel);
        b.onClick = std::move(action);
        addAndMakeVisible(b);
    };

    styleButton(playButton, [this]
    {
        auto& p = processorRef.getParameters();
        bool playing = p.seqPlaying.load() <= 0.5f;
        p.seqPlaying = playing ? 1.0f : 0.0f;

        // Stopping also disarms: leaving REC armed is how you overwrite a lane
        // by accident the next time you press play.
        if (!playing)
            p.seqRecord = 0.0f;

        updateTransportButtons();
        lcd.print(playing ? "Sequencer running." : "Sequencer stopped.");
    });

    styleButton(recordButton, [this]
    {
        auto& p = processorRef.getParameters();
        bool arming = p.seqRecord.load() <= 0.5f;
        p.seqRecord = arming ? 1.0f : 0.0f;

        if (arming && p.seqPlaying.load() <= 0.5f)
            p.seqPlaying = 1.0f; // arming with the transport stopped is never what you meant

        updateTransportButtons();
        lcd.print(arming ? "REC armed - move a dial to write it onto the timeline."
                         : "REC off.");
    });

    styleButton(generateButton, [this]
    {
        processorRef.recordUndoPoint("Generate");

        int patternSteps = juce::jlimit(1, StepSequencer::maxSteps,
                                         (int) std::round(processorRef.getParameters().seqLength.load()));
        int pulses = densityKnob != nullptr
                         ? juce::jlimit(1, patternSteps, (int) std::round(densityKnob->getRealValue()))
                         : patternSteps / 2;
        int range = rangeKnob != nullptr ? juce::jlimit(1, 14, (int) std::round(rangeKnob->getRealValue())) : 7;

        processorRef.getSequencer().generate(pulses, range, true, patternSteps);
        refreshFromProcessor();

        lcd.print("Generated a line: " + juce::String(pulses) + " notes over "
                  + juce::String(patternSteps) + " steps.");
        lcd.print("Lock the steps you like, then generate again to keep them.");
    });

    // Clear acts on the pattern you are looking at. Clearing the notes from the
    // drum page, or the beat from the note page, would be the button doing
    // something out of sight - which is the one thing a Clear button must never
    // do. The label follows the view for the same reason.
    styleButton(clearButton, [this]
    {
        if (editingDrums)
        {
            processorRef.recordUndoPoint("Clear Drums");
            processorRef.getSequencer().clearDrumPattern();
            refreshFromProcessor();
            lcd.print("Drum pattern cleared.");
        }
        else
        {
            processorRef.recordUndoPoint("Clear Steps");
            processorRef.getSequencer().clearPattern();
            refreshFromProcessor();
            lcd.print("Steps cleared.");
        }
    });

    styleButton(notesViewButton, [this] { setEditingDrums(false); });
    styleButton(drumsViewButton, [this] { setEditingDrums(true); });

    styleButton(zoomInButton, [this] { changeView(1, true); });
    styleButton(zoomOutButton, [this] { changeView(-1, true); });

    // Whether REC also captures what you play, and whether captured notes are
    // pulled onto the grid or left where your fingers put them.
    recordNotesButton.setToggleState(params.seqRecordNotes.load() > 0.5f, juce::dontSendNotification);
    recordNotesButton.onClick = [this]
    {
        bool on = recordNotesButton.getToggleState();
        processorRef.getParameters().seqRecordNotes = on ? 1.0f : 0.0f;
        lcd.print(on ? "REC will capture notes you play onto the grid."
                     : "REC captures dial moves only.");
    };
    addAndMakeVisible(recordNotesButton);

    quantiseButton.setToggleState(params.seqQuantise.load() > 0.5f, juce::dontSendNotification);
    quantiseButton.onClick = [this]
    {
        bool on = quantiseButton.getToggleState();
        processorRef.getParameters().seqQuantise = on ? 1.0f : 0.0f;
        lcd.print(on ? "Quantise on - captured notes snap to the nearest step."
                     : "Quantise off - captured notes keep the timing you played.");
    };
    addAndMakeVisible(quantiseButton);

    styleButton(addLaneButton, [this] { showAddLaneMenu(); });

    styleButton(clearAutomationButton, [this]
    {
        auto& sequencer = processorRef.getSequencer();
        const auto view = currentLaneView();

        if (view == LaneView::all)
        {
            processorRef.recordUndoPoint("Clear Automation");
            sequencer.clearAllLanes();
            layoutAutomation();
            lcd.print("Automation cleared - the dials stay where they are.");
            return;
        }

        // Only what the strip is showing, asked of the same function the strip
        // drew from. The lanes the filter is hiding are not touched, and the
        // display says how many survived so nobody has to wonder.
        const auto shown = shownLanes(sequencer, view);

        if (shown.empty())
            return;

        const juce::String kind = view == LaneView::drums ? "Drum" : "Synth";
        const juce::String other = view == LaneView::drums ? "synth" : "drum";

        processorRef.recordUndoPoint("Clear " + kind + " Automation");

        for (const auto id : shown)
            sequencer.clearLane(id);

        layoutAutomation();

        const auto kept = hiddenLaneCount(sequencer, view);

        lcd.print(kind + " automation cleared"
                  + (kept > 0 ? " - " + juce::String(kept) + " " + other
                                    + (kept == 1 ? " lane" : " lanes") + " kept."
                              : juce::String(" - the dials stay where they are.")));
    });

    styleButton(showAllLanesButton, [this]
    {
        showAllLanes = !showAllLanes;
        layoutAutomation();

        const auto what = editingDrums ? "drum" : "synth";

        lcd.print(showAllLanes ? juce::String("Showing every lane, whichever grid is up.")
                               : "Showing the " + juce::String(what)
                                     + " lanes only - the button counts any that are hidden.");
    });

    auto makeKnob = [this](std::unique_ptr<ui::ParameterKnob>& target, const juce::String& name,
                            ui::IconType icon, juce::NormalisableRange<double> range, double initial,
                            std::function<juce::String(double)> formatter, std::function<void(double)> apply)
    {
        target = std::make_unique<ui::ParameterKnob>(name, icon, range, initial, std::move(formatter));
        target->onValueChanged = [this, apply](double realValue, juce::String text)
        {
            apply(realValue);
            lcd.printTransient(text);
            grid.repaint();
        };

        target->onGestureStart = [this, name] { processorRef.recordUndoPoint(name); };

        addAndMakeVisible(*target);
    };

    // Fine up to a bar's worth of steps, then whole bars. Past sixteen steps a
    // pattern of, say, thirty-seven is not something anyone means - it is what
    // you get when a dial with sixty-four positions lands between two useful
    // ones. Snapping to fours keeps the long lengths musical.
    makeKnob(lengthKnob, "Length", ui::IconType::sustain, {1.0, 64.0}, params.seqLength.load(),
             [](double v) { return juce::String(snapPatternLength(v)); },
             [&params](double v) { params.seqLength = (float) snapPatternLength(v); });

    // This is the clock the whole project runs on, so it says what the
    // arpeggiator is being pulled to as well as what the steps are.
    makeKnob(rateKnob, "Rate", ui::IconType::lfoRate, {0.0, (double) tempo::getNumDivisions() - 1},
             params.seqDivision.load(),
             [&params](double v)
             {
                 auto text = tempo::divisionName((int) std::round(v));
                 int link = juce::jlimit(0, tempo::getNumRateLinks() - 1,
                                          (int) std::round(params.arpRateLink.load()));

                 if (params.arpEnabled.load() > 0.5f && params.arpSync.load() > 0.5f
                     && !tempo::isFreeRateLink(link))
                 {
                     double beats = tempo::linkedBeats(tempo::divisionBeats((int) std::round(v)), link);
                     text += "  (arp " + tempo::divisionName(tempo::nearestDivisionForBeats(beats)) + ")";
                 }

                 return text;
             },
             [&params](double v) { params.seqDivision = (float) std::round(v); });

    makeKnob(rootKnob, "Root", ui::IconType::pitch, {24.0, 84.0}, params.seqRootNote.load(),
             [](double v) { return juce::MidiMessage::getMidiNoteName((int) std::round(v), true, true, 3); },
             [&params](double v) { params.seqRootNote = (float) std::round(v); });

    makeKnob(densityKnob, "Density", ui::IconType::voices, {1.0, 64.0}, 8.0,
             [](double v) { return juce::String((int) std::round(v)); },
             [](double) {});

    makeKnob(rangeKnob, "Range", ui::IconType::detune, {1.0, 14.0}, 7.0,
             [](double v) { return juce::String((int) std::round(v)); },
             [](double) {});

    updateTransportButtons();

    // Starts on notes, and goes through the switch rather than setting the flag
    // directly so that the button states, the section heading, the Clear label
    // and which grid is visible all come from one place.
    setEditingDrums(false);

    startTimerHz(30);
}

SequencerPanel::~SequencerPanel()
{
    stopTimer();
}

void SequencerPanel::changeView(int delta, bool zoom)
{
    if (zoom)
    {
        // Zoom steps through musical amounts rather than continuously, so the
        // bar lines always land somewhere sensible.
        static const int levels[] = {4, 8, 16, 32, 64};
        constexpr int numLevels = (int) (sizeof(levels) / sizeof(levels[0]));

        int current = 0;
        for (int i = 0; i < numLevels; ++i)
            if (levels[i] == timeView.visibleSteps)
                current = i;

        // Zooming in shows fewer steps, so a positive delta moves down the list.
        int next = juce::jlimit(0, numLevels - 1, current - delta);

        // Keep the middle of the view where it was, which is what makes zoom
        // feel like it happened around what you were looking at.
        int centre = timeView.firstStep + timeView.visibleSteps / 2;
        timeView.visibleSteps = levels[next];
        timeView.firstStep = centre - timeView.visibleSteps / 2;
    }
    else
    {
        timeView.firstStep -= delta * juce::jmax(1, timeView.visibleSteps / 4);
    }

    timeView.firstStep = juce::jlimit(0, juce::jmax(0, StepSequencer::maxSteps - timeView.visibleSteps),
                                       timeView.firstStep);

    syncScrollBar();
    repaintTimeline();
}

void SequencerPanel::repaintTimeline()
{
    grid.repaint();
    drumGrid.repaint();
    automationStrip.repaint();
}

void SequencerPanel::syncScrollBar()
{
    timeScrollBar.setCurrentRange((double) timeView.firstStep, (double) timeView.visibleSteps,
                                   juce::dontSendNotification);
    timeScrollBar.setVisible(timeView.visibleSteps < StepSequencer::maxSteps);
}

void SequencerPanel::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &pitchScrollBar)
    {
        // The bar counts downward from the highest degree, the way a piano roll
        // does: dragging it down moves you down the keyboard.
        grid.setTopDegree(grid.highestDegree() - (int) std::round(newRangeStart));
        return;
    }

    timeView.firstStep = juce::jlimit(0, juce::jmax(0, StepSequencer::maxSteps - timeView.visibleSteps),
                                       (int) std::round(newRangeStart));
    repaintTimeline();
}

void SequencerPanel::syncPitchScrollBar()
{
    int total = grid.totalRows();
    int visible = grid.numRows();

    pitchScrollBar.setRangeLimits(0.0, (double) total, juce::dontSendNotification);
    pitchScrollBar.setCurrentRange((double) (grid.highestDegree() - grid.topDegree()),
                                    (double) visible, juce::dontSendNotification);
    pitchScrollBar.setVisible(visible < total);
}

void SequencerPanel::showAddLaneMenu()
{
    // The parameter chooser: a lane can be drawn without performing it first.
    // Grouped by section, because the registry went from thirty-two entries to
    // sixty-three and one flat scrolling column of them is a list you read
    // rather than a menu you pick from.
    juce::PopupMenu menu;

    auto& sequencer = processorRef.getSequencer();

    // Gathered by group rather than split wherever the group changes, because
    // the registry is append-only: everything added for MIDI learn sits after
    // the original thirty-two, so Glide comes long after the other oscillator
    // dials. Walking it in order would open a second "Oscillators" submenu
    // further down the menu instead of putting Glide in the first one.
    std::vector<std::pair<juce::String, juce::PopupMenu>> sections;

    // Filtered the way the strip is. A lane added from the other grid's list
    // would vanish the moment it was made, which looks exactly like the menu
    // not working. With the override on, the whole list is offered - sixteen
    // groups, which is also why filtering it is worth doing at all.
    const auto view = currentLaneView();

    for (int id = 0; id < paramreg::count(); ++id)
    {
        if (!laneBelongsTo(id, view))
            continue;

        juce::String group = paramreg::group(id);

        auto found = std::find_if(sections.begin(), sections.end(),
                                  [&group](const auto& s) { return s.first == group; });

        if (found == sections.end())
        {
            sections.push_back({group, juce::PopupMenu{}});
            found = sections.end() - 1;
        }

        found->second.addItem(id + 1, paramreg::name(id), !sequencer.isLaneUsed(id),
                              sequencer.isLaneUsed(id));
    }

    for (auto& section : sections)
        menu.addSubMenu(section.first, section.second);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addLaneButton).withMinimumWidth(180),
                       [this](int result)
                       {
                           if (result <= 0)
                               return;

                           int id = result - 1;
                           processorRef.getSequencer().createLane(id, processorRef.getParameters());
                           layoutAutomation();
                           lcd.print(juce::String(paramreg::name(id))
                                     + " lane added - drag in it to draw, or press REC and move the dial.");
                       });
}

void SequencerPanel::refreshFromProcessor()
{
    // A pattern that arrived with a project may sit outside the rows currently
    // on screen, so the view goes to meet it.
    grid.scrollToShowPattern();
    syncPitchScrollBar();

    auto& params = processorRef.getParameters();
    if (lengthKnob != nullptr) lengthKnob->setRealValue(params.seqLength.load());
    if (rateKnob != nullptr) rateKnob->setRealValue(params.seqDivision.load());
    if (rootKnob != nullptr) rootKnob->setRealValue(params.seqRootNote.load());

    updateTransportButtons();
    layoutAutomation();
    grid.repaint();
    drumGrid.repaint();
    repaint();
}

void SequencerPanel::updateTransportButtons()
{
    auto& params = processorRef.getParameters();

    bool playing = params.seqPlaying.load() > 0.5f;
    bool recording = params.seqRecord.load() > 0.5f;

    playButton.setButtonText(playing ? "Stop" : "Play");
    playButton.setColour(juce::TextButton::buttonColourId,
                         playing ? ui::colours::accent.darker(0.5f) : ui::colours::panel);

    recordButton.setColour(juce::TextButton::buttonColourId,
                           recording ? juce::Colour(0xffb3413f) : ui::colours::panel);
}

void SequencerPanel::layoutAutomation()
{
    // The view first, because refreshLanes reads it. Set on every layout
    // rather than only when the grid or the override changes, so no path to
    // this function can draw one view's lanes under the other's grid.
    const auto view = currentLaneView();
    automationStrip.setView(view);

    automationStrip.setSize(automationViewport.getWidth() > 0
                                ? automationViewport.getWidth() - (automationViewport.isVerticalScrollBarShown() ? 10 : 0)
                                : getWidth(),
                            automationStrip.getHeight());
    automationStrip.refreshLanes();
    automationStrip.repaint();

    updateLaneFilterControls(view);
    repaint();
}

LaneView SequencerPanel::currentLaneView() const
{
    if (showAllLanes)
        return LaneView::all;

    return editingDrums ? LaneView::drums : LaneView::synth;
}

void SequencerPanel::updateLaneFilterControls(LaneView view)
{
    const auto hidden = hiddenLaneCount(processorRef.getSequencer(), view);

    // The title says what the strip holds, so the filter is never invisible.
    // "Synth" rather than "Notes" even though the grid button says Notes: the
    // lanes are the synth's DIALS, and none of them is a note.
    automationSection.setTitle(view == LaneView::all     ? "Automation"
                               : view == LaneView::drums ? "Drum Automation"
                                                         : "Synth Automation");

    // Counting what is hidden is the whole defence against the filter's one
    // real failure, which is a lane recording away where nobody can see it.
    // Warm when there is something to find, so it reads as a notice and not
    // as a label.
    showAllLanesButton.setButtonText(hidden > 0 ? "Show all (" + juce::String(hidden) + " hidden)"
                                                : juce::String("Show all"));

    showAllLanesButton.setColour(juce::TextButton::buttonColourId,
                                 showAllLanes ? ui::colours::accent.darker(0.5f)
                                              : ui::colours::panel);
    showAllLanesButton.setColour(juce::TextButton::textColourOffId,
                                 hidden > 0 ? ui::colours::accentWarm : ui::colours::textPrimary);

    // Clear removes what is SHOWN. "Clear All" beside a strip showing three of
    // seven lanes would be a button that deletes four things you were not
    // shown, which is the one thing a filter must never make easy. So it says
    // "All" only when all is what you are looking at, and is off when there is
    // nothing in view to clear.
    clearAutomationButton.setButtonText(view == LaneView::all ? "Clear All" : "Clear");
    clearAutomationButton.setEnabled(automationStrip.visibleLaneCount() > 0);
}

void SequencerPanel::timerCallback()
{
    auto& sequencer = processorRef.getSequencer();

    int step = sequencer.isPlaying() ? sequencer.getCurrentStep() : -1;
    if (step != shownStep)
    {
        shownStep = step;
        repaintTimeline();
    }

    // A lane appears the moment a dial is recorded, so the strip has to notice
    // without anything telling it.
    int laneCount = sequencer.getNumUsedLanes();
    if (laneCount != shownLaneCount)
    {
        // Going from no lanes to some (or back) changes how the page divides,
        // so the whole layout is redone rather than only the strip.
        bool wasEmpty = (shownLaneCount <= 0);
        shownLaneCount = laneCount;

        if (wasEmpty != (laneCount <= 0))
            resized();
        else
            layoutAutomation();
    }
}

void SequencerPanel::paint(juce::Graphics& g)
{
    g.fillAll(ui::colours::background);

    if (shownLaneCount == 0)
    {
        g.setColour(ui::colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("No lanes yet - press REC and move a dial while it plays, or use Add Lane to draw one.",
                   automationViewport.getBounds().reduced(10, 0), juce::Justification::centredLeft);
    }
    else if (automationStrip.visibleLaneCount() == 0)
    {
        // Lanes exist, and the filter is hiding every one of them. Said in the
        // strip itself, where the eye goes looking for them, as well as on the
        // button - an empty strip over a project with automation in it is the
        // exact moment somebody concludes the automation has been lost.
        const auto view = currentLaneView();
        const auto hidden = hiddenLaneCount(processorRef.getSequencer(), view);
        const juce::String kind = view == LaneView::drums ? "drum" : "synth";
        const juce::String other = view == LaneView::drums ? "synth" : "drum";

        g.setColour(ui::colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("No " + kind + " lanes - " + juce::String(hidden) + " " + other
                       + (hidden == 1 ? " lane is" : " lanes are")
                       + " hidden. Show all to see them, or Add Lane to draw one here.",
                   automationViewport.getBounds().reduced(10, 0), juce::Justification::centredLeft);
    }
}

void SequencerPanel::setEditingDrums(bool drums)
{
    editingDrums = drums;

    grid.setVisible(!drums);
    drumGrid.setVisible(drums);

    // The pitch bar belongs to the note grid. Drums have twelve fixed rows and
    // nothing to scroll through, so a bar that moved nothing would just be a
    // control that does not work.
    pitchScrollBar.setVisible(!drums);

    // Which one is selected, shown the way this page already shows Play being
    // held down: by the button's colour, not by its toggle state.
    //
    // setToggleState was the obvious thing and it was wrong. A TextButton that
    // is toggled on takes its text from textColourOnId, which in this look and
    // feel is dark - so the ACTIVE view rendered as near-invisible grey while
    // the inactive one stayed bright, saying exactly the opposite of the truth.
    // Only visible by looking at it.
    notesViewButton.setColour(juce::TextButton::buttonColourId,
                              !drums ? ui::colours::accent.darker(0.5f) : ui::colours::panel);
    drumsViewButton.setColour(juce::TextButton::buttonColourId,
                              drums ? ui::colours::accent.darker(0.5f) : ui::colours::panel);

    // Generate writes a melodic line from the density and range dials, and
    // neither means anything to a drum row. Disabled rather than hidden, so the
    // page does not reflow every time the view changes.
    generateButton.setEnabled(!drums);

    clearButton.setButtonText(drums ? "Clear Drums" : "Clear Steps");
    gridSection.setTitle(drums ? "Drums" : "Notes");

    resized();
    repaint();
}

void SequencerPanel::resized()
{
    auto area = getLocalBounds();

    // ---- Transport -------------------------------------------------------
    auto transport = area.removeFromTop(96);
    transportSection.setBounds(transport);

    auto inner = transport.reduced(10, 0).withTrimmedTop(18).withTrimmedBottom(6);
    auto buttons = inner.removeFromLeft(200);
    auto topButtons = buttons.removeFromTop(buttons.getHeight() / 2).reduced(0, 3);
    playButton.setBounds(topButtons.removeFromLeft(94));
    topButtons.removeFromLeft(8);
    recordButton.setBounds(topButtons);

    auto bottomButtons = buttons.reduced(0, 3);
    generateButton.setBounds(bottomButtons.removeFromLeft(94));
    bottomButtons.removeFromLeft(8);
    clearButton.setBounds(bottomButtons);

    inner.removeFromLeft(12);

    // What REC captures, and how tightly.
    auto recordOptions = inner.removeFromLeft(104);
    recordNotesButton.setBounds(recordOptions.removeFromTop(recordOptions.getHeight() / 2).reduced(0, 4));
    quantiseButton.setBounds(recordOptions.reduced(0, 4));

    inner.removeFromLeft(14);
    int knobWidth = juce::jmax(58, inner.getWidth() / 5);
    lengthKnob->setBounds(inner.removeFromLeft(knobWidth).reduced(3, 0));
    rateKnob->setBounds(inner.removeFromLeft(knobWidth).reduced(3, 0));
    rootKnob->setBounds(inner.removeFromLeft(knobWidth).reduced(3, 0));
    densityKnob->setBounds(inner.removeFromLeft(knobWidth).reduced(3, 0));
    rangeKnob->setBounds(inner.removeFromLeft(knobWidth).reduced(3, 0));

    area.removeFromTop(10);

    // ---- Automation, at the foot and sharing the grid's columns ----------
    // Only takes the space it needs: an empty strip leaves the note rows tall
    // enough to aim at, and it grows to a third of the page once lanes exist.
    int automationHeight = shownLaneCount > 0 ? juce::jlimit(120, 280, area.getHeight() / 3) : 84;
    auto automation = area.removeFromBottom(automationHeight);
    automationSection.setBounds(automation);

    auto automationHeader = automation.reduced(10, 0).removeFromTop(18);
    clearAutomationButton.setBounds(automationHeader.removeFromRight(76).withHeight(16));
    automationHeader.removeFromRight(6);
    addLaneButton.setBounds(automationHeader.removeFromRight(86).withHeight(16));

    // Wide enough for "Show all (99 hidden)" at the header's text size, so the
    // count never truncates to an ellipsis - the count is the point of it.
    automationHeader.removeFromRight(6);
    showAllLanesButton.setBounds(automationHeader.removeFromRight(128).withHeight(16));

    automationViewport.setBounds(automation.reduced(8, 0).withTrimmedTop(20).withTrimmedBottom(6));

    area.removeFromBottom(10);

    // ---- Note grid -------------------------------------------------------
    gridSection.setBounds(area);

    // Zoom sits on the section's title row, where a piano roll usually keeps it.
    auto gridHeader = area.reduced(8, 0).removeFromTop(18);
    zoomInButton.setBounds(gridHeader.removeFromRight(24).withHeight(16));
    gridHeader.removeFromRight(4);
    zoomOutButton.setBounds(gridHeader.removeFromRight(24).withHeight(16));

    // Which pattern is being edited, on the section's title row beside the
    // zoom. Two buttons rather than one that toggles, so the page always says
    // which of the two you are on rather than which one you would get.
    gridHeader.removeFromRight(10);
    drumsViewButton.setBounds(gridHeader.removeFromRight(52).withHeight(16));
    gridHeader.removeFromRight(2);
    notesViewButton.setBounds(gridHeader.removeFromRight(52).withHeight(16));

    auto gridArea = area.reduced(8, 0).withTrimmedTop(18).withTrimmedBottom(6);
    timeScrollBar.setBounds(gridArea.removeFromBottom(11).withTrimmedLeft(StepGrid::gutterWidth));

    // Pitch runs down the right-hand edge, clear of the ruler so it lines up
    // with the rows it moves. The drum grid has no pitch, so it takes the full
    // width the bar would otherwise occupy.
    if (editingDrums)
    {
        drumGrid.setBounds(gridArea);
    }
    else
    {
        pitchScrollBar.setBounds(gridArea.removeFromRight(11).withTrimmedTop(18));
        grid.setBounds(gridArea);
    }

    syncScrollBar();
    syncPitchScrollBar();
    layoutAutomation();
}
} // namespace wavelathe
