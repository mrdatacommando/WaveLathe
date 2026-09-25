// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "OutputTap.h"
#include "SpectrumAnalyser.h"
#include "SynthParameters.h"
#include <vector>

namespace wavelathe
{
// What is coming out right now, as opposed to the two views beside it - which
// are an offline render of one C3 note and tell you about the patch rather than
// about the performance.
//
// Two halves, because clipping is two questions. The scope says what the
// waveform is DOING - flat tops, the squared shoulders drive leaves - and the
// meter says how far over it went and whether it stayed there. Either alone
// leaves you guessing at the other.
class OutputScope : public juce::Component,
                    private juce::Timer
{
public:
    OutputScope(OutputTap& tapToWatch, const SynthParameters& parametersToRead);
    ~OutputScope() override;

    void paint(juce::Graphics&) override;

    // Clears the clip indicator without waiting for it to time out, so the next
    // pass starts from a known state rather than from whatever the last one
    // left lit.
    void mouseDown(const juce::MouseEvent&) override;

    juce::String getTooltip();

    // Which EQ the spectrum draws its curve for: the Synth page's, or the
    // Master page's bus EQ. The page decides - the curve is only worth drawing
    // for the EQ whose dials are in front of you.
    void showBusEq(bool shouldShowBusEq);

private:
    void timerCallback() override;

    void paintScope(juce::Graphics&, juce::Rectangle<float> area);
    void paintSpectrum(juce::Graphics&, juce::Rectangle<float> area);
    void paintEqCurve(juce::Graphics&, juce::Rectangle<float> area);

    // The two views want the same space and are never read at once, so they
    // share it rather than each getting half of a strip that is already short.
    enum class View { scope, spectrum };
    View view = View::scope;

    bool busEq = false;
    juce::Rectangle<int> viewToggleArea() const;

    // Frequency to x and back, log scaled, because an octave is an octave
    // wherever it sits and a linear axis spends four fifths of its width on the
    // top two octaves where almost nothing needs deciding.
    static double frequencyAt(float proportion);
    static float proportionOf(double frequency);
    void paintMeter(juce::Graphics&, juce::Rectangle<float> area);

    // Where in the captured window to start drawing, so the waveform stands
    // still instead of sliding. Without this a scope of a steady tone is an
    // unreadable blur of every phase at once.
    int findTrigger() const;

    static float toBarPosition(float magnitude);

    OutputTap& tap;
    const SynthParameters& params;
    SpectrumAnalyser analyser;

    // What the analyser was last told. Its bin frequencies are derived from the
    // rate, so one built at the wrong rate labels every peak wrongly - and the
    // rate is not known until the engine is actually running.
    double analyserRate = 0.0;

    std::vector<float> left;
    std::vector<float> right;

    // The two summed, which is what the transform sees. A spectrum of one
    // channel would miss whatever the other one is doing alone.
    std::vector<float> mono;
    int captured = 0;

    // What the bars show. Instant on the way up so nothing is missed, and a
    // slow fall so a reading lasts long enough to read.
    float displayedLevel[2] = {0.0f, 0.0f};

    // The loudest recent moment, held still and then let go, because a peak
    // that vanished in a sixtieth of a second is a peak you never saw.
    float heldPeak[2] = {0.0f, 0.0f};
    int holdFramesLeft[2] = {0, 0};

    float lastPeakDb = -100.0f;
    bool clipLit = false;

    // Frames the light has left. It is held rather than merely flashed, because
    // a clip that lit for a sixtieth of a second is a clip nobody saw - but it
    // is not held forever either: a light still on from something you fixed
    // twenty minutes ago is not a warning, it is furniture, and the next real
    // clip arrives at a light that is already lit and says nothing.
    int clipFramesLeft = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputScope)
};
} // namespace wavelathe
