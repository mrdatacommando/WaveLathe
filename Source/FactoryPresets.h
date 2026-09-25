// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "FxpPreset.h"
#include <vector>

namespace wavelathe
{
// The bank the synth ships with, so it opens onto something playable rather
// than a bare sine. Built in code rather than as files: nothing to install, and
// the bank cannot go missing or fall out of step with the parameter set.
namespace FactoryPresets
{
enum class Category
{
    bass = 0,
    lead,
    pad,
    pluck,
    keys,
    sequence,
    effect,
    numCategories
};

struct Entry
{
    juce::String name;
    Category category;
    PresetValues values;
};

const char* categoryName(int category);

// Every factory preset, in category order.
const std::vector<Entry>& all();

// Case-insensitive match on name or category; an empty term matches everything.
std::vector<const Entry*> search(const juce::String& term, int categoryFilter);

// The patch the synth starts on, and what "Init" resets to.
PresetValues init();
} // namespace FactoryPresets
} // namespace wavelathe
