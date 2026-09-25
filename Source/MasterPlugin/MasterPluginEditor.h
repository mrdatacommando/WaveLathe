// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "MasterPluginProcessor.h"
#include "MasteringPanel.h"
#include "UiComponents.h"

namespace wavelathe
{
// The effect plugin's window: the same Master page the synth has, and almost
// nothing else. There is nothing else it should have - the page IS the product.
class MasterPluginEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MasterPluginEditor(MasterPluginProcessor& processor);
    ~MasterPluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // Automation and a second window on the same plugin can both move a value
    // without this editor being the one that did it, so the dials are pulled
    // back into step on a timer rather than only when they are built.
    //
    // A dial being dragged is not fought over: what the drag wrote went to the
    // host, and what comes back is the same number, so the reading lands on the
    // step the dial is already on and nothing moves.
    void timerCallback() override;

    MasterPluginProcessor& processorRef;

    ui::WaveLatheLookAndFeel lookAndFeel;
    MasteringPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterPluginEditor)
};
} // namespace wavelathe
