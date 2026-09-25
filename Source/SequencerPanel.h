// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "UiComponents.h"
#include "LaneFilter.h"
#include <memory>
#include <vector>

namespace wavelathe
{
// The sequencer page, laid out the way every piano roll is: time runs left to
// right, pitch runs bottom to top, a key strip names the rows, and the lanes
// that describe what happens over the same stretch of time sit underneath it
// sharing the same columns.
//
// Pitch rows are scale DEGREES rather than note numbers, so the grid only ever
// offers notes that are in key, and changing key moves the whole pattern.

// ---- The note grid, its ruler, velocity footer and lock row ---------------
// How much of the timeline is on screen. Shared by the grid and the lanes so
// their columns always line up, however far you are zoomed in.
struct TimeView
{
    int firstStep = 0;
    int visibleSteps = 64;

    int lastStep() const { return firstStep + visibleSteps; }
};

class StepGrid : public juce::Component,
                 public juce::TooltipClient
{
public:
    StepGrid(WaveLatheProcessor& processor, TimeView& view);

    void paint(juce::Graphics&) override;

    // What the row under the mouse does, and - for slide - what it needs before
    // it will do anything. The footer rows are drawn cells rather than separate
    // components, so the hint is worked out from where the pointer is.
    juce::String getTooltip() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Columns line up with the automation lanes underneath, so both use this.
    static constexpr int gutterWidth = 62;

    std::function<void()> onPatternChanged;
    std::function<void(int delta, bool zoom)> onViewShouldChange;
    std::function<void()> onPitchViewChanged;

    // Moving the pitch window, and putting the pattern back in view when it has
    // been written somewhere you are not looking.
    // Pitch scrolls as well as time: the grid covers five octaves of degrees,
    // of which only as many rows as will fit at a readable height are shown.
    // Without this a note written outside the window simply could not be seen.
    void setTopDegree(int degree);
    void scrollToShowPattern();

    // What the pitch scrollbar needs to describe the window it is moving.
    int numRows() const;       // how many rows are on screen
    int totalRows() const;     // how many there are altogether
    int highestDegree() const;
    int lowestDegree() const;
    int topDegree() const;     // the degree drawn on the top row

private:
    enum class Zone { none, notes, velocity, probability, octave, accent, slide, lock };

    void handleMouse(const juce::MouseEvent& event, bool isDrag);
    Zone zoneFor(juce::Point<int> position) const;

    // The note whose right-hand edge is under this point, or -1. Dragging that
    // edge is how a note is made longer, the way it works in every piano roll.
    int noteEdgeAt(juce::Point<int> position) const;

    int degreeForRow(int row) const;
    int rowForDegree(int degree) const;
    bool isRootRow(int degree) const;
    juce::String labelForDegree(int degree) const;

    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> noteArea() const;
    juce::Rectangle<int> velocityArea() const;

    // How likely a step is to sound, and which octave it sounds in. Both are
    // things the sequencer has always played and saved; these are the rows that
    // let you set them by hand rather than only by Generate.
    juce::Rectangle<int> probabilityArea() const;
    juce::Rectangle<int> octaveArea() const;

    // Which steps are tied to the one after them. A tie is a property of the
    // GAP between two steps rather than of either one, which is why the row
    // draws across to the next step instead of filling its own cell.
    juce::Rectangle<int> slideArea() const;

    // Which steps hit at the pattern's accent level rather than their own
    // velocity. A switch per step, like the lock row.
    juce::Rectangle<int> accentArea() const;
    juce::Rectangle<int> lockArea() const;
    int stepForX(int x) const;
    int tickForX(int x) const;

    WaveLatheProcessor& processorRef;
    TimeView& timeView;
    Zone dragZone = Zone::none;

    // The degree on the top visible row. Left unset until the first paint, so
    // the opening view can be centred on the key the pattern is in.
    int pitchTop = 0;
    bool pitchInitialised = false;
    void initialisePitch();
    int resizingNote = -1; // the step whose length is being dragged

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepGrid)
};

// ---- One automation lane: a header you can collapse, and its curve --------
// ---- The drum grid --------------------------------------------------------
//
// Twelve named rows against the same columns as the note grid above it, on the
// same TimeView, so switching between the two does not move the bar under your
// hand. A cell is on or off; velocity and accent are properties of a cell that
// it keeps when switched off.
//
// A separate component rather than a mode inside StepGrid. The two draw
// different things - one has pitch rows, a ruler, a velocity footer and notes
// that span steps; this has a fixed twelve rows of on and off - and a StepGrid
// that could be either would be a long chain of "if drums" through every paint
// and mouse handler.
class DrumGrid : public juce::Component,
                 public juce::TooltipClient
{
public:
    DrumGrid(WaveLatheProcessor& processor, TimeView& view);

    void paint(juce::Graphics&) override;
    juce::String getTooltip() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    std::function<void()> onPatternChanged;
    std::function<void(int delta, bool zoom)> onViewShouldChange;

private:
    // A CELL, not a step. There are project::drumSubdivisions of them to each
    // column of the note grid above, which is how a hit lands between beats.
    int cellForX(int x) const;
    int voiceForY(int y) const;
    juce::Rectangle<int> rowArea(int voice) const;
    juce::Rectangle<int> cellArea(int voice, int cell) const;

    // Painting a run of cells: the first click decides whether the drag is
    // switching cells on or off, and every cell it crosses gets that same
    // answer. Without it, dragging across a row toggles each cell and a swipe
    // meant to clear four steps leaves them exactly as they were.
    void applyAt(juce::Point<int> position, bool firstClick);

    // Shift-drag moves a placed hit off its beat instead of drawing. Held on a
    // modifier because drawing is the common action and has to stay a single
    // unmodified stroke - and because a plain drag that sometimes drew and
    // sometimes nudged would depend on where inside a cell it started, which is
    // not something anybody could aim at.
    void nudgeAt(juce::Point<int> position, bool firstClick);

    WaveLatheProcessor& processorRef;
    TimeView& timeView;

    bool paintingOn = true;
    bool nudging = false;
    int nudgeVoice = -1;
    int nudgeCell = -1;
    int nudgeStartX = 0;
    int nudgeStartValue = 0;

    int lastVoice = -1;
    int lastCell = -1;
    int hoverVoice = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumGrid)
};

class AutomationLaneStrip : public juce::Component
{
public:
    AutomationLaneStrip(WaveLatheProcessor& processor, TimeView& view);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Rebuilds the list of lanes and returns the height it now needs.
    int refreshLanes();

    // Which lanes to draw. Set by the page whenever the grid or the override
    // changes, and read by refreshLanes - see LaneFilter.h for the rule.
    void setView(LaneView newView) { view = newView; }

    // How many lanes the strip is drawing, as of the last refreshLanes. Not
    // how many exist: with the filter on, the two differ, and the difference
    // is what the page reports as hidden.
    int visibleLaneCount() const { return (int) lanes.size(); }

    std::function<void()> onLanesChanged;

private:
    struct LaneRow
    {
        int parameterId = -1;
        bool expanded = true;
        int y = 0;
        int height = 0;
    };

    static constexpr int headerHeight = 22;
    static constexpr int curveHeight = 62;

    void writeCurve(const juce::MouseEvent& event);
    int laneAt(juce::Point<int> position) const;

    WaveLatheProcessor& processorRef;
    TimeView& timeView;
    std::vector<LaneRow> lanes;
    std::vector<bool> expandedByParameter;
    LaneView view = LaneView::synth;
    int draggingLane = -1;

    // Where the last drag point landed, so a quick swipe draws a continuous
    // ramp instead of leaving gaps at every tick the mouse skipped over.
    int lastDragTick = -1;
    float lastDragValue = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationLaneStrip)
};

// ---- The page ------------------------------------------------------------
class SequencerPanel : public juce::Component,
                       private juce::Timer,
                       private juce::ScrollBar::Listener
{
public:
    explicit SequencerPanel(WaveLatheProcessor& processor, ui::LcdDisplay& lcd);
    ~SequencerPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void refreshFromProcessor();

private:
    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;
    void updateTransportButtons();
    void showAddLaneMenu();
    void layoutAutomation();

    WaveLatheProcessor& processorRef;
    ui::LcdDisplay& lcd;

    juce::TextButton playButton{"Play"};
    juce::TextButton recordButton{"REC"};
    juce::TextButton generateButton{"Generate"};
    juce::TextButton clearButton{"Clear Steps"};
    juce::TextButton addLaneButton{"+ Add Lane"};
    juce::TextButton clearAutomationButton{"Clear All"};

    // The override on the lane filter: every lane, whichever grid is up.
    //
    // A TextButton whose colour carries the state, like the Notes and Drums
    // buttons, rather than a toggle - see setEditingDrums for why toggle state
    // on a TextButton reads backwards in this look and feel. Its label counts
    // the lanes the filter is hiding, so a lane you cannot see is never a lane
    // you do not know about.
    juce::TextButton showAllLanesButton{"Show all"};
    bool showAllLanes = false;

    // Which lanes the strip should be showing right now: the grid that is up,
    // unless the override is on.
    LaneView currentLaneView() const;

    // Puts the section's title, the override's label and the Clear button in
    // step with what the strip is showing. After refreshLanes, since two of
    // the three read what it found.
    void updateLaneFilterControls(LaneView view);
    juce::TextButton zoomInButton{"+"};
    juce::TextButton zoomOutButton{"-"};
    juce::ToggleButton recordNotesButton{"Rec notes"};
    juce::ToggleButton quantiseButton{"Quantise"};

    // Which pattern you are EDITING. Not which one is running - both run. A
    // groovebox behaves this way, and it is what lets a bassline and a beat
    // play together, which a mode switch would forbid.
    juce::TextButton notesViewButton{"Notes"};
    juce::TextButton drumsViewButton{"Drums"};
    bool editingDrums = false;
    void setEditingDrums(bool drums);

    TimeView timeView;
    juce::ScrollBar timeScrollBar{false};

    // Pitch runs off the top and bottom of the grid, so it needs a bar of its
    // own - a note written outside the window is otherwise invisible.
    juce::ScrollBar pitchScrollBar{true};
    void syncPitchScrollBar();
    void changeView(int delta, bool zoom);
    void syncScrollBar();

    // Everything that draws against the shared TimeView, in one call.
    //
    // This was three separate repaint lists, and the drum grid was in only one
    // of them. Zooming or scrolling on the drum page really did move the view -
    // it just never redrew the grid, so the only thing that visibly moved was
    // the automation strip underneath, which looked like the zoom belonged to
    // the automation. A list somebody has to remember to extend is a bug
    // waiting for the next component that draws on this timeline.
    void repaintTimeline();

    std::unique_ptr<ui::ParameterKnob> lengthKnob, rateKnob, rootKnob, densityKnob, rangeKnob;

    ui::FlowSection transportSection{"Transport", ui::colours::accentMod};
    ui::FlowSection gridSection{"Notes", ui::colours::accent};
    ui::FlowSection automationSection{"Automation", ui::colours::accentWarm};

    StepGrid grid;
    DrumGrid drumGrid;
    juce::Viewport automationViewport;
    AutomationLaneStrip automationStrip;

    int shownStep = -1;
    int shownLaneCount = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerPanel)
};
} // namespace wavelathe
