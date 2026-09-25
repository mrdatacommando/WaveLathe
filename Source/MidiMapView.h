// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MidiMap.h"
#include <functional>

namespace wavelathe
{
// Everything the controller is currently doing, in one list. Learn mode answers
// "what is this dial bound to"; this answers the question that actually comes up
// after ten minutes of assigning, which is "what did I put on CC 21" - and that
// one cannot be answered by hovering dials, because the dial you want is the one
// you have forgotten.
//
// Sorted by controller number rather than by parameter, since that is the order
// the hardware is laid out in and the order you read it in when something moves
// that you did not expect.
class MidiMapView : public juce::Component,
                    private juce::ListBoxModel
{
public:
    MidiMapView(MidiMap& mapToShow, std::function<void()> onChanged);
    ~MidiMapView() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static void show(MidiMap& mapToShow, std::function<void()> onChanged);

private:
    struct Row
    {
        int controller = 0;
        int parameterId = -1;
    };

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    void refresh();
    void clearSelected();
    void clearEverything();

    MidiMap& map;
    std::function<void()> changed;

    std::vector<Row> rows;

    juce::ListBox list;
    juce::TextButton clearButton{"Clear"};
    juce::TextButton clearAllButton{"Clear All"};
    juce::TextButton closeButton{"Close"};
    juce::Label countLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMapView)
};
} // namespace wavelathe
