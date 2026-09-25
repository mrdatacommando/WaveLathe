// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UiComponents.h"
#include "Mastering/MasteringControls.h"
#include <functional>
#include <memory>
#include <vector>

namespace wavelathe
{

// The Master page: the mastering chain's controls and its two gain reduction
// meters, laid out in the order the signal meets them.
//
// Self-contained, the way SequencerPanel is, rather than another few dozen
// members on an editor that is already three and a half thousand lines. It owns
// its dials and its layout, and it reaches the values behind them through a
// small interface rather than through whatever happens to own them - which is
// what lets the same page appear in the synth and in a separate effect plugin.
//
// None of these dials report to the host, because none of these parameters are
// in ParameterRegistry - which is why this panel can build its own knobs
// instead of going through the editor's factory. The factory exists largely to
// tell the host that a registered parameter moved, and there is nothing here to
// tell it about.
class MasteringPanel : public juce::Component, private juce::Timer
{
public:
    explicit MasteringPanel(mastering::Controls& controlsToDrive);
    ~MasteringPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Pulls every dial back into step with the values behind them, for when a
    // preset has been loaded underneath the panel.
    void refresh();

    // What to say on the main display. The panel does not own the LCD and
    // should not reach across the editor to find it.
    std::function<void(const juce::String&)> onMessage;

    // Something audible changed. Unset in the effect plugin, which has nothing
    // drawing its sound; the synth's Master page redraws its displays on it.
    std::function<void()> onChanged;

    // Which number the first of the four stages shows. 1 on its own, as the
    // effect plugin has it; the synth's Master page puts its seven bus effects
    // ahead of these and numbers the whole page as one chain, 1 to 11.
    void setFirstStage(int first);

private:
    void timerCallback() override;

    void buildKnobs();
    void layoutSection(ui::FlowSection& section, juce::Rectangle<int> area,
                       const std::vector<ui::ParameterKnob*>& knobs);

    // One gain reduction meter: a bar that grows downward from the top, because
    // gain reduction is something being taken away rather than a level being
    // reached, and a meter that rose would read as the opposite of what it is.
    void paintMeter(juce::Graphics& g, juce::Rectangle<int> area, float reductionDb,
                    float fullScaleDb, const juce::String& label) const;

    mastering::Controls& controls;

    juce::ToggleButton enableButton{"Mastering on"};

    // Four stages in a chain, numbered, so the order the signal takes is
    // readable from the panel rather than something to be remembered. The trim
    // is a stage of its own precisely because its position matters: it drives
    // the limiter, and putting it anywhere later would let it push the signal
    // back over the ceiling.
    ui::FlowSection saturationSection{"Saturation", ui::colours::accentWarm};
    ui::FlowSection compressorSection{"Compressor", ui::colours::accent};
    ui::FlowSection trimSection{"Trim", ui::colours::accentWarm};
    ui::FlowSection limiterSection{"Limiter", ui::colours::accent};

    std::unique_ptr<ui::ParameterKnob> driveKnob, satMixKnob;
    std::unique_ptr<ui::ParameterKnob> thresholdKnob, ratioKnob, attackKnob, releaseKnob,
        makeupKnob, compMixKnob;
    std::unique_ptr<ui::ParameterKnob> trimKnob;
    std::unique_ptr<ui::ParameterKnob> ceilingKnob, limitReleaseKnob;

    juce::Rectangle<int> compressorMeterArea, limiterMeterArea;

    // What the meters are currently showing, as opposed to what the chain
    // reported this instant. A gain reduction meter that jumped to each new
    // reading would be unreadable; it falls back slowly and catches up at once,
    // which is how every meter of this kind behaves.
    float shownCompressorDb = 0.0f;
    float shownLimiterDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasteringPanel)
};
} // namespace wavelathe
