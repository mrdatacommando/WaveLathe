// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MasteringPanel.h"

#include <cmath>

namespace wavelathe
{
namespace
{
// The meters run to this much reduction and no further. Twenty decibels is more
// than a mix bus should ever be asked for; a scale that ran to sixty would
// spend most of its length showing nothing happening.
constexpr float meterFullScaleDb = 20.0f;

juce::String formatPercent(double v) { return juce::String((int) std::round(v * 100.0)) + "%"; }

juce::String formatDecibels(double v)
{
    return (v > 0.0 ? "+" : "") + ui::toFixed(v, std::abs(v) < 10.0 ? 1 : 0) + "dB";
}

juce::String formatRatio(double v)
{
    if (v <= 1.001)
        return "off";
    return juce::String(v, 1) + ":1";
}

juce::String formatMilliseconds(double v)
{
    if (v < 10.0)
        return juce::String(v, 1) + "ms";
    return juce::String((int) std::round(v)) + "ms";
}
} // namespace

MasteringPanel::MasteringPanel(mastering::Controls& controlsToDrive) : controls(controlsToDrive)
{
    addAndMakeVisible(enableButton);
    enableButton.onClick = [this]
    {
        const bool on = enableButton.getToggleState();
        controls.set(mastering::Control::enabled, on ? 1.0f : 0.0f);

        if (onMessage)
            onMessage(on ? "Mastering on - saturation, compressor and limiter over the whole mix."
                         : "Mastering off - the mix passes through untouched.");

        // The note beside this button is painted only while the stage is off,
        // and a toggle button repaints itself rather than the panel behind it.
        // Without this the explanation stayed on screen contradicting the very
        // switch it sits next to.
        repaint();

        if (onChanged)
            onChanged();
    };

    for (auto* section : {&saturationSection, &compressorSection, &trimSection, &limiterSection})
        addAndMakeVisible(*section);

    setFirstStage(1);

    buildKnobs();
    refresh();

    // Thirty a second, the same as the other meters in this program. Fast
    // enough that a gain reduction meter reads as movement rather than as a
    // series of positions.
    startTimerHz(30);
}

MasteringPanel::~MasteringPanel() { stopTimer(); }

void MasteringPanel::setFirstStage(int first)
{
    saturationSection.setStyle(ui::SectionStyle::chainStart, first);
    compressorSection.setStyle(ui::SectionStyle::chainMiddle, first + 1);
    trimSection.setStyle(ui::SectionStyle::chainMiddle, first + 2);
    limiterSection.setStyle(ui::SectionStyle::chainEnd, first + 3);
}

void MasteringPanel::buildKnobs()
{
    using C = mastering::Control;

    auto make = [this](std::unique_ptr<ui::ParameterKnob>& target, const juce::String& name,
                       ui::IconType icon, juce::NormalisableRange<double> range, double initial,
                       std::function<juce::String(double)> formatter,
                       std::function<void(double)> apply)
    {
        target = std::make_unique<ui::ParameterKnob>(name, icon, range, initial, std::move(formatter));
        target->onValueChanged = [this, apply, &target](double realValue, juce::String text)
        {
            apply(realValue);
            if (onMessage)
                onMessage(target->getName().toUpperCase() + ": " + text);
            if (onChanged)
                onChanged();
        };
        addAndMakeVisible(*target);
    };

    const juce::NormalisableRange<double> unit{0.0, 1.0};

    make(driveKnob, "Drive", ui::IconType::drive, unit, controls.get(C::satDrive), formatPercent,
         [this](double v) { controls.set(C::satDrive, (float) v); });
    make(satMixKnob, "Sat Mix", ui::IconType::delayMix, unit, controls.get(C::satMix), formatPercent,
         [this](double v) { controls.set(C::satMix, (float) v); });

    make(thresholdKnob, "Thresh", ui::IconType::gain, {-48.0, 0.0}, controls.get(C::compThreshold),
         formatDecibels, [this](double v) { controls.set(C::compThreshold, (float) v); });
    make(ratioKnob, "Ratio", ui::IconType::resonance, {1.0, 20.0, 0.0, 0.5},
         controls.get(C::compRatio), formatRatio,
         [this](double v) { controls.set(C::compRatio, (float) v); });

    // Both times are skewed hard towards the short end, because that is where
    // every useful setting lives and a linear dial would spend three quarters
    // of its travel between values nobody picks.
    make(attackKnob, "Attack", ui::IconType::attack, {0.1, 100.0, 0.0, 0.3},
         controls.get(C::compAttack), formatMilliseconds,
         [this](double v) { controls.set(C::compAttack, (float) v); });
    make(releaseKnob, "Release", ui::IconType::release, {10.0, 1000.0, 0.0, 0.35},
         controls.get(C::compRelease), formatMilliseconds,
         [this](double v) { controls.set(C::compRelease, (float) v); });

    make(makeupKnob, "Makeup", ui::IconType::gain, {0.0, 24.0}, controls.get(C::compMakeup),
         formatDecibels, [this](double v) { controls.set(C::compMakeup, (float) v); });
    make(compMixKnob, "Mix", ui::IconType::delayMix, unit, controls.get(C::compMix), formatPercent,
         [this](double v) { controls.set(C::compMix, (float) v); });

    // Snapped to exactly unity near the centre, which none of the other dials
    // here need. A dial has 128 positions and a symmetric range has no middle
    // one, so the centre detent lands about a tenth of a decibel off zero. That
    // is inaudible everywhere except here, where "trim at centre" is the
    // setting somebody chooses in order to change nothing at all.
    make(trimKnob, "Trim", ui::IconType::gain, {-24.0, 24.0}, controls.get(C::trim),
         [](double v) { return std::abs(v) < 0.2 ? juce::String("0.0dB") : formatDecibels(v); },
         [this](double v) { controls.set(C::trim, (float) (std::abs(v) < 0.2 ? 0.0 : v)); });

    make(ceilingKnob, "Ceiling", ui::IconType::gain, {-12.0, 0.0}, controls.get(C::limitCeiling),
         formatDecibels, [this](double v) { controls.set(C::limitCeiling, (float) v); });
    make(limitReleaseKnob, "Release", ui::IconType::release, {1.0, 500.0, 0.0, 0.35},
         controls.get(C::limitRelease), formatMilliseconds,
         [this](double v) { controls.set(C::limitRelease, (float) v); });
}

void MasteringPanel::refresh()
{
    using C = mastering::Control;

    enableButton.setToggleState(controls.get(C::enabled) > 0.5f, juce::dontSendNotification);

    driveKnob->setRealValue(controls.get(C::satDrive));
    satMixKnob->setRealValue(controls.get(C::satMix));
    thresholdKnob->setRealValue(controls.get(C::compThreshold));
    ratioKnob->setRealValue(controls.get(C::compRatio));
    attackKnob->setRealValue(controls.get(C::compAttack));
    releaseKnob->setRealValue(controls.get(C::compRelease));
    makeupKnob->setRealValue(controls.get(C::compMakeup));
    compMixKnob->setRealValue(controls.get(C::compMix));
    trimKnob->setRealValue(controls.get(C::trim));
    ceilingKnob->setRealValue(controls.get(C::limitCeiling));
    limitReleaseKnob->setRealValue(controls.get(C::limitRelease));
}

void MasteringPanel::timerCallback()
{
    const float compressor = controls.compressorReductionDb();
    const float limiter = controls.limiterReductionDb();

    // Up at once, down slowly. A meter that followed the release exactly would
    // flicker too fast to read; one that fell as slowly as it rose would never
    // show a gap between two separate things happening.
    auto follow = [](float shown, float value)
    {
        return value > shown ? value : shown + (value - shown) * 0.25f;
    };

    const float newCompressor = follow(shownCompressorDb, compressor);
    const float newLimiter = follow(shownLimiterDb, limiter);

    if (std::abs(newCompressor - shownCompressorDb) > 0.01f
        || std::abs(newLimiter - shownLimiterDb) > 0.01f)
    {
        shownCompressorDb = newCompressor;
        shownLimiterDb = newLimiter;
        repaint(compressorMeterArea.getUnion(limiterMeterArea));
    }
}

void MasteringPanel::paintMeter(juce::Graphics& g, juce::Rectangle<int> area, float reductionDb,
                                float fullScaleDb, const juce::String& label) const
{
    if (area.isEmpty())
        return;

    auto bounds = area.toFloat();

    auto labelArea = bounds.removeFromBottom(16.0f);

    g.setColour(ui::colours::knobTrack);
    g.fillRoundedRectangle(bounds, 3.0f);

    const float proportion = juce::jlimit(0.0f, 1.0f, reductionDb / fullScaleDb);
    if (proportion > 0.0f)
    {
        auto filled = bounds.withHeight(bounds.getHeight() * proportion);
        g.setColour(reductionDb > fullScaleDb * 0.75f ? juce::Colours::orangered
                                                      : ui::colours::accentWarm);
        g.fillRoundedRectangle(filled, 3.0f);
    }

    g.setColour(ui::colours::panelEdge);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    g.setColour(ui::colours::textDim);
    g.setFont(11.0f);
    g.drawText(label, labelArea, juce::Justification::centred);

    g.setColour(ui::colours::textPrimary);
    g.setFont(12.0f);
    g.drawText(reductionDb < 0.05f ? "0.0" : juce::String(reductionDb, 1),
               bounds.reduced(2.0f), juce::Justification::centredTop);
}

void MasteringPanel::paint(juce::Graphics& g)
{
    g.fillAll(ui::colours::background);

    paintMeter(g, compressorMeterArea, shownCompressorDb, meterFullScaleDb, "COMP GR");
    paintMeter(g, limiterMeterArea, shownLimiterDb, meterFullScaleDb, "LIMIT GR");

    // Said on the page rather than left to be discovered, because it is the
    // first question somebody has when a stage is switched off by default and
    // the second question when they wonder why the plugin reports latency.
    if (!enableButton.getToggleState())
    {
        g.setColour(ui::colours::textDim);
        g.setFont(12.0f);
        g.drawText("Off by default, so older patches sound as they always did.",
                   getLocalBounds().removeFromTop(38).withTrimmedLeft(180),
                   juce::Justification::centredLeft);
    }
}

void MasteringPanel::layoutSection(ui::FlowSection& section, juce::Rectangle<int> area,
                                   const std::vector<ui::ParameterKnob*>& knobs)
{
    section.setBounds(area);

    auto inner = area.reduced(8, 0).withTrimmedTop(18).withTrimmedBottom(6);
    if (knobs.empty())
        return;

    const int knobWidth = inner.getWidth() / (int) knobs.size();
    for (auto* knob : knobs)
        knob->setBounds(inner.removeFromLeft(knobWidth).reduced(2, 0));
}

void MasteringPanel::resized()
{
    auto area = getLocalBounds().reduced(10);

    auto header = area.removeFromTop(34);
    enableButton.setBounds(header.removeFromLeft(160));

    area.removeFromTop(8);

    // The chain is one row and the page is much taller than it needs to be, so
    // the row is given real height and then sat a little above centre. Pinned
    // to the top it left a void underneath that read as unfinished; dead centre
    // it floats away from the tabs it belongs to. A third of the slack above
    // and two thirds below is where it stops looking like either.
    const int rowHeight = juce::jmin(300, area.getHeight());
    area.removeFromTop((area.getHeight() - rowHeight) / 3);
    auto row = area.removeFromTop(rowHeight);

    // The meters take a column of their own down the right, beside the chain
    // rather than inside any one stage of it: they describe what two separate
    // stages are doing and belong to neither. Full height, because a gain
    // reduction meter is read as a distance and a short one has nowhere to say
    // the difference between a little and a lot.
    auto meterColumn = row.removeFromRight(130);
    row.removeFromRight(12);
    compressorMeterArea = meterColumn.removeFromLeft(meterColumn.getWidth() / 2).reduced(10, 4);
    limiterMeterArea = meterColumn.reduced(10, 4);

    // Four stages, sized by how many dials each one carries, so a six-dial
    // compressor is not squeezed into the same width as a one-dial trim.
    const int totalKnobs = 2 + 6 + 1 + 2;
    const int usable = row.getWidth();

    auto widthFor = [&](int knobCount)
    {
        return (usable * knobCount) / totalKnobs;
    };

    layoutSection(saturationSection, row.removeFromLeft(widthFor(2)),
                  {driveKnob.get(), satMixKnob.get()});
    layoutSection(compressorSection, row.removeFromLeft(widthFor(6)),
                  {thresholdKnob.get(), ratioKnob.get(), attackKnob.get(), releaseKnob.get(),
                   makeupKnob.get(), compMixKnob.get()});
    layoutSection(trimSection, row.removeFromLeft(widthFor(1)), {trimKnob.get()});
    layoutSection(limiterSection, row, {ceilingKnob.get(), limitReleaseKnob.get()});
}
} // namespace wavelathe
