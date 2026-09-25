// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "UndoHistory.h"
#include <utility>

namespace wavelathe
{
void UndoHistory::record(const PresetValues& state, const WavetableSet* wavetable,
                         const juce::String& action)
{
    past.push_back({state, wavetable, action});

    while (past.size() > maxEntries)
        past.pop_front();

    // Doing something new after undoing abandons what was undone, which is how
    // undo behaves everywhere. Keeping it would mean redo could jump to a state
    // that never followed from where you now are.
    future.clear();
}

bool UndoHistory::undo(const PresetValues& current, const WavetableSet* currentWavetable,
                       Snapshot& outSnapshot)
{
    if (past.empty())
        return false;

    // Taken out and the redo entry built before outSnapshot is touched: a
    // caller passing the same object as both the current state and the
    // destination is the natural way to step back repeatedly, and writing to
    // the destination first would change what "current" meant halfway through.
    auto stepBack = past.back();
    past.pop_back();

    // What is being stepped away from becomes the thing redo returns to, under
    // the same name - redoing "Generate" puts the generated line back.
    future.push_back({current, currentWavetable, stepBack.action});

    outSnapshot = std::move(stepBack);
    return true;
}

bool UndoHistory::redo(const PresetValues& current, const WavetableSet* currentWavetable,
                       Snapshot& outSnapshot)
{
    if (future.empty())
        return false;

    auto stepForward = future.back();
    future.pop_back();

    past.push_back({current, currentWavetable, stepForward.action});

    outSnapshot = std::move(stepForward);
    return true;
}

void UndoHistory::clear()
{
    past.clear();
    future.clear();
}
} // namespace wavelathe
