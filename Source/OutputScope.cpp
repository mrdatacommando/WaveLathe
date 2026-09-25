// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "OutputScope.h"
#include "UiComponents.h"
#include "MasterEffects.h"
#include <cmath>

namespace wavelathe
{
namespace
{
// About 21 ms at 48 kHz. Long enough that a bass note shows a whole cycle,
// short enough that a high one is not a solid block of ink.
constexpr int scopeWindow = 1024;

// One transform's worth, which is more than the scope needs and exactly what
// the analyser does. Capturing once for both is cheaper than capturing twice
// and keeps the two views looking at the same moment.
constexpr int captureSamples = SpectrumAnalyser::fftSize;

// How much of the capture the trigger may search. It needs room to find a
// rising crossing WITHOUT running out of samples to draw after it.
constexpr int scopeSearch = scopeWindow;

constexpr int meterWidth = 56;

// Below this a meter is showing noise, not signal, and the bar is better spent
// on the range anyone actually mixes in.
constexpr float floorDb = -54.0f;

constexpr int peakHoldFrames = 50; // a little under a second at screen rate
constexpr float fallPerFrame = 0.025f;

// How long the clip light stays on for. Twenty seconds is long enough to turn
// back from the keyboard and see what the last thing you played did, and short
// enough that it is still telling you about the patch you are on.
constexpr int clipHoldFrames = 30 * 20; // the timer runs at 30 Hz

// What the spectrum's vertical axis covers. Not the analyser's full 96 dB: in a
// pane this short that would put everything worth looking at in the top third.
constexpr float spectrumTopDb = 0.0f;
constexpr float spectrumBottomDb = -72.0f;

// What the EQ curve's own axis covers, drawn about the centre line. The EQ can
// only reach 12 dB, so a scale a little wider than that keeps a full boost off
// the ceiling where its shape would be lost.
constexpr float eqRangeDb = 16.0f;

constexpr double lowestFrequency = 20.0;
constexpr double decades = 3.0; // 20 Hz to 20 kHz

float toDecibels(float magnitude)
{
    return magnitude > 1.0e-6f ? 20.0f * std::log10(magnitude) : -100.0f;
}
} // namespace

OutputScope::OutputScope(OutputTap& tapToWatch, const SynthParameters& parametersToRead)
    : tap(tapToWatch), params(parametersToRead)
{
    left.resize((size_t) captureSamples, 0.0f);
    right.resize((size_t) captureSamples, 0.0f);
    mono.resize((size_t) captureSamples, 0.0f);

    startTimerHz(30);
}

OutputScope::~OutputScope()
{
    stopTimer();
}

double OutputScope::frequencyAt(float proportion)
{
    return lowestFrequency * std::pow(10.0, decades * (double) juce::jlimit(0.0f, 1.0f, proportion));
}

float OutputScope::proportionOf(double frequency)
{
    if (frequency <= lowestFrequency)
        return 0.0f;

    return juce::jlimit(0.0f, 1.0f,
                        (float) (std::log10(frequency / lowestFrequency) / decades));
}

void OutputScope::timerCallback()
{
    // The engine decides the rate, and not until it starts. Asking every frame
    // and acting only on a change is cheaper than any arrangement where the
    // audio thread has to reach up here to say so.
    const double rate = tap.getSampleRate();
    if (rate != analyserRate)
    {
        analyserRate = rate;
        analyser.prepare(rate);
    }

    captured = tap.readLatest(left.data(), right.data(), captureSamples);

    // Only transformed while it is being looked at. The cost is small but it is
    // not nothing, and a synth spends most of its life with this pane showing
    // the scope.
    if (view == View::spectrum && captured >= SpectrumAnalyser::fftSize)
    {
        for (int i = 0; i < captured; ++i)
            mono[(size_t) i] = 0.5f * (left[(size_t) i] + right[(size_t) i]);

        analyser.update(mono.data(), captured);
    }

    for (int channel = 0; channel < 2; ++channel)
    {
        const float peak = tap.takePeak(channel);

        // Straight up, slowly down. A meter that rose gently would understate
        // exactly the transient that made you look at it.
        displayedLevel[channel] = peak > displayedLevel[channel]
                                      ? peak
                                      : juce::jmax(0.0f, displayedLevel[channel] - fallPerFrame);

        if (peak >= heldPeak[channel])
        {
            heldPeak[channel] = peak;
            holdFramesLeft[channel] = peakHoldFrames;
        }
        else if (holdFramesLeft[channel] > 0)
        {
            --holdFramesLeft[channel];
        }
        else
        {
            heldPeak[channel] = juce::jmax(0.0f, heldPeak[channel] - fallPerFrame);
        }
    }

    lastPeakDb = toDecibels(juce::jmax(heldPeak[0], heldPeak[1]));

    // Latched for twenty seconds, or until clicked. A clip that lit for a frame
    // and went out again is a clip you did not see, which is the same as one
    // that was never reported - but a latch with no end to it stops reporting
    // too, because a light that has been on since you sat down is not about
    // anything you are doing now. Every new clip restarts the clock, so a patch
    // that keeps going over stays lit the whole time it is going over.
    if (tap.takeClipped())
    {
        clipLit = true;
        clipFramesLeft = clipHoldFrames;
    }
    else if (clipLit && --clipFramesLeft <= 0)
    {
        clipLit = false;
    }

    repaint();
}

juce::Rectangle<int> OutputScope::viewToggleArea() const
{
    // The right end of the label row, clear of the OUT / LIVE markings.
    auto row = getLocalBounds().reduced(8).removeFromTop(12);
    return row.removeFromRight(meterWidth + 8 + 84).withTrimmedRight(meterWidth + 8);
}

void OutputScope::mouseDown(const juce::MouseEvent& event)
{
    if (viewToggleArea().contains(event.getPosition()))
    {
        view = view == View::scope ? View::spectrum : View::scope;

        // The decay state belongs to the view that was showing. Carrying it
        // across would draw a curve of a sound from before you switched.
        analyser.reset();
        repaint();
        return;
    }

    if (!clipLit)
        return;

    clipLit = false;
    clipFramesLeft = 0;
    repaint();
}

juce::String OutputScope::getTooltip()
{
    juce::String text = "OUT is what is leaving the synth right now, after the effects and the "
                        "output gain - the two views to the left are an offline render of one "
                        "note and do not move when you play.";

    text += view == View::scope
                ? juce::String("  The trace shows the waveform's shape, so drive squaring off the "
                               "tops is visible rather than only audible. Click SPECTRUM to see "
                               "the same signal by frequency, with the EQ drawn over it.")
                : juce::String("  The curve is the signal by frequency; the brighter line over it "
                               "is what the EQ is doing to it, so a band can be set against what "
                               "is actually there. Click SCOPE to go back to the waveform.");

    text += "  The bars are peak level, with the line above each one holding the loudest recent "
            "moment.";

    text += clipLit ? "  CLIP is lit: something reached full scale in the last twenty seconds. "
                      "Click it to clear it now."
                    : "  CLIP lights if anything reaches full scale, and stays lit for twenty "
                      "seconds after the last one - so it is always telling you about what you "
                      "are playing now.";

    return text;
}

int OutputScope::findTrigger() const
{
    if (captured < captureSamples)
        return juce::jmax(0, captured - scopeWindow);

    // Searched over the most recent part of the capture, so the picture is of
    // now rather than of a tenth of a second ago.
    const int searchFrom = captured - scopeWindow - scopeSearch;

    for (int i = juce::jmax(1, searchFrom); i < captured - scopeWindow; ++i)
        if (left[(size_t) (i - 1)] <= 0.0f && left[(size_t) i] > 0.0f)
            return i;

    return juce::jmax(0, captured - scopeWindow);
}

float OutputScope::toBarPosition(float magnitude)
{
    // Decibels, not amplitude: half of full scale is 6 dB down, and a bar that
    // put it halfway up would say the level was half when it is a sixth of the
    // way to being too loud.
    const float db = toDecibels(magnitude);
    return juce::jlimit(0.0f, 1.0f, (db - floorDb) / -floorDb);
}

void OutputScope::paintScope(juce::Graphics& g, juce::Rectangle<float> area)
{
    const float centreY = area.getCentreY();
    const float halfHeight = area.getHeight() * 0.5f;

    // Full scale, drawn as the ceiling it is. Without it the trace has no
    // stated limit and "how close am I" has no answer.
    g.setColour(ui::colours::panelEdge);
    g.drawHorizontalLine((int) area.getY(), area.getX(), area.getRight());
    g.drawHorizontalLine((int) area.getBottom(), area.getX(), area.getRight());

    g.setColour(ui::colours::flowLine.withAlpha(0.6f));
    g.drawHorizontalLine((int) centreY, area.getX(), area.getRight());

    if (captured <= 0)
    {
        g.setColour(ui::colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("silent", area.toNearestInt(), juce::Justification::centred, false);
        return;
    }

    const int start = findTrigger();
    const int available = juce::jmin(scopeWindow, captured - start);

    if (available <= 1)
        return;

    const int columns = juce::jmax(1, (int) area.getWidth());
    const float samplesPerColumn = (float) available / (float) columns;

    juce::Path trace;
    bool over = false;

    // Min and max per column rather than one sample per column: at these rates
    // a column covers several cycles, and picking one sample out of them draws
    // whatever alias the spacing happens to produce instead of the signal.
    for (int column = 0; column < columns; ++column)
    {
        const int from = start + (int) (column * samplesPerColumn);
        const int to =
            juce::jmin(start + available, start + (int) ((column + 1) * samplesPerColumn) + 1);

        float lowest = 1.0f;
        float highest = -1.0f;

        for (int i = from; i < to; ++i)
        {
            const float value = 0.5f * (left[(size_t) i] + right[(size_t) i]);
            lowest = juce::jmin(lowest, value);
            highest = juce::jmax(highest, value);

            if (std::abs(value) >= 1.0f)
                over = true;
        }

        if (lowest > highest)
            continue;

        const float x = area.getX() + (float) column;
        const float yTop = centreY - juce::jlimit(-1.0f, 1.0f, highest) * halfHeight;
        const float yBottom = centreY - juce::jlimit(-1.0f, 1.0f, lowest) * halfHeight;

        trace.addRectangle(x, yTop, 1.0f, juce::jmax(1.0f, yBottom - yTop));
    }

    // Coloured by what it is showing rather than always the same: a trace that
    // has reached the ceiling is the one thing here worth interrupting for.
    g.setColour(over ? juce::Colours::orangered : ui::colours::accent);
    g.fillPath(trace);
}

void OutputScope::showBusEq(bool shouldShowBusEq)
{
    if (busEq == shouldShowBusEq)
        return;

    busEq = shouldShowBusEq;
    repaint();
}

void OutputScope::paintEqCurve(juce::Graphics& g, juce::Rectangle<float> area)
{
    // Either EQ has the same three bands at the same frequencies - they are two
    // copies of one chain - so only where the gains come from differs.
    using B = project::BusFxControl;

    const float lowDb = busEq ? params.busFx[(size_t) B::eqLow].load() : params.eqLowGain.load();
    const float midDb = busEq ? params.busFx[(size_t) B::eqMid].load() : params.eqMidGain.load();
    const float highDb = busEq ? params.busFx[(size_t) B::eqHigh].load() : params.eqHighGain.load();

    const bool flat = std::abs(lowDb) < 0.05f && std::abs(midDb) < 0.05f && std::abs(highDb) < 0.05f;

    // Its own scale, centred, rather than sharing the spectrum's. They measure
    // different things - one is how loud the signal is, the other is what is
    // being done to it - and forcing both onto one axis would leave the EQ
    // curve either invisible or pinned to the ceiling.
    const float centreY = area.getCentreY();
    const float perDb = area.getHeight() * 0.5f / eqRangeDb;

    // The rate the engine is really running at, not an assumed one - a shelf
    // built at the wrong rate sits at the wrong frequency.
    const double rate = tap.getSampleRate();

    g.setColour(ui::colours::accentWarm.withAlpha(flat ? 0.18f : 0.35f));
    g.drawHorizontalLine((int) centreY, area.getX(), area.getRight());

    juce::Path curve;
    const int columns = juce::jmax(2, (int) area.getWidth());

    for (int column = 0; column < columns; ++column)
    {
        const float proportion = (float) column / (float) (columns - 1);
        const double frequency = frequencyAt(proportion);

        const float gain =
            MasterEffects::getEqualiserMagnitude(lowDb, midDb, highDb, frequency, rate);
        const float db = juce::jlimit(-eqRangeDb, eqRangeDb, toDecibels(gain));

        const float x = area.getX() + (float) column;
        const float y = centreY - db * perDb;

        if (column == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }

    // Dimmed when it is doing nothing, so a flat EQ does not look like a
    // setting and a shaped one is obviously deliberate.
    g.setColour(ui::colours::accentWarm.withAlpha(flat ? 0.25f : 1.0f));
    g.strokePath(curve, juce::PathStrokeType(flat ? 1.0f : 1.8f));
}

void OutputScope::paintSpectrum(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(ui::colours::panelEdge);
    g.drawHorizontalLine((int) area.getY(), area.getX(), area.getRight());
    g.drawHorizontalLine((int) area.getBottom(), area.getX(), area.getRight());

    // Decade lines, labelled, because a frequency plot with no marks on it says
    // "there is something up there" and not "there is something at 4 kHz".
    g.setFont(juce::Font(juce::FontOptions(8.0f)));
    for (double frequency : {100.0, 1000.0, 10000.0})
    {
        const float x = area.getX() + proportionOf(frequency) * area.getWidth();

        g.setColour(ui::colours::flowLine.withAlpha(0.5f));
        g.drawVerticalLine((int) x, area.getY(), area.getBottom());

        g.setColour(ui::colours::textDim.withAlpha(0.7f));
        g.drawText(frequency >= 1000.0 ? juce::String((int) (frequency / 1000.0)) + "k"
                                       : juce::String((int) frequency),
                   juce::Rectangle<float>(x + 2.0f, area.getBottom() - 10.0f, 22.0f, 9.0f)
                       .toNearestInt(),
                   juce::Justification::centredLeft, false);
    }

    if (captured >= SpectrumAnalyser::fftSize)
    {
        const int columns = juce::jmax(2, (int) area.getWidth());
        const float range = spectrumTopDb - spectrumBottomDb;

        juce::Path fill;
        fill.startNewSubPath(area.getX(), area.getBottom());

        for (int column = 0; column < columns; ++column)
        {
            // The band this pixel covers, not the frequency at its centre. At
            // the top of the range one pixel spans dozens of bins, and sampling
            // the middle one of them walks straight past the peak beside it.
            const double from = frequencyAt((float) column / (float) columns);
            const double to = frequencyAt((float) (column + 1) / (float) columns);

            const float db = juce::jlimit(spectrumBottomDb, spectrumTopDb,
                                          analyser.getDbInRange(from, to));

            const float x = area.getX() + (float) column;
            const float y = area.getBottom() - (db - spectrumBottomDb) / range * area.getHeight();

            fill.lineTo(x, y);
        }

        fill.lineTo(area.getRight(), area.getBottom());
        fill.closeSubPath();

        g.setColour(ui::colours::accent.withAlpha(0.30f));
        g.fillPath(fill);
        g.setColour(ui::colours::accent);
        g.strokePath(fill, juce::PathStrokeType(1.0f));
    }
    else
    {
        g.setColour(ui::colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText("silent", area.toNearestInt(), juce::Justification::centred, false);
    }

    // Last, so the EQ reads as something laid over the signal rather than as
    // another thing in it.
    paintEqCurve(g, area);
}

void OutputScope::paintMeter(juce::Graphics& g, juce::Rectangle<float> area)
{
    auto clipArea = area.removeFromTop(13.0f);
    area.removeFromTop(3.0f);
    auto readout = area.removeFromBottom(12.0f);
    area.removeFromBottom(2.0f);

    // ---- Clip ----
    g.setColour(clipLit ? juce::Colours::orangered : ui::colours::panelEdge);
    g.fillRoundedRectangle(clipArea, 3.0f);
    g.setColour(clipLit ? juce::Colours::white : ui::colours::textDim);
    g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    g.drawText("CLIP", clipArea.toNearestInt(), juce::Justification::centred, false);

    // ---- Bars ----
    const float gap = 3.0f;
    const float barWidth = (area.getWidth() - gap) * 0.5f;

    for (int channel = 0; channel < 2; ++channel)
    {
        auto bar = area.withWidth(barWidth).withX(area.getX() + channel * (barWidth + gap));

        g.setColour(ui::colours::background);
        g.fillRect(bar);

        const float filled = toBarPosition(displayedLevel[channel]) * bar.getHeight();

        if (filled > 0.0f)
        {
            auto lit = bar.withTop(bar.getBottom() - filled);

            // The top few decibels in a warmer colour, because the useful
            // question is not "how loud" but "how much is left".
            g.setColour(ui::colours::accent);
            g.fillRect(lit);

            const float headroomLine = bar.getBottom() - toBarPosition(0.71f) * bar.getHeight();
            if (lit.getY() < headroomLine)
            {
                g.setColour(ui::colours::accentWarm);
                g.fillRect(lit.withBottom(headroomLine));
            }
        }

        // Where it last got to, held still long enough to read.
        if (heldPeak[channel] > 0.0f)
        {
            const float y = bar.getBottom() - toBarPosition(heldPeak[channel]) * bar.getHeight();
            g.setColour(ui::colours::textPrimary);
            g.drawHorizontalLine((int) y, bar.getX(), bar.getRight());
        }

        g.setColour(ui::colours::panelEdge);
        g.drawRect(bar, 1.0f);
    }

    // ---- Number ----
    // A figure as well as a bar, because "is this patch louder than the last
    // one" is a question about a number and cannot be eyeballed off a bar.
    g.setColour(lastPeakDb > -0.1f ? juce::Colours::orangered : ui::colours::textDim);
    g.setFont(juce::Font(juce::FontOptions(9.5f)));
    g.drawText(lastPeakDb <= floorDb ? juce::String("-inf") : juce::String(lastPeakDb, 1) + " dB",
               readout.toNearestInt(), juce::Justification::centred, false);
}

void OutputScope::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(ui::colours::panel.withAlpha(0.55f));
    g.fillRoundedRectangle(bounds, 7.0f);
    g.setColour(ui::colours::panelEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 1.0f);

    auto content = bounds.reduced(8.0f);

    auto meterArea = content.removeFromRight((float) meterWidth);
    content.removeFromRight(8.0f);

    // Labelled OUT rather than SCOPE, and marked live, because the two views to
    // the left of it are offline renders that never move while you play - and
    // a panel where half the pictures are frozen reads as broken unless it says
    // which half is which.
    g.setColour(ui::colours::textDim);
    g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
    auto labelRow = content.removeFromTop(11.0f);
    g.drawText("OUT", labelRow.toNearestInt(), juce::Justification::centredLeft, false);

    g.setColour(tap.hasSignal() ? ui::colours::lcdText : ui::colours::flowLine);
    g.fillEllipse(labelRow.getX() + 26.0f, labelRow.getCentreY() - 2.5f, 5.0f, 5.0f);

    g.setColour(ui::colours::textDim);
    g.setFont(juce::Font(juce::FontOptions(8.5f)));
    g.drawText("LIVE", labelRow.withTrimmedLeft(36.0f).toNearestInt(),
               juce::Justification::centredLeft, false);

    // Which EQ the curve is, in the curve's own colour. There are two in the
    // project now, and an unnamed curve would be a guess about which dials it
    // answers to.
    if (view == View::spectrum)
    {
        g.setColour(ui::colours::accentWarm.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
        g.drawText(busEq ? "BUS EQ" : "SYNTH EQ", labelRow.withTrimmedLeft(66.0f).toNearestInt(),
                   juce::Justification::centredLeft, false);
    }

    // The switch, drawn as the name of the view you would get rather than of
    // the one you are in: a label that names where you already are has to be
    // read twice to work out which it means.
    auto toggle = viewToggleArea();
    g.setColour(ui::colours::accent.withAlpha(0.16f));
    g.fillRoundedRectangle(toggle.toFloat(), 3.0f);
    g.setColour(ui::colours::accent);
    g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
    g.drawText(view == View::scope ? "SPECTRUM >" : "< SCOPE", toggle,
               juce::Justification::centred, false);

    auto plot = content.withTrimmedTop(1.0f);

    if (view == View::scope)
        paintScope(g, plot);
    else
        paintSpectrum(g, plot);

    paintMeter(g, meterArea);
}
} // namespace wavelathe
