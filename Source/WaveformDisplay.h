// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "OfflineRenderer.h"
#include <atomic>
#include <memory>
#include <vector>

namespace wavelathe
{
// Shows what the current settings actually sound like, by rendering a short
// note through the real synth engine offline and drawing it. Two views: a few
// cycles of the steady waveform (timbre) and the whole note's amplitude
// envelope (shape over time). Re-renders when a parameter changes, throttled
// by a timer so dragging a dial stays smooth.
class WaveformDisplay : public juce::Component, private juce::Timer
{
public:
    explicit WaveformDisplay(WaveLatheProcessor& processor);
    ~WaveformDisplay() override;

    void paint(juce::Graphics&) override;

    // Marks the view stale; the next timer tick re-renders it.
    void markDirty() { dirty = true; }

    // Show one drum slot instead of the patch, or go back to the patch.
    //
    // The Drums page has its own idea of what "the sound" is, and it is not
    // the synth: turning Decay down on the snare while the display shows a
    // sawtooth is a display showing the wrong instrument. Called with whichever
    // slot was last touched.
    void showDrumSlot(int voice);
    void showSynth();

    // The patch as it comes out of the Master page - through the bus effects
    // and the mastering chain - drawn over a faint copy of it going in. The
    // Master page's dials change what the patch becomes rather than what it
    // is, so the useful picture is the difference.
    void showMaster();

private:
    void timerCallback() override;
    void regenerate();
    void regenerateSynth();
    void regenerateDrum(int voice);
    void regenerateMaster();

    // Which of the three is up. A drum slot needs its number as well, which is
    // what drumSlot is for.
    enum class Showing { synth, drum, master };
    Showing showing = Showing::synth;

    WaveLatheProcessor& processorRef;
    std::unique_ptr<OfflineRenderer> renderer;
    std::vector<float> cycleView;    // a few cycles of the sustained portion
    std::vector<float> envelopeView; // amplitude over the whole note

    // On the Master page, the same two views of the note BEFORE the page's
    // processing, drawn faint underneath. Empty everywhere else, which is how
    // paint knows not to draw them.
    std::vector<float> cycleGhost;
    std::vector<float> envelopeGhost;

    // Private copies of the Master page's two chains, run over the preview
    // note on the message thread. Their own instances rather than the
    // processor's, which belong to the audio thread and hold its state.
    MasterEffects previewBus;
    mastering::Chain previewMastering;
    mastering::Parameters previewMasteringParameters;
    bool previewChainsPrepared = false;
    std::atomic<bool> dirty{true};

    // Which drum slot is being shown, or -1 for the patch.
    int drumSlot = -1;

    // What to call what is drawn. "WAVEFORM" for the patch, the drum's own
    // name for a slot - so the two are never confused with one another.
    juce::String waveLabel{"WAVEFORM"};

    // How long the drum hit being shown actually was, for the envelope's
    // readout. Zero while the patch is showing.
    double drumSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};
} // namespace wavelathe
