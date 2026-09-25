// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "UiComponents.h"

namespace wavelathe
{
namespace ui
{
namespace
{
juce::Path makeWavePath(juce::Rectangle<float> r, int cycles, float amplitudeScale)
{
    juce::Path p;
    int steps = 48;
    for (int i = 0; i <= steps; ++i)
    {
        float t = (float) i / steps;
        float x = r.getX() + t * r.getWidth();
        float y = r.getCentreY()
                  - std::sin(t * juce::MathConstants<float>::twoPi * cycles) * r.getHeight() * 0.5f * amplitudeScale;
        if (i == 0)
            p.startNewSubPath(x, y);
        else
            p.lineTo(x, y);
    }
    return p;
}
} // namespace

void drawParameterIcon(juce::Graphics& g, IconType icon, juce::Rectangle<float> area, juce::Colour colour)
{
    g.setColour(colour);
    auto r = area.reduced(1.0f);
    float thickness = juce::jmax(1.0f, r.getHeight() * 0.09f);

    switch (icon)
    {
        case IconType::wave:
            g.strokePath(makeWavePath(r, 1, 0.9f), juce::PathStrokeType(thickness));
            break;

        case IconType::voices:
        {
            // Several stacked waves = several unison voices.
            for (int i = 0; i < 3; ++i)
            {
                auto slice = r.withHeight(r.getHeight() / 3.0f).translated(0.0f, i * r.getHeight() / 3.0f);
                g.strokePath(makeWavePath(slice, 1, 0.75f), juce::PathStrokeType(thickness * 0.8f));
            }
            break;
        }

        case IconType::detune:
        {
            // Two waves slipping out of phase.
            g.strokePath(makeWavePath(r.translated(0.0f, -r.getHeight() * 0.12f), 1, 0.7f),
                         juce::PathStrokeType(thickness * 0.9f));
            g.setColour(colour.withAlpha(0.55f));
            g.strokePath(makeWavePath(r.translated(r.getWidth() * 0.10f, r.getHeight() * 0.12f), 1, 0.7f),
                         juce::PathStrokeType(thickness * 0.9f));
            break;
        }

        case IconType::width:
        {
            // Left/right arrows for stereo spread.
            float cy = r.getCentreY();
            g.drawLine(r.getX(), cy, r.getRight(), cy, thickness);
            juce::Path arrows;
            float a = r.getHeight() * 0.28f;
            arrows.startNewSubPath(r.getX() + a, cy - a);
            arrows.lineTo(r.getX(), cy);
            arrows.lineTo(r.getX() + a, cy + a);
            arrows.startNewSubPath(r.getRight() - a, cy - a);
            arrows.lineTo(r.getRight(), cy);
            arrows.lineTo(r.getRight() - a, cy + a);
            g.strokePath(arrows, juce::PathStrokeType(thickness));
            break;
        }

        case IconType::cutoff:
        {
            // Low-pass response: flat then rolling off.
            juce::Path p;
            p.startNewSubPath(r.getX(), r.getY() + r.getHeight() * 0.30f);
            p.lineTo(r.getX() + r.getWidth() * 0.52f, r.getY() + r.getHeight() * 0.30f);
            p.quadraticTo(r.getX() + r.getWidth() * 0.72f, r.getY() + r.getHeight() * 0.32f, r.getRight(),
                          r.getBottom());
            g.strokePath(p, juce::PathStrokeType(thickness));
            break;
        }

        case IconType::resonance:
        {
            // Same response with a peak at the corner frequency.
            juce::Path p;
            p.startNewSubPath(r.getX(), r.getY() + r.getHeight() * 0.42f);
            p.lineTo(r.getX() + r.getWidth() * 0.42f, r.getY() + r.getHeight() * 0.42f);
            p.quadraticTo(r.getX() + r.getWidth() * 0.58f, r.getY(), r.getX() + r.getWidth() * 0.68f,
                          r.getY() + r.getHeight() * 0.38f);
            p.quadraticTo(r.getX() + r.getWidth() * 0.82f, r.getY() + r.getHeight() * 0.75f, r.getRight(),
                          r.getBottom());
            g.strokePath(p, juce::PathStrokeType(thickness));
            break;
        }

        case IconType::drive:
        {
            // A wave squared off at the rails.
            juce::Path p;
            float top = r.getY() + r.getHeight() * 0.22f;
            float bottom = r.getBottom() - r.getHeight() * 0.22f;
            p.startNewSubPath(r.getX(), r.getCentreY());
            p.lineTo(r.getX() + r.getWidth() * 0.12f, top);
            p.lineTo(r.getX() + r.getWidth() * 0.38f, top);
            p.lineTo(r.getX() + r.getWidth() * 0.5f, r.getCentreY());
            p.lineTo(r.getX() + r.getWidth() * 0.62f, bottom);
            p.lineTo(r.getX() + r.getWidth() * 0.88f, bottom);
            p.lineTo(r.getRight(), r.getCentreY());
            g.strokePath(p, juce::PathStrokeType(thickness));
            break;
        }

        case IconType::attack:
        {
            juce::Path p;
            p.startNewSubPath(r.getX(), r.getBottom());
            p.lineTo(r.getX() + r.getWidth() * 0.65f, r.getY());
            p.lineTo(r.getRight(), r.getY());
            g.strokePath(p, juce::PathStrokeType(thickness));
            break;
        }

        case IconType::decay:
        {
            juce::Path p;
            p.startNewSubPath(r.getX(), r.getY());
            p.lineTo(r.getX() + r.getWidth() * 0.18f, r.getY());
            p.quadraticTo(r.getX() + r.getWidth() * 0.5f, r.getY() + r.getHeight() * 0.15f, r.getRight(),
                          r.getY() + r.getHeight() * 0.62f);
            g.strokePath(p, juce::PathStrokeType(thickness));
            break;
        }

        case IconType::sustain:
        {
            juce::Path p;
            p.startNewSubPath(r.getX(), r.getBottom());
            p.lineTo(r.getX() + r.getWidth() * 0.22f, r.getY() + r.getHeight() * 0.35f);
            p.lineTo(r.getRight(), r.getY() + r.getHeight() * 0.35f);
            g.strokePath(p, juce::PathStrokeType(thickness));
            break;
        }

        case IconType::release:
        {
            juce::Path p;
            p.startNewSubPath(r.getX(), r.getY() + r.getHeight() * 0.3f);
            p.lineTo(r.getX() + r.getWidth() * 0.32f, r.getY() + r.getHeight() * 0.3f);
            p.quadraticTo(r.getX() + r.getWidth() * 0.62f, r.getY() + r.getHeight() * 0.45f, r.getRight(),
                          r.getBottom());
            g.strokePath(p, juce::PathStrokeType(thickness));
            break;
        }

        case IconType::lfoRate:
            g.strokePath(makeWavePath(r, 2, 0.85f), juce::PathStrokeType(thickness));
            break;

        case IconType::lfoToFilter:
        {
            g.strokePath(makeWavePath(r.withHeight(r.getHeight() * 0.55f), 2, 0.8f),
                         juce::PathStrokeType(thickness * 0.85f));
            juce::Path p;
            auto lower = r.withTrimmedTop(r.getHeight() * 0.6f);
            p.startNewSubPath(lower.getX(), lower.getY());
            p.lineTo(lower.getX() + lower.getWidth() * 0.5f, lower.getY());
            p.quadraticTo(lower.getX() + lower.getWidth() * 0.75f, lower.getY(), lower.getRight(), lower.getBottom());
            g.strokePath(p, juce::PathStrokeType(thickness * 0.85f));
            break;
        }

        case IconType::lfoToAmp:
        {
            g.strokePath(makeWavePath(r.withHeight(r.getHeight() * 0.55f), 2, 0.8f),
                         juce::PathStrokeType(thickness * 0.85f));
            auto lower = r.withTrimmedTop(r.getHeight() * 0.62f);
            juce::Path spk;
            spk.startNewSubPath(lower.getX(), lower.getCentreY() - lower.getHeight() * 0.3f);
            spk.lineTo(lower.getX() + lower.getWidth() * 0.28f, lower.getCentreY() - lower.getHeight() * 0.3f);
            spk.lineTo(lower.getX() + lower.getWidth() * 0.52f, lower.getY());
            spk.lineTo(lower.getX() + lower.getWidth() * 0.52f, lower.getBottom());
            spk.lineTo(lower.getX() + lower.getWidth() * 0.28f, lower.getCentreY() + lower.getHeight() * 0.3f);
            spk.lineTo(lower.getX(), lower.getCentreY() + lower.getHeight() * 0.3f);
            spk.closeSubPath();
            g.strokePath(spk, juce::PathStrokeType(thickness * 0.8f));
            break;
        }

        case IconType::gain:
        {
            // Rising level bars.
            int bars = 4;
            float barWidth = r.getWidth() / (bars * 1.8f);
            for (int i = 0; i < bars; ++i)
            {
                float h = r.getHeight() * (0.25f + 0.25f * i);
                float x = r.getX() + i * barWidth * 1.8f;
                g.fillRect(juce::Rectangle<float>(x, r.getBottom() - h, barWidth, h));
            }
            break;
        }

        case IconType::distortion:
        {
            // A wave breaking up into jagged edges.
            juce::Path p;
            int steps = 24;
            for (int i = 0; i <= steps; ++i)
            {
                float t = (float) i / steps;
                float x = r.getX() + t * r.getWidth();
                float base = std::sin(t * juce::MathConstants<float>::twoPi);
                float clipped = juce::jlimit(-0.6f, 0.6f, base * 1.8f);
                float y = r.getCentreY() - clipped * r.getHeight() * 0.75f;
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            g.strokePath(p, juce::PathStrokeType(thickness));
            break;
        }

        case IconType::delayTime:
        {
            // Repeats spaced further apart.
            float cy = r.getCentreY();
            for (int i = 0; i < 3; ++i)
            {
                float x = r.getX() + r.getWidth() * (0.08f + i * 0.34f);
                float h = r.getHeight() * (0.85f - i * 0.2f);
                g.fillRect(juce::Rectangle<float>(x, cy - h * 0.5f, thickness * 1.3f, h));
            }
            break;
        }

        case IconType::delayFeedback:
        {
            // A loop arrow: signal fed back round again.
            auto square = r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight()),
                                                   juce::jmin(r.getWidth(), r.getHeight()));
            juce::Path p;
            p.addCentredArc(square.getCentreX(), square.getCentreY(), square.getWidth() * 0.36f,
                            square.getHeight() * 0.36f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.2f, true);
            g.strokePath(p, juce::PathStrokeType(thickness));
            juce::Path head;
            float ax = square.getCentreX() + square.getWidth() * 0.36f * std::sin(0.6f);
            float ay = square.getCentreY() - square.getHeight() * 0.36f * std::cos(0.6f);
            head.addTriangle(ax, ay - 3.0f, ax, ay + 3.0f, ax + 4.5f, ay);
            g.fillPath(head);
            break;
        }

        case IconType::delayMix:
        {
            // Dry hit plus decaying echoes.
            float cy = r.getCentreY();
            g.fillRect(juce::Rectangle<float>(r.getX(), cy - r.getHeight() * 0.42f, thickness * 1.4f,
                                               r.getHeight() * 0.84f));
            for (int i = 1; i <= 3; ++i)
            {
                float x = r.getX() + r.getWidth() * (0.24f * i + 0.06f);
                float h = r.getHeight() * 0.84f * std::pow(0.55f, (float) i);
                g.setColour(colour.withAlpha(1.0f - 0.22f * i));
                g.fillRect(juce::Rectangle<float>(x, cy - h * 0.5f, thickness * 1.2f, h));
            }
            break;
        }

        case IconType::reverbSize:
        {
            // Expanding room outlines.
            for (int i = 0; i < 3; ++i)
            {
                float inset = r.getWidth() * 0.16f * (2 - i);
                g.setColour(colour.withAlpha(0.35f + 0.3f * i));
                g.drawRoundedRectangle(r.reduced(inset, inset * r.getHeight() / r.getWidth()), 2.0f,
                                        thickness * 0.8f);
            }
            break;
        }

        case IconType::pitch:
        {
            // Up/down arrows: transposition.
            float cx = r.getCentreX();
            g.drawLine(cx, r.getY(), cx, r.getBottom(), thickness);
            juce::Path heads;
            float a = r.getWidth() * 0.22f;
            heads.startNewSubPath(cx - a, r.getY() + a);
            heads.lineTo(cx, r.getY());
            heads.lineTo(cx + a, r.getY() + a);
            heads.startNewSubPath(cx - a, r.getBottom() - a);
            heads.lineTo(cx, r.getBottom());
            heads.lineTo(cx + a, r.getBottom() - a);
            g.strokePath(heads, juce::PathStrokeType(thickness));
            break;
        }

        case IconType::subOsc:
        {
            // A big slow wave under a small fast one: an octave below.
            g.setColour(colour.withAlpha(0.5f));
            g.strokePath(makeWavePath(r.withHeight(r.getHeight() * 0.45f), 2, 0.7f),
                         juce::PathStrokeType(thickness * 0.8f));
            g.setColour(colour);
            g.strokePath(makeWavePath(r.withTrimmedTop(r.getHeight() * 0.42f), 1, 0.95f),
                         juce::PathStrokeType(thickness * 1.1f));
            break;
        }

        case IconType::noise:
        {
            // Irregular spikes.
            juce::Path p;
            juce::Random rng(20260901);
            int steps = 26;
            for (int i = 0; i <= steps; ++i)
            {
                float x = r.getX() + (float) i / steps * r.getWidth();
                float y = r.getCentreY() + (rng.nextFloat() * 2.0f - 1.0f) * r.getHeight() * 0.45f;
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            g.strokePath(p, juce::PathStrokeType(thickness * 0.85f));
            break;
        }

        case IconType::filterType:
        {
            // Overlaid low-pass and high-pass slopes: a choice of shapes.
            juce::Path lp;
            lp.startNewSubPath(r.getX(), r.getY() + r.getHeight() * 0.3f);
            lp.lineTo(r.getCentreX(), r.getY() + r.getHeight() * 0.3f);
            lp.quadraticTo(r.getCentreX() + r.getWidth() * 0.2f, r.getY() + r.getHeight() * 0.32f, r.getRight(),
                           r.getBottom());
            g.strokePath(lp, juce::PathStrokeType(thickness * 0.9f));

            g.setColour(colour.withAlpha(0.5f));
            juce::Path hp;
            hp.startNewSubPath(r.getX(), r.getBottom());
            hp.quadraticTo(r.getCentreX() - r.getWidth() * 0.2f, r.getY() + r.getHeight() * 0.32f, r.getCentreX(),
                           r.getY() + r.getHeight() * 0.3f);
            hp.lineTo(r.getRight(), r.getY() + r.getHeight() * 0.3f);
            g.strokePath(hp, juce::PathStrokeType(thickness * 0.9f));
            break;
        }

        case IconType::modEnvToWave:
        {
            // An envelope ramp driving a wave that changes shape along it.
            juce::Path env;
            env.startNewSubPath(r.getX(), r.getBottom());
            env.lineTo(r.getX() + r.getWidth() * 0.35f, r.getY());
            env.lineTo(r.getRight(), r.getY() + r.getHeight() * 0.35f);
            g.setColour(colour.withAlpha(0.45f));
            g.strokePath(env, juce::PathStrokeType(thickness * 0.9f));

            g.setColour(colour);
            juce::Path w;
            int steps = 40;
            for (int i = 0; i <= steps; ++i)
            {
                float t = (float) i / steps;
                float x = r.getX() + t * r.getWidth();
                // Waveform grows more complex left to right, as the position sweeps.
                float y = r.getCentreY()
                          - (std::sin(t * juce::MathConstants<float>::twoPi * 2.0f)
                             + t * 0.6f * std::sin(t * juce::MathConstants<float>::twoPi * 6.0f))
                                * r.getHeight() * 0.26f;
                if (i == 0) w.startNewSubPath(x, y); else w.lineTo(x, y);
            }
            g.strokePath(w, juce::PathStrokeType(thickness * 0.9f));
            break;
        }

        case IconType::modEnvToCutoff:
        {
            // An envelope ramp above a filter slope: the classic sweep.
            juce::Path env;
            env.startNewSubPath(r.getX(), r.getBottom());
            env.lineTo(r.getX() + r.getWidth() * 0.32f, r.getY());
            env.lineTo(r.getRight(), r.getY() + r.getHeight() * 0.42f);
            g.setColour(colour.withAlpha(0.45f));
            g.strokePath(env, juce::PathStrokeType(thickness * 0.9f));

            g.setColour(colour);
            juce::Path slope;
            auto lower = r.withTrimmedTop(r.getHeight() * 0.55f);
            slope.startNewSubPath(lower.getX(), lower.getY());
            slope.lineTo(lower.getX() + lower.getWidth() * 0.5f, lower.getY());
            slope.quadraticTo(lower.getX() + lower.getWidth() * 0.75f, lower.getY(), lower.getRight(),
                              lower.getBottom());
            g.strokePath(slope, juce::PathStrokeType(thickness * 0.9f));
            break;
        }

        case IconType::lfoToWave:
        {
            // A slow LFO above a morphing wave.
            g.strokePath(makeWavePath(r.withHeight(r.getHeight() * 0.5f), 1, 0.8f),
                         juce::PathStrokeType(thickness * 0.85f));
            g.setColour(colour.withAlpha(0.75f));
            auto lower = r.withTrimmedTop(r.getHeight() * 0.55f);
            g.strokePath(makeWavePath(lower, 3, 0.85f), juce::PathStrokeType(thickness * 0.85f));
            break;
        }

        case IconType::reverbMix:
        {
            // An impulse smearing out into a tail.
            float cy = r.getCentreY();
            g.fillRect(juce::Rectangle<float>(r.getX(), cy - r.getHeight() * 0.45f, thickness * 1.4f,
                                               r.getHeight() * 0.9f));
            juce::Path tail;
            tail.startNewSubPath(r.getX() + thickness * 2.0f, cy);
            for (int i = 1; i <= 18; ++i)
            {
                float t = (float) i / 18.0f;
                float x = r.getX() + thickness * 2.0f + t * (r.getWidth() - thickness * 2.0f);
                float amp = std::exp(-t * 2.6f) * r.getHeight() * 0.42f;
                float y = cy + (i % 2 == 0 ? amp : -amp);
                tail.lineTo(x, y);
            }
            g.strokePath(tail, juce::PathStrokeType(thickness * 0.75f));
            break;
        }
    }
}

// ---- Look and feel ---------------------------------------------------------

WaveLatheLookAndFeel::WaveLatheLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, colours::textPrimary);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textColourId, colours::textPrimary);
    setColour(juce::TextButton::buttonColourId, colours::panel);
    setColour(juce::TextButton::textColourOffId, colours::textPrimary);
    setColour(juce::TextButton::textColourOnId, colours::background);
    setColour(juce::ToggleButton::textColourId, colours::textPrimary);
    setColour(juce::ToggleButton::tickColourId, colours::accent);
}

juce::Font WaveLatheLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(juce::FontOptions(12.0f));
}

namespace
{
// Wide enough for a sentence to run on rather than being chopped every few
// words, and narrow enough that the eye still finds the next line. JUCE's own
// tooltip wraps at 400, which for prose at 13px is around six or seven words a
// line.
constexpr float tooltipWidth = 460.0f;
constexpr float tooltipFontSize = 13.0f;
constexpr int tooltipPadding = 9;
} // namespace

juce::TextLayout WaveLatheLookAndFeel::layoutTooltip(const juce::String& text, float width)
{
    juce::AttributedString s;
    s.setJustification(juce::Justification::topLeft);
    s.setWordWrap(juce::AttributedString::byWord);

    // Named rather than left to the default, so the tooltip reads in a plain
    // interface face instead of whatever the plugin host happens to leave set.
    // Any of these that is missing falls through to the next.
    juce::String face = "Segoe UI";
    auto available = juce::Font::findAllTypefaceNames();
    for (const auto& candidate : {juce::String("Segoe UI"), juce::String("Arial"),
                                  juce::String("Helvetica"), juce::String("Verdana")})
    {
        if (available.contains(candidate))
        {
            face = candidate;
            break;
        }
    }

    s.append(text, juce::Font(juce::FontOptions(face, tooltipFontSize, juce::Font::plain)),
             colours::textPrimary);

    juce::TextLayout layout;

    // createLayout, not createLayoutWithBalancedLineLengths: balancing is what
    // turns a two-line tip into two awkward half-lines, and the callers write
    // sentences that are meant to run to the edge.
    layout.createLayout(s, width);
    return layout;
}

juce::Rectangle<int> WaveLatheLookAndFeel::getTooltipBounds(const juce::String& tipText,
                                                            juce::Point<int> screenPos,
                                                            juce::Rectangle<int> parentArea)
{
    auto layout = layoutTooltip(tipText, tooltipWidth);

    int w = (int) std::ceil(layout.getWidth()) + tooltipPadding * 2;
    int h = (int) std::ceil(layout.getHeight()) + tooltipPadding * 2;

    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                w, h)
        .constrainedWithin(parentArea);
}

void WaveLatheLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height);

    g.setColour(colours::panel);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(colours::panelEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    layoutTooltip(text, (float) width - tooltipPadding * 2)
        .draw(g, bounds.reduced((float) tooltipPadding));
}

void WaveLatheLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                             juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(2.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto centre = bounds.getCentre();
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto arcRadius = radius - 4.0f;
    float trackThickness = juce::jmax(2.5f, radius * 0.16f);

    // Track
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(colours::knobTrack);
    g.strokePath(track, juce::PathStrokeType(trackThickness, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    // Value arc
    auto accent = slider.isEnabled() ? colours::accent : colours::textDim;
    if (sliderPos > 0.001f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(accent.withAlpha(0.25f));
        g.strokePath(valueArc, juce::PathStrokeType(trackThickness + 3.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        g.setColour(accent);
        g.strokePath(valueArc, juce::PathStrokeType(trackThickness, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    // Notches, on dials whose range holds only a few distinct settings: each
    // one marks the point where the next value takes over, so you can see how
    // far there is left to turn.
    auto bodyRadius = arcRadius - trackThickness * 0.9f - 2.0f;

    if (const auto* boundaries = slider.getProperties()["valueBoundaries"].getArray())
    {
        float outer = arcRadius - trackThickness * 0.5f - 1.0f;
        float inner = juce::jmax(bodyRadius * 0.45f, outer - juce::jmax(4.0f, radius * 0.22f));

        // Thinner when there are many, so a densely stepped dial reads as a
        // fine scale instead of a solid ring.
        float thickness = boundaries->size() > 16 ? 1.0f : juce::jmax(1.5f, radius * 0.06f);

        for (const auto& boundary : *boundaries)
        {
            float position = (float) (double) boundary;
            float notchAngle = rotaryStartAngle + position * (rotaryEndAngle - rotaryStartAngle);

            // Notches already passed pick up the accent, so the dial shows how
            // many settings are behind it as well as where the next one starts.
            g.setColour(position <= sliderPos ? accent.withAlpha(0.9f)
                                              : colours::textPrimary.withAlpha(0.55f));
            g.drawLine(centre.x + std::sin(notchAngle) * inner,
                       centre.y - std::cos(notchAngle) * inner,
                       centre.x + std::sin(notchAngle) * outer,
                       centre.y - std::cos(notchAngle) * outer,
                       thickness);
        }
    }

    // Body
    juce::ColourGradient body(colours::panel.brighter(0.16f), centre.x, centre.y - bodyRadius,
                              colours::panel.darker(0.35f), centre.x, centre.y + bodyRadius, false);
    g.setGradientFill(body);
    g.fillEllipse(juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre));

    g.setColour(colours::panelEdge);
    g.drawEllipse(juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre), 1.0f);

    // Pointer
    juce::Point<float> tip(centre.x + std::sin(angle) * (bodyRadius - 3.0f),
                           centre.y - std::cos(angle) * (bodyRadius - 3.0f));
    juce::Point<float> base(centre.x + std::sin(angle) * bodyRadius * 0.42f,
                            centre.y - std::cos(angle) * bodyRadius * 0.42f);
    g.setColour(accent.brighter(0.3f));
    g.drawLine({base, tip}, juce::jmax(1.6f, radius * 0.09f));
}

// ---- ParameterKnob ---------------------------------------------------------

ParameterKnob::ParameterKnob(const juce::String& nameToUse, IconType iconToUse,
                             juce::NormalisableRange<double> rangeToUse, double initialValue,
                             std::function<juce::String(double)> valueFormatter)
    : name(nameToUse), icon(iconToUse), range(rangeToUse), formatter(std::move(valueFormatter))
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange(0.0, 127.0, 1.0); // one step per MIDI value
    slider.setValue(juce::jlimit(0.0, 127.0, std::round(range.convertTo0to1(initialValue) * 127.0)),
                    juce::dontSendNotification);

    slider.onValueChange = [this]
    {
        if (onValueChanged)
            onValueChanged(getRealValue(), getFormattedValue());
        repaint();
    };

    slider.addListener(this);
    addAndMakeVisible(slider);

    markValueBoundaries();
}

void ParameterKnob::markValueBoundaries()
{
    // A dial covering only a handful of settings - octaves, voice count, filter
    // type - gives no clue how far it has to turn before the setting actually
    // changes. Walking the 128 positions and watching where the readout changes
    // finds those points without each dial having to declare them, and works
    // whatever the range does in between.
    juce::StringArray readings;
    juce::Array<juce::var> boundaries;

    for (int step = 0; step <= 127; ++step)
    {
        auto text = formatter ? formatter(range.convertFrom0to1(step / 127.0)) : juce::String(step);

        if (step > 0 && text != readings[step - 1])
        {
            // Mark the position the readout flips at, halfway between the last
            // step showing the old value and the first showing the new one.
            boundaries.add(juce::var((step - 0.5) / 127.0));
        }

        readings.add(text);
    }

    // Above this the dial is effectively continuous and the notches would be a
    // smear, so only genuinely stepped dials get them.
    constexpr int maxDistinctValues = 40;
    if (boundaries.size() + 1 < maxDistinctValues)
        slider.getProperties().set("valueBoundaries", boundaries);
}

ParameterKnob::~ParameterKnob()
{
    stopTimer();
    slider.removeListener(this);
}

void ParameterKnob::beginGesture()
{
    if (gestureOpen)
        return;

    gestureOpen = true;

    if (onGestureStart)
        onGestureStart();
}

void ParameterKnob::endGesture()
{
    if (!gestureOpen)
        return;

    gestureOpen = false;

    if (onGestureEnd)
        onGestureEnd();
}

void ParameterKnob::sliderDragStarted(juce::Slider*)
{
    dragging = true;
    beginGesture();
}

void ParameterKnob::sliderDragEnded(juce::Slider*)
{
    dragging = false;
    endGesture();
}

void ParameterKnob::sliderValueChanged(juce::Slider*)
{
    // A drag has already announced itself. This is for the other ways a dial
    // moves - the wheel, or a keypress - which have no drag around them, and
    // where a pause is a fair sign that one adjustment has finished.
    if (!dragging && !showValue)
        beginGesture();

    // Reveal the number while the dial is being moved, then let it fade away
    // again so the panel stays uncluttered at rest.
    showValue = true;
    startTimer(900);
    repaint();
}

void ParameterKnob::timerCallback()
{
    stopTimer();
    showValue = false;

    // Closes a wheel or keyboard gesture, which has no mouse-up of its own. A
    // drag that already ended closed itself, and the guard inside makes the
    // second call here harmless.
    if (!dragging)
        endGesture();

    repaint();
}

double ParameterKnob::getRealValue() const
{
    return range.convertFrom0to1(slider.getValue() / 127.0);
}

juce::String ParameterKnob::getFormattedValue() const
{
    return formatter ? formatter(getRealValue()) : juce::String(getRealValue(), 2);
}

void ParameterKnob::setScale(juce::NormalisableRange<double> newRange,
                             std::function<juce::String(double)> newFormatter, double valueToKeep)
{
    range = newRange;
    formatter = std::move(newFormatter);
    markValueBoundaries();
    setRealValue(valueToKeep);
}

void ParameterKnob::setRealValue(double newValue)
{
    auto step = juce::jlimit(0.0, 127.0, std::round(range.convertTo0to1(newValue) * 127.0));

    // Nothing moved, so nothing is drawn. That matters now in a way it did not
    // before: every dial is asked to follow its parameter on every frame, and a
    // repaint here regardless would be sixty-three of them a frame for a panel
    // standing perfectly still.
    if ((int) step == (int) slider.getValue())
        return;

    slider.setValue(step, juce::dontSendNotification);
    repaint();
}

namespace
{
constexpr int knobHeaderHeight = 15;

// Only cells this short count as small dials. A tall cell has room for the name
// above the dial and the dial is already big, so it is left alone however wide
// it happens to be.
constexpr int smallDialHeight = 90;
constexpr float sideLayoutRatio = 1.4f;
} // namespace

bool ParameterKnob::usesSideLayout() const
{
    return getHeight() > 0 && getHeight() <= smallDialHeight
           && (float) getWidth() >= (float) getHeight() * sideLayoutRatio;
}

juce::Rectangle<int> ParameterKnob::dialArea() const
{
    auto r = getLocalBounds();

    if (!usesSideLayout())
        return r.withTrimmedTop(knobHeaderHeight);

    // Square, and as large as the cell's height allows - the point of moving
    // the text aside is that the dial no longer has to make room for it.
    int side = juce::jmin(r.getHeight(), juce::roundToInt((float) r.getWidth() * 0.55f));
    return r.removeFromLeft(side).withSizeKeepingCentre(side, side);
}

juce::Rectangle<int> ParameterKnob::labelArea() const
{
    auto r = getLocalBounds();

    if (!usesSideLayout())
        return r.removeFromTop(knobHeaderHeight);

    int side = juce::jmin(r.getHeight(), juce::roundToInt((float) r.getWidth() * 0.55f));
    r.removeFromLeft(side);
    return r.reduced(5, 0);
}

void ParameterKnob::resized()
{
    slider.setBounds(dialArea());
}

void ParameterKnob::paint(juce::Graphics& g)
{
    auto label = labelArea();
    bool side = usesSideLayout();

    juce::Font nameFont(juce::FontOptions(11.0f));
    g.setFont(nameFont);
    auto textWidth = juce::GlyphArrangement::getStringWidth(nameFont, name);
    float iconSize = 11.0f;

    if (side)
    {
        // Name on top, value under it, both reading from the left edge of the
        // space beside the dial.
        auto nameRow = label.removeFromTop(juce::jmax(14, label.getHeight() / 2)).toFloat();

        drawParameterIcon(g, icon, {nameRow.getX(), nameRow.getCentreY() - iconSize * 0.5f, iconSize, iconSize},
                          colours::accent.withAlpha(0.85f));

        g.setColour(colours::textDim);
        g.drawFittedText(name,
                         juce::Rectangle<float>(nameRow.getX() + iconSize + 4.0f, nameRow.getY(),
                                                 nameRow.getWidth() - iconSize - 4.0f, nameRow.getHeight())
                             .toNearestInt(),
                         juce::Justification::centredLeft, 1, 0.7f);

        g.setColour(showValue ? colours::accent : colours::textPrimary);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawFittedText(getFormattedValue(), label, juce::Justification::centredLeft, 1, 0.6f);
        return;
    }

    float totalWidth = iconSize + 4.0f + textWidth;
    float startX = (float) label.getCentreX() - totalWidth * 0.5f;

    drawParameterIcon(g, icon, {startX, (float) label.getCentreY() - iconSize * 0.5f, iconSize, iconSize},
                      colours::accent.withAlpha(0.85f));

    g.setColour(colours::textDim);
    g.drawText(name, juce::Rectangle<float>(startX + iconSize + 4.0f, (float) label.getY(),
                                             textWidth + 2.0f, (float) label.getHeight())
                         .toNearestInt(),
               juce::Justification::centredLeft);
}

void ParameterKnob::paintOverChildren(juce::Graphics& g)
{
    // Only the stacked layout needs this: with the text beside the dial there
    // is nothing to tuck into the wedge.
    if (usesSideLayout())
        return;

    auto dial = slider.getBounds().toFloat();
    float radius = juce::jmin(dial.getWidth(), dial.getHeight()) * 0.5f - 2.0f;
    if (radius < 8.0f)
        return;

    auto text = getFormattedValue();
    if (text.isEmpty())
        return;

    // The arc stops short of the bottom, leaving a wedge roughly a fifth of the
    // way round. Sitting the readout in it means the number is always there
    // without taking a row of its own or covering the dial face.
    float fontHeight = juce::jlimit(8.0f, 11.0f, radius * 0.36f);
    juce::Font font(juce::FontOptions(fontHeight, juce::Font::bold));

    float wanted = juce::GlyphArrangement::getStringWidth(font, text) + 8.0f;
    float plateWidth = juce::jmin(dial.getWidth() - 2.0f, wanted);
    float plateHeight = fontHeight + 4.0f;

    // Just clear of the arc where the cell is tall enough to allow it, and up
    // inside the wedge where it is not - either way the readout is never laid
    // across the arc it belongs to.
    float preferred = dial.getCentreY() + radius * 1.02f + plateHeight * 0.5f;
    float lowest = (float) getHeight() - plateHeight * 0.5f - 1.0f;

    auto plate = juce::Rectangle<float>(plateWidth, plateHeight)
                     .withCentre({dial.getCentreX(), juce::jmin(preferred, lowest)});

    bool moving = showValue;

    // A backing plate, because the readout sits between the two ends of the arc
    // and would otherwise be read against whatever is behind them.
    g.setColour(colours::background.withAlpha(moving ? 0.92f : 0.75f));
    g.fillRoundedRectangle(plate, 3.0f);

    g.setColour(moving ? colours::accent : colours::textPrimary.withAlpha(0.78f));
    g.setFont(font);
    g.drawFittedText(text, plate.toNearestInt(), juce::Justification::centred, 1, 0.65f);
}

// ---- LcdDisplay ------------------------------------------------------------

LcdDisplay::LcdDisplay(int numLinesToShow) : numLines(juce::jlimit(2, 4, numLinesToShow))
{
    for (int i = 0; i < numLines; ++i)
        lines.add({});
}

void LcdDisplay::print(const juce::String& message)
{
    lines.add(message);
    while (lines.size() > numLines)
        lines.remove(0);
    repaint();
}

void LcdDisplay::printTransient(const juce::String& message)
{
    if (lines.isEmpty())
        lines.add(message);
    else
        lines.set(lines.size() - 1, message);
    repaint();
}

void LcdDisplay::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // Bezel
    g.setColour(juce::Colour{0xff05070a});
    g.fillRoundedRectangle(r, 5.0f);

    auto screen = r.reduced(4.0f);
    g.setColour(colours::lcdBackground);
    g.fillRoundedRectangle(screen, 3.0f);

    // Faint character-cell scanlines for the LCD look.
    g.setColour(colours::lcdDim.withAlpha(0.35f));
    for (float y = screen.getY() + 2.0f; y < screen.getBottom(); y += 3.0f)
        g.drawHorizontalLine((int) y, screen.getX(), screen.getRight());

    g.setColour(colours::lcdDim);
    g.drawRoundedRectangle(screen, 3.0f, 1.0f);

    auto textArea = screen.reduced(7.0f, 4.0f);
    float lineHeight = textArea.getHeight() / (float) numLines;
    auto font = juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                             juce::jmin(13.0f, lineHeight * 0.78f), juce::Font::plain));
    g.setFont(font);

    for (int i = 0; i < lines.size(); ++i)
    {
        auto lineArea = textArea.withHeight(lineHeight).translated(0.0f, i * lineHeight);
        // Dim ghost of the text below, as a lit LCD tends to bloom.
        g.setColour(colours::lcdText.withAlpha(0.18f));
        g.drawText(lines[i], lineArea.translated(0.0f, 1.0f).toNearestInt(), juce::Justification::centredLeft, true);
        g.setColour(colours::lcdText);
        g.drawText(lines[i], lineArea.toNearestInt(), juce::Justification::centredLeft, true);
    }
}

// ---- FlowSection -----------------------------------------------------------

FlowSection::FlowSection(const juce::String& titleToUse, juce::Colour tintToUse)
    : title(titleToUse), tint(tintToUse)
{
    setInterceptsMouseClicks(false, true);
}

void FlowSection::setStyle(SectionStyle newStyle, int stageNumber)
{
    style = newStyle;
    stage = stageNumber;
    repaint();
}

void FlowSection::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const float notch = (float) chevronWidth;

    bool pointsRight = style == SectionStyle::chainStart || style == SectionStyle::chainMiddle;
    bool notchedLeft = style == SectionStyle::chainMiddle || style == SectionStyle::chainEnd;

    juce::Path shape;
    if (pointsRight || notchedLeft)
    {
        // A chevron: the point of one section sits inside the notch of the
        // next, so the chain reads as flowing left to right by its shape.
        float midY = r.getCentreY();
        shape.startNewSubPath(r.getX(), r.getY());
        shape.lineTo(pointsRight ? r.getRight() - notch : r.getRight(), r.getY());
        if (pointsRight)
        {
            shape.lineTo(r.getRight(), midY);
            shape.lineTo(r.getRight() - notch, r.getBottom());
        }
        else
        {
            shape.lineTo(r.getRight(), r.getBottom());
        }
        shape.lineTo(r.getX(), r.getBottom());
        if (notchedLeft)
            shape.lineTo(r.getX() + notch, midY);
        shape.closeSubPath();
    }
    else
    {
        shape.addRoundedRectangle(r, 7.0f);
    }

    g.setColour(colours::panel.withAlpha(0.62f));
    g.fillPath(shape);
    g.setColour(tint.withAlpha(0.34f));
    g.strokePath(shape, juce::PathStrokeType(1.2f));

    auto titleArea = r.reduced(notchedLeft ? notch + 6.0f : 9.0f, 0.0f).removeFromTop(15.0f);

    // Numbered badge fixes each section's place in the chain without relying
    // on a drawn arrow landing in the right spot.
    if (stage > 0)
    {
        auto badge = titleArea.removeFromLeft(14.0f).withSizeKeepingCentre(13.0f, 13.0f);
        g.setColour(tint.withAlpha(0.8f));
        g.fillEllipse(badge);
        g.setColour(colours::background);
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText(juce::String(stage), badge.toNearestInt(), juce::Justification::centred);
        titleArea.removeFromLeft(4.0f);
    }
    else if (style == SectionStyle::modulator)
    {
        // Modulators are not in the chain, so they get a marker instead.
        auto badge = titleArea.removeFromLeft(14.0f).withSizeKeepingCentre(13.0f, 13.0f);
        g.setColour(tint.withAlpha(0.75f));
        g.drawEllipse(badge, 1.2f);
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText("M", badge.toNearestInt(), juce::Justification::centred);
        titleArea.removeFromLeft(4.0f);
    }

    g.setColour(tint.withAlpha(0.9f));
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawText(title.toUpperCase(), titleArea.toNearestInt(), juce::Justification::centredLeft);
}

// ---- Transport buttons -----------------------------------------------------
TransportButton::TransportButton(Shape shapeToDraw, juce::Colour tint)
    : juce::Button({}), shape(shapeToDraw), colour(tint)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void TransportButton::setShape(Shape newShape)
{
    if (shape == newShape)
        return;

    shape = newShape;
    repaint();
}

void TransportButton::setLit(bool shouldBeLit)
{
    if (lit == shouldBeLit)
        return;

    lit = shouldBeLit;
    repaint();
}

void TransportButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    // A lit button is filled with its own colour so it reads as on from across
    // the room; otherwise it is a dark pill that brightens under the mouse.
    auto background = lit ? colour.withAlpha(0.22f)
                          : colours::panel.brighter(isMouseOver ? 0.14f : 0.0f);
    if (isButtonDown)
        background = background.brighter(0.12f);

    g.setColour(background);
    g.fillRoundedRectangle(bounds, 5.0f);

    g.setColour(lit ? colour : colours::panelEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.2f);

    auto icon = bounds.reduced(bounds.getWidth() * 0.30f, bounds.getHeight() * 0.28f);
    float size = juce::jmin(icon.getWidth(), icon.getHeight());
    icon = icon.withSizeKeepingCentre(size, size);

    g.setColour(lit ? colour : colours::textPrimary.withAlpha(isMouseOver ? 1.0f : 0.8f));

    switch (shape)
    {
        case Shape::play:
        {
            // Nudged right by a hair: a triangle centred on its bounding box
            // looks off-centre, because its visual weight is on the left.
            juce::Path triangle;
            triangle.addTriangle(icon.getX() + size * 0.12f, icon.getY(),
                                 icon.getX() + size * 0.12f, icon.getBottom(),
                                 icon.getRight() + size * 0.06f, icon.getCentreY());
            g.fillPath(triangle);
            break;
        }

        case Shape::stop:
            g.fillRoundedRectangle(icon.reduced(size * 0.06f), 1.5f);
            break;

        case Shape::record:
            g.fillEllipse(icon);

            // A ring around it while armed, so an armed transport is obvious
            // even before anything starts moving.
            if (lit)
            {
                g.setColour(colour.withAlpha(0.5f));
                g.drawEllipse(icon.expanded(size * 0.22f), 1.4f);
            }
            break;
    }
}

// ---- Full screen -----------------------------------------------------------
FullScreenButton::FullScreenButton() : juce::Button({})
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void FullScreenButton::setExpanded(bool isFullScreen)
{
    if (expanded == isFullScreen)
        return;

    expanded = isFullScreen;
    repaint();
}

void FullScreenButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    // No frame at rest. It sits among the window's own minimise and close
    // buttons, which are bare glyphs on the title bar, and a button with a box
    // round it in that row would read as something that had landed there rather
    // than as one of them. It lights under the mouse like they do.
    if (isMouseOver || isButtonDown)
    {
        g.setColour(juce::Colours::white.withAlpha(isButtonDown ? 0.16f : 0.09f));
        g.fillRoundedRectangle(bounds, 4.0f);
    }

    // Sized to carry the same visual weight as the glyphs it sits beside.
    float size = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.55f;
    auto icon = bounds.withSizeKeepingCentre(size, size);

    // How far along each edge a bracket runs. A third of the side is enough to
    // read as a corner and short enough that the four stay four.
    const float arm = size * 0.36f;

    juce::Path path;

    // Drawn as four corners of one rectangle. Outward-pointing brackets sit on
    // the icon's own corners; inward-pointing ones are the same shape rotated
    // half a turn about each corner, which is why the two states share a loop.
    const float direction = expanded ? -1.0f : 1.0f;

    for (int corner = 0; corner < 4; ++corner)
    {
        const bool onRight = (corner & 1) != 0;
        const bool onBottom = (corner & 2) != 0;

        const float x = onRight ? icon.getRight() : icon.getX();
        const float y = onBottom ? icon.getBottom() : icon.getY();

        // Which way is "into the icon" from this corner.
        const float towardsX = onRight ? -1.0f : 1.0f;
        const float towardsY = onBottom ? -1.0f : 1.0f;

        // Outwards: the corner stays put and the arms run inwards along the
        // edges. Inwards: the corner moves in by one arm and they run back out.
        const float cornerX = x + (direction > 0.0f ? 0.0f : towardsX * arm);
        const float cornerY = y + (direction > 0.0f ? 0.0f : towardsY * arm);

        path.startNewSubPath(cornerX + towardsX * arm * direction, cornerY);
        path.lineTo(cornerX, cornerY);
        path.lineTo(cornerX, cornerY + towardsY * arm * direction);
    }

    g.setColour(colours::textPrimary.withAlpha(isMouseOver ? 1.0f : 0.78f));
    g.strokePath(path, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::square));
}

// ---- Page tabs -------------------------------------------------------------
namespace
{
constexpr int tabPadding = 22;   // space either side of a tab's text
constexpr int tabMinWidth = 96;
constexpr int tabAccentHeight = 3;

juce::Font tabFont() { return juce::Font(juce::FontOptions(13.0f, juce::Font::bold)); }
} // namespace

PageTabs::PageTabs(juce::StringArray tabNames) : names(std::move(tabNames))
{
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

int PageTabs::getPreferredWidth() const
{
    int total = 0;
    auto font = tabFont();

    for (const auto& name : names)
        total += juce::jmax(tabMinWidth,
                            (int) juce::GlyphArrangement::getStringWidth(font, name) + tabPadding * 2);

    return total;
}

juce::Rectangle<int> PageTabs::boundsForTab(int index) const
{
    auto font = tabFont();
    int x = 0;

    for (int i = 0; i < names.size(); ++i)
    {
        int width = juce::jmax(tabMinWidth,
                               (int) juce::GlyphArrangement::getStringWidth(font, names[i]) + tabPadding * 2);
        if (i == index)
            return {x, 0, width, getHeight()};

        x += width;
    }

    return {};
}

int PageTabs::tabAt(juce::Point<int> position) const
{
    for (int i = 0; i < names.size(); ++i)
        if (boundsForTab(i).contains(position))
            return i;

    return -1;
}

void PageTabs::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();

    // The line the tabs stand on, broken by whichever one is selected - that
    // break is what makes the selected tab read as part of the page below it
    // rather than as another button.
    float baseline = (float) area.getBottom() - 0.5f;

    for (int i = 0; i < names.size(); ++i)
    {
        auto tab = boundsForTab(i);
        bool isSelected = i == selected;
        auto rounded = tab.toFloat().withHeight((float) tab.getHeight() + 8.0f);

        if (isSelected)
        {
            g.setColour(colours::panel);
            g.fillRoundedRectangle(rounded, 7.0f);

            // A bar along the top edge, the usual way a tab says "this one".
            g.setColour(colours::accent);
            g.fillRoundedRectangle(tab.removeFromTop(tabAccentHeight).toFloat().reduced(1.0f, 0.0f), 1.5f);
        }
        else
        {
            g.setColour(colours::background.brighter(i == hovered ? 0.10f : 0.04f));
            g.fillRoundedRectangle(rounded.reduced(0.0f, 2.0f), 7.0f);

            g.setColour(colours::panelEdge.withAlpha(0.7f));
            g.drawLine((float) tab.getX(), baseline, (float) tab.getRight(), baseline, 1.0f);
        }
    }

    // Whatever is left of the strip past the last tab keeps the same line.
    int used = 0;
    for (int i = 0; i < names.size(); ++i)
        used += boundsForTab(i).getWidth();

    if (used < area.getWidth())
    {
        g.setColour(colours::panelEdge.withAlpha(0.7f));
        g.drawLine((float) used, baseline, (float) area.getRight(), baseline, 1.0f);
    }

    g.setFont(tabFont());
    for (int i = 0; i < names.size(); ++i)
    {
        bool isSelected = i == selected;
        g.setColour(isSelected ? colours::textPrimary
                               : (i == hovered ? colours::textPrimary.withAlpha(0.85f) : colours::textDim));
        g.drawText(names[i], boundsForTab(i).withTrimmedTop(tabAccentHeight),
                   juce::Justification::centred);
    }
}

void PageTabs::mouseUp(const juce::MouseEvent& event)
{
    int index = tabAt(event.getPosition());
    if (index >= 0)
        setSelected(index, juce::sendNotification);
}

void PageTabs::mouseMove(const juce::MouseEvent& event)
{
    int index = tabAt(event.getPosition());
    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void PageTabs::mouseExit(const juce::MouseEvent&)
{
    if (hovered != -1)
    {
        hovered = -1;
        repaint();
    }
}

void PageTabs::setSelected(int index, juce::NotificationType notify)
{
    index = juce::jlimit(0, juce::jmax(0, names.size() - 1), index);
    if (index == selected)
        return;

    selected = index;
    repaint();

    if (notify != juce::dontSendNotification && onTabSelected)
        onTabSelected(selected);
}
} // namespace ui
} // namespace wavelathe
