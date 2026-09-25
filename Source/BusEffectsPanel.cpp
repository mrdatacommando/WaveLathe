// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BusEffectsPanel.h"

#include <cmath>

namespace wavelathe
{
namespace
{
// The Synth page's formatters, repeated because they are one-liners and this
// panel should not reach into the editor for them. The ranges below are the
// Synth page's too: this is the same chain, and a Delay Time dial that turned
// differently here would be a second instrument pretending to be the first.
juce::String formatPercent(double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; }

juce::String formatHz(double v) { return juce::String(v, 2) + "Hz"; }

juce::String formatMs(double v) { return juce::String(juce::roundToInt(v)) + "m"; }

// "flat" at the centre, as the Synth page reads it - and here the value really
// is flat there, because the dial snaps it (see the EQ dials below).
constexpr double flatWithin = 0.15;

juce::String formatDecibels(double v)
{
    if (std::abs(v) < flatWithin)
        return "flat";

    return (v > 0.0 ? juce::String("+") : juce::String()) + ui::toFixed(v, std::abs(v) < 10.0 ? 1 : 0);
}

// A bus level reads in decibels with a real zero, as the mastering Trim does.
constexpr double unityWithin = 0.2;

juce::String formatGain(double v)
{
    if (std::abs(v) < unityWithin)
        return "0.0dB";

    return (v > 0.0 ? "+" : "") + ui::toFixed(v, std::abs(v) < 10.0 ? 1 : 0) + "dB";
}

// How far apart the chevrons of a chain overlap, so the point of one section
// sits in the notch of the next. The Synth page's figure.
constexpr int mesh = ui::FlowSection::chevronWidth;

// Room above the outline for its title, which sits ON the top edge.
constexpr int titleOverhang = 8;
} // namespace

BusEffectsPanel::BusEffectsPanel(WaveLatheProcessor& processor) : processorRef(processor)
{
    for (auto* section : {&distortionSection, &chorusSection, &phaserSection, &delaySection,
                          &reverbSection, &eqSection, &outputSection})
        addAndMakeVisible(*section);

    distortionSection.setStyle(ui::SectionStyle::chainStart, firstStage);
    chorusSection.setStyle(ui::SectionStyle::chainMiddle, firstStage + 1);
    phaserSection.setStyle(ui::SectionStyle::chainMiddle, firstStage + 2);
    delaySection.setStyle(ui::SectionStyle::chainMiddle, firstStage + 3);
    reverbSection.setStyle(ui::SectionStyle::chainMiddle, firstStage + 4);
    eqSection.setStyle(ui::SectionStyle::chainMiddle, firstStage + 5);
    outputSection.setStyle(ui::SectionStyle::chainEnd, firstStage + 6);

    buildKnobs();

    addAndMakeVisible(delaySyncButton);
    delaySyncButton.onClick = [this]
    {
        const bool on = delaySyncButton.getToggleState();

        processorRef.recordUndoPoint(project::busFxControlName(Control::delaySync));
        processorRef.getParameters().busFx[(size_t) Control::delaySync].store(on ? 1.0f : 0.0f);

        if (onMessage)
            onMessage(on ? "Bus delay synced to tempo." : "Bus delay running free.");

        if (onChanged)
            onChanged();
    };

    refresh();
}

void BusEffectsPanel::buildKnobs()
{
    auto& busFx = processorRef.getParameters().busFx;

    // `snap` is where a dial with a meaningful centre - the EQ bands, the gain -
    // puts the value to exactly that centre. A dial has 128 positions and a
    // symmetric range has no middle one, so without it "flat" would be a tenth
    // of a decibel of EQ, and the chain skips the EQ only when it is truly flat.
    auto make = [this, &busFx](Control control, const juce::String& name, ui::IconType icon,
                               juce::NormalisableRange<double> range,
                               std::function<juce::String(double)> formatter,
                               std::function<double(double)> snap = nullptr)
    {
        auto& target = knobs[(size_t) control];
        const auto initial = (double) busFx[(size_t) control].load();

        target = std::make_unique<ui::ParameterKnob>(name, icon, range, initial, std::move(formatter));

        target->onValueChanged = [this, control, snap, &target, &busFx](double realValue, juce::String text)
        {
            const auto value = snap != nullptr ? snap(realValue) : realValue;
            busFx[(size_t) control].store((float) value);

            if (onTransient)
                onTransient(juce::String(project::busFxControlName(control)).toUpperCase() + ": " + text
                            + "   [" + juce::String(target->getMidiStep()) + "/127]");

            if (onChanged)
                onChanged();
        };

        // Undoable like every other dial. The undo snapshot is the whole
        // project, and the bus is in it.
        target->onGestureStart = [this, control]
        {
            processorRef.recordUndoPoint(project::busFxControlName(control));
        };

        addAndMakeVisible(*target);
    };

    const juce::NormalisableRange<double> unit{0.0, 1.0};
    const juce::NormalisableRange<double> rate{0.05, 8.0, 0.0, 0.4};
    const juce::NormalisableRange<double> band{-12.0, 12.0};

    const auto snapFlat = [](double v) { return std::abs(v) < flatWithin ? 0.0 : v; };
    const auto snapUnity = [](double v) { return std::abs(v) < unityWithin ? 0.0 : v; };

    make(Control::distortion, "Amount", ui::IconType::distortion, unit, formatPercent);

    make(Control::chorusRate, "Rate", ui::IconType::lfoRate, rate, formatHz);
    make(Control::chorusDepth, "Depth", ui::IconType::lfoToAmp, unit, formatPercent);
    make(Control::chorusMix, "Mix", ui::IconType::delayMix, unit, formatPercent);

    make(Control::phaserRate, "Rate", ui::IconType::lfoRate, rate, formatHz);
    make(Control::phaserFeedback, "Feedback", ui::IconType::delayFeedback, {0.0, 0.9}, formatPercent);
    make(Control::phaserMix, "Mix", ui::IconType::delayMix, unit, formatPercent);

    make(Control::delayTime, "Time", ui::IconType::delayTime, {10.0, 1500.0, 0.0, 0.4}, formatMs);
    make(Control::delayFeedback, "Feedback", ui::IconType::delayFeedback, {0.0, 0.95}, formatPercent);
    make(Control::delayMix, "Mix", ui::IconType::delayMix, unit, formatPercent);

    make(Control::reverbSize, "Size", ui::IconType::reverbSize, unit, formatPercent);
    make(Control::reverbMix, "Mix", ui::IconType::reverbMix, unit, formatPercent);

    make(Control::eqLow, "Low", ui::IconType::cutoff, band, formatDecibels, snapFlat);
    make(Control::eqMid, "Mid", ui::IconType::resonance, band, formatDecibels, snapFlat);
    make(Control::eqHigh, "High", ui::IconType::modEnvToCutoff, band, formatDecibels, snapFlat);

    make(Control::outputGain, "Gain", ui::IconType::gain, {-24.0, 24.0}, formatGain, snapUnity);
}

void BusEffectsPanel::refresh()
{
    const auto& busFx = processorRef.getParameters().busFx;

    for (int i = 0; i < project::numBusFxControls; ++i)
        if (auto* knob = knobs[(size_t) i].get())
            knob->setRealValue((double) busFx[(size_t) i].load());

    delaySyncButton.setToggleState(busFx[(size_t) Control::delaySync].load() > 0.5f,
                                   juce::dontSendNotification);
}

void BusEffectsPanel::paint(juce::Graphics& g)
{
    g.fillAll(ui::colours::background);

    if (groupArea.isEmpty())
        return;

    // The Synth page's group outline, drawn the same way, so the two copies of
    // the chain are framed alike. Warm, as "Master effects" is there.
    const auto tint = ui::colours::accentWarm;
    const auto r = groupArea.toFloat();

    g.setColour(tint.withAlpha(0.05f));
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(tint.withAlpha(0.35f));
    g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.2f);

    // Named for where it sits and what it hears, since "effects" alone would
    // be the Synth page's name for a different chain.
    const juce::String title = "BUS EFFECTS - SYNTH AND DRUMS, BEFORE MASTERING";

    g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    const auto textWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), title) + 12.0f;
    const juce::Rectangle<float> label(r.getX() + 14.0f, r.getY() - 7.0f, textWidth, 14.0f);

    g.setColour(ui::colours::background);
    g.fillRect(label);
    g.setColour(tint.withAlpha(0.9f));
    g.drawText(title, label, juce::Justification::centred);
}

void BusEffectsPanel::layoutSection(ui::FlowSection& section, juce::Rectangle<int> area,
                                    std::initializer_list<Control> controls)
{
    section.setBounds(area);

    auto inner = area.reduced(8, 0).withTrimmedTop(18).withTrimmedBottom(6);
    if (controls.size() == 0)
        return;

    const int knobWidth = inner.getWidth() / (int) controls.size();

    for (const auto control : controls)
        if (auto* knob = knobFor(control))
            knob->setBounds(inner.removeFromLeft(knobWidth).reduced(2, 0));
}

void BusEffectsPanel::resized()
{
    auto area = getLocalBounds().reduced(10, 0);
    area.removeFromTop(titleOverhang);

    groupArea = area;

    auto row = area.reduced(8, 0).withTrimmedTop(16).withTrimmedBottom(8);

    // One chain across the page, sized by how many dials each stage carries,
    // so a one-dial Distortion is not given the width of a three-dial Chorus.
    // The Synth page stacks the same seven in three rows because it has half
    // the width; here there is room for the chain to read left to right.
    const std::vector<int> weights{1, 3, 3, 3, 2, 3, 1};

    int totalWeight = 0;
    for (const auto w : weights)
        totalWeight += w;

    const int span = row.getWidth() + mesh * ((int) weights.size() - 1);

    std::vector<juce::Rectangle<int>> areas;
    int x = row.getX();

    for (size_t i = 0; i < weights.size(); ++i)
    {
        const int width = i + 1 == weights.size() ? row.getRight() - x : span * weights[i] / totalWeight;
        areas.push_back({x, row.getY(), width, row.getHeight()});
        x += width - mesh;
    }

    layoutSection(distortionSection, areas[0], {Control::distortion});
    layoutSection(chorusSection, areas[1], {Control::chorusRate, Control::chorusDepth, Control::chorusMix});
    layoutSection(phaserSection, areas[2],
                  {Control::phaserRate, Control::phaserFeedback, Control::phaserMix});
    layoutSection(delaySection, areas[3], {Control::delayTime, Control::delayFeedback, Control::delayMix});
    layoutSection(reverbSection, areas[4], {Control::reverbSize, Control::reverbMix});
    layoutSection(eqSection, areas[5], {Control::eqLow, Control::eqMid, Control::eqHigh});
    layoutSection(outputSection, areas[6], {Control::outputGain});

    // On the Delay section's title row, where the Synth page puts its own.
    delaySyncButton.setBounds(areas[3].getRight() - 62 - mesh, areas[3].getY() + 1, 58, 15);
}
} // namespace wavelathe
