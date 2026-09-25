// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ParameterRegistry.h"
#include "StepSequencer.h"
#include <vector>

namespace wavelathe
{
// Which automation lanes the strip under the grid shows.
//
// The Sequencer page edits two patterns, notes and drums, over one shared
// automation strip. Since 0.44.0 every drum dial is automatable - 108 of them -
// so a project that automates a few drum levels puts those lanes in the same
// strip you are looking at while editing a bassline, and the reverse. The plan
// named three answers when it saw this coming: show everything, filter by
// view, or filter with an override. This is the third.
//
// Filtered by default because a lane is only useful where you can see it
// against the grid it belongs to. The override exists because of the plan's
// own objection to filtering: a lane you cannot see is a lane you forget is
// recording. So the page always says how many lanes are hidden, and one tick
// shows them.
//
// Header-only and free of any component, so the strip, the Clear button and
// SequencerTest all ask the SAME function. The strip showing one set while
// Clear removed another would be the worst bug this could have - clearing
// automation you were never shown - and one function is what rules it out.
enum class LaneView
{
    synth, // the note grid is up: the synth's dials
    drums, // the drum grid is up: the kit's dials
    all    // the override
};

// Whether a lane for this parameter belongs in the strip for this view.
inline bool laneBelongsTo(int parameterId, LaneView view)
{
    if (view == LaneView::all)
        return true;

    return paramreg::isDrumParameter(parameterId) == (view == LaneView::drums);
}

// The lanes a view shows, in registry order - which is the order the strip
// draws them in.
inline std::vector<int> shownLanes(const StepSequencer& sequencer, LaneView view)
{
    std::vector<int> ids;

    for (int id = 0; id < paramreg::count(); ++id)
        if (sequencer.isLaneUsed(id) && laneBelongsTo(id, view))
            ids.push_back(id);

    return ids;
}

// How many lanes exist that this view does not show. What the override's
// label counts, so that hidden never means forgotten.
inline int hiddenLaneCount(const StepSequencer& sequencer, LaneView view)
{
    int hidden = 0;

    for (int id = 0; id < paramreg::count(); ++id)
        if (sequencer.isLaneUsed(id) && !laneBelongsTo(id, view))
            ++hidden;

    return hidden;
}
} // namespace wavelathe
