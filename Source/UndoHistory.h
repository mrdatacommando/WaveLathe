// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "FxpPreset.h"
#include <deque>

namespace wavelathe
{
class WavetableSet;

// Undo, kept as whole snapshots rather than as a list of reversible commands.
//
// Every destructive thing WaveLathe does - Generate, Clear, loading a patch,
// drawing over a note - already has a way to describe the entire project as
// values, because that is what saving a project does. Remembering the state
// before an action and putting it back is therefore both simpler and harder to
// get wrong than writing an inverse for each one: there is no action that can
// be added later and forgotten about, because the snapshot does not know or
// care what changed.
//
// A snapshot is a couple of kilobytes, so a deep history costs almost nothing.
// The wavetable is held as a pointer rather than copied - the processor keeps
// every set it has loaded alive, so the pointer stays good and a snapshot does
// not have to carry a hundred kilobytes of table data.
class UndoHistory
{
public:
    // Remembers how things are now, labelled with what is about to happen.
    // Call it before the change, not after.
    void record(const PresetValues& state, const WavetableSet* wavetable, const juce::String& action);

    bool canUndo() const { return !past.empty(); }
    bool canRedo() const { return !future.empty(); }

    // What the next undo or redo would reverse, for putting on a menu.
    juce::String undoName() const { return past.empty() ? juce::String() : past.back().action; }
    juce::String redoName() const { return future.empty() ? juce::String() : future.back().action; }

    struct Snapshot
    {
        PresetValues state;
        const WavetableSet* wavetable = nullptr;
        juce::String action;
    };

    // Steps back, handing over what to apply. The current state has to be
    // passed in so it can be put on the redo pile.
    bool undo(const PresetValues& current, const WavetableSet* currentWavetable, Snapshot& outSnapshot);
    bool redo(const PresetValues& current, const WavetableSet* currentWavetable, Snapshot& outSnapshot);

    void clear();
    int getDepth() const { return (int) past.size(); }

private:
    // Deep enough that a long editing session is covered, shallow enough that
    // the memory never becomes a question.
    static constexpr size_t maxEntries = 64;

    std::deque<Snapshot> past;
    std::deque<Snapshot> future;
};
} // namespace wavelathe
