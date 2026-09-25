// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MasterPluginEditor.h"

namespace wavelathe
{
MasterPluginEditor::MasterPluginEditor(MasterPluginProcessor& processor)
    : juce::AudioProcessorEditor(&processor), processorRef(processor), panel(processor)
{
    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(panel);

    // Wide because the chain is four stages side by side and squeezing them
    // into a column would lose the one thing the layout is saying, which is the
    // order the signal goes through them.
    setResizable(true, true);
    setResizeLimits(900, 320, 2400, 900);
    setSize(1240, 400);

    startTimerHz(10);
}

MasterPluginEditor::~MasterPluginEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void MasterPluginEditor::timerCallback() { panel.refresh(); }

void MasterPluginEditor::paint(juce::Graphics& g) { g.fillAll(ui::colours::background); }

void MasterPluginEditor::resized() { panel.setBounds(getLocalBounds()); }
} // namespace wavelathe
