// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "UiComponents.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace wavelathe
{
// The Master page's bus effects: a second copy of the Synth page's seven
// stages, run over synth and drums together before the mastering chain.
//
// Its own component rather than more of MasteringPanel, because MasteringPanel
// is shared - the separate WaveLatheMaster effect plugin shows the same page,
// and a mix bus of synth and drums means nothing in a plugin that has neither.
// The editor stacks the two on the Master page, this one above, in the order
// the signal meets them.
//
// Builds its own dials, the way MasteringPanel does, rather than going through
// the editor's factory. The factory exists to report registered parameters to
// the host, and none of these are registered - see project::BusFxControl.
// Which also means none of them can be MIDI learned: learn is keyed by the
// same registry id the host sees.
class BusEffectsPanel : public juce::Component
{
public:
    explicit BusEffectsPanel(WaveLatheProcessor& processor);

    void paint(juce::Graphics&) override;
    void resized() override;

    // Pulls every dial back into step with the values behind them, for when a
    // project has been loaded - or an undo applied - underneath the panel.
    void refresh();

    // Said as a dial moves, and overwritten by the next move. The synth's own
    // dials use the display the same way.
    std::function<void(const juce::String&)> onTransient;

    // Said once, and kept.
    std::function<void(const juce::String&)> onMessage;

    // Something audible changed, so anything drawing the Master page's sound
    // is out of date.
    std::function<void()> onChanged;

    // The stage number the first section shows. The page numbers its whole
    // chain as one run - these seven, then the mastering four - so the order
    // the signal takes reads straight off the page.
    static constexpr int firstStage = 1;
    static constexpr int numStages = 7;

private:
    using Control = project::BusFxControl;

    void buildKnobs();
    void layoutSection(ui::FlowSection& section, juce::Rectangle<int> area,
                       std::initializer_list<Control> controls);

    ui::ParameterKnob* knobFor(Control control) const
    {
        return knobs[(size_t) control].get();
    }

    WaveLatheProcessor& processorRef;

    // The same names and colours as the Synth page's sections, since this is
    // the same chain and should be recognised as one at a glance.
    ui::FlowSection distortionSection{"Distortion", ui::colours::accentWarm};
    ui::FlowSection chorusSection{"Chorus", ui::colours::accent};
    ui::FlowSection phaserSection{"Phaser", ui::colours::accent};
    ui::FlowSection delaySection{"Delay", ui::colours::accent};
    ui::FlowSection reverbSection{"Reverb", ui::colours::accent};
    ui::FlowSection eqSection{"EQ", ui::colours::accentWarm};
    ui::FlowSection outputSection{"Output", ui::colours::accentWarm};

    // One slot per control, indexed by BusFxControl. Delay Sync is a switch
    // rather than a dial, so its slot stays empty and the button below is it.
    std::array<std::unique_ptr<ui::ParameterKnob>, project::numBusFxControls> knobs;
    juce::ToggleButton delaySyncButton{"Sync"};

    // Where the group outline goes, worked out in resized and drawn in paint.
    juce::Rectangle<int> groupArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BusEffectsPanel)
};
} // namespace wavelathe
