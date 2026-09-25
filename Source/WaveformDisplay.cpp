// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "WaveformDisplay.h"
#include "UiComponents.h"
#include "FxpPreset.h"
#include "Drums/DrumVoice.h"
#include <cmath>

namespace wavelathe
{
namespace
{
constexpr int previewMidiNote = 48; // C3: low enough to show bass character, short enough to render fast
constexpr double previewSeconds = 0.6;
constexpr double previewSampleRate = 44100.0;
constexpr int cyclesToShow = 3;
constexpr int envelopePoints = 220;

// How much of the bus's tail the Master page's envelope shows past the note.
// Enough to see echoes spaced out and a reverb dying away, and short enough
// that a large room does not cost a second of message thread per dial move -
// the drum view caps its hits at three seconds for the same reason, and this
// with the 0.6 second note comes to the same three.
constexpr double maxMasterTailSeconds = 2.4;

// The block size the preview chains are prepared for. Both split anything
// longer themselves, so this only decides how finely they chunk the work.
constexpr int previewBlockSize = 512;

// Peak level in each of envelopePoints equal slices - the envelope view's
// reading of a signal, for the Master page's two signals at once.
std::vector<float> slicePeaks(const std::vector<float>& signal)
{
    std::vector<float> peaks((size_t) envelopePoints, 0.0f);
    const auto total = (int) signal.size();

    if (total <= 0)
        return peaks;

    for (int p = 0; p < envelopePoints; ++p)
    {
        const int start = (int) ((double) p / envelopePoints * total);
        const int end = juce::jmin(total, juce::jmax(start + 1, (int) ((double) (p + 1) / envelopePoints * total)));

        float peak = 0.0f;
        for (int i = start; i < end; ++i)
            peak = juce::jmax(peak, std::abs(signal[(size_t) i]));

        peaks[(size_t) p] = peak;
    }

    return peaks;
}
} // namespace

WaveformDisplay::WaveformDisplay(WaveLatheProcessor& processor) : processorRef(processor)
{
    renderer = std::make_unique<OfflineRenderer>(processor.getActiveWavetables(), previewSampleRate);
    startTimerHz(12);
}

WaveformDisplay::~WaveformDisplay()
{
    stopTimer();
}

void WaveformDisplay::timerCallback()
{
    if (dirty.exchange(false))
        regenerate();
}

void WaveformDisplay::showDrumSlot(int voice)
{
    if (voice < 0 || voice >= project::numDrumVoices)
        return;

    // Marked dirty even when the slot is the one already showing, because the
    // caller is saying "this has been worked on" rather than "look at this
    // one" - and the usual case is a dial moving on the drum already drawn.
    // The timer is what stops a drag from re-rendering per pixel.
    showing = Showing::drum;
    drumSlot = voice;
    cycleGhost.clear();
    envelopeGhost.clear();
    markDirty();
}

void WaveformDisplay::showSynth()
{
    // Asked of the mode, not of drumSlot. It used to return early whenever no
    // drum slot was set, which was "already on the patch" while there were two
    // modes - and would be "stuck on the Master page" with three.
    if (showing == Showing::synth)
        return;

    showing = Showing::synth;
    drumSlot = -1;
    drumSeconds = 0.0;
    waveLabel = "WAVEFORM";
    cycleGhost.clear();
    envelopeGhost.clear();
    markDirty();
}

void WaveformDisplay::showMaster()
{
    if (showing == Showing::master)
        return;

    showing = Showing::master;
    drumSlot = -1;
    drumSeconds = 0.0;
    waveLabel = "MASTER";
    markDirty();
}

void WaveformDisplay::regenerate()
{
    switch (showing)
    {
        case Showing::drum:   regenerateDrum(drumSlot); break;
        case Showing::master: regenerateMaster(); break;
        case Showing::synth:  regenerateSynth(); break;
    }
}

// One drum, rendered dry.
//
// A drums::Voice rather than a whole Kit, and that is the point of it: a Kit
// would run the slot's two sends, and a six-second reverb tail turns the
// envelope into a chart of the reverb rather than of the drum. The dials this
// display sits above are Attack, Decay and Tune - all of them about the voice -
// so the voice on its own is what it should show. The sends have their own
// dials with their own readouts.
//
// Reads the LIVE parameters by reference. They are atomics and Voice only ever
// reads them, so this costs nothing and cannot drift from what is sounding.
void WaveformDisplay::regenerateDrum(int voice)
{
    const auto& kit = processorRef.getDrumParameters();
    const auto& params = kit.voices[(size_t) voice];
    const auto engine = params.engine.load();

    const auto* bank = processorRef.getPublishedSampleBank();
    const auto* sample = (bank != nullptr && bank->has(voice)) ? bank->slots[(size_t) voice]
                                                              : nullptr;

    waveLabel = juce::String(project::drumVoiceName(voice)).toUpperCase();

    // An empty slot has nothing to draw, and drawing the kick that engineSpec
    // hands back for noEngine would be a display inventing a sound.
    if (engine == drums::noEngine && sample == nullptr)
    {
        cycleView.assign(1, 0.0f);
        envelopeView.assign(envelopePoints, 0.0f);
        drumSeconds = 0.0;
        repaint();
        return;
    }

    const auto& spec = drums::engineSpec(engine);

    // As long as the hit actually lasts, capped so a crash at full Decay does
    // not spend a second of message thread on a picture.
    const auto decay = params.decay.load();
    const auto attack = params.attack.load();

    double seconds = sample != nullptr
                         ? (double) drums::sampleAttackSeconds(*sample, attack, decay)
                               + (double) drums::sampleAudibleSeconds(*sample, decay)
                         : (double) drums::hitLengthSeconds(spec, decay, attack);

    seconds = juce::jlimit(0.05, 3.0, seconds);
    drumSeconds = seconds;

    const int numSamples = (int) (seconds * previewSampleRate);

    // Mono, deliberately: one channel makes Voice take its constant-power mono
    // path, so what is drawn is the whole of the sound rather than whichever
    // side Pan happened to favour.
    juce::AudioBuffer<float> rendered(1, numSamples);
    rendered.clear();

    drums::Voice player;
    player.prepare(previewSampleRate, 0x9e3779b9u | 1u);
    player.trigger(1.0f, true, spec, params, sample);

    // Master level 1, so the picture is of the drum rather than of where the
    // kit fader happens to be.
    player.renderAdding(rendered, 0, numSamples, params, 1.0f);

    const float* data = rendered.getReadPointer(0);

    envelopeView.assign(envelopePoints, 0.0f);
    for (int p = 0; p < envelopePoints; ++p)
    {
        int start = (int) ((double) p / envelopePoints * numSamples);
        int end = juce::jmax(start + 1, (int) ((double) (p + 1) / envelopePoints * numSamples));
        end = juce::jmin(end, numSamples);

        float peak = 0.0f;
        for (int i = start; i < end; ++i)
            peak = juce::jmax(peak, std::abs(data[i]));
        envelopeView[(size_t) p] = peak;
    }

    // A window from just after the strike, which is where a drum's character
    // is. Three cycles of its own pitch where it has one; a fixed short window
    // where it does not, because a clap and a maraca have no cycle to count.
    const auto pitched = (double) spec.frequency * (double) drums::tuneRatioFor(params.tune.load());

    int windowLength = pitched > 20.0
                           ? (int) (previewSampleRate / pitched) * cyclesToShow
                           : (int) (previewSampleRate * 0.008);

    windowLength = juce::jlimit(2, numSamples, windowLength);

    // Past the attack ramp, so a swelled hit shows its body rather than the
    // silence it starts from.
    const auto attackSamples = sample != nullptr
                                   ? (int) (drums::sampleAttackSeconds(*sample, attack, decay) * previewSampleRate)
                                   : (int) (drums::attackSecondsFor(spec, attack, decay) * previewSampleRate);

    const int windowStart = juce::jlimit(0, juce::jmax(0, numSamples - windowLength),
                                         attackSamples + (int) (previewSampleRate * 0.002));

    cycleView.assign((size_t) windowLength, 0.0f);
    for (int i = 0; i < windowLength; ++i)
        cycleView[(size_t) i] = data[windowStart + i];

    float peak = 0.0f;
    for (auto v : cycleView) peak = juce::jmax(peak, std::abs(v));
    if (peak > 1.0e-6f)
        for (auto& v : cycleView) v /= peak;

    repaint();
}

void WaveformDisplay::regenerateSynth()
{
    // Track whichever wavetable set is live, so sampling a new one updates the view.
    renderer->setTables(processorRef.getActiveWavetables());

    auto values = FxpPreset::fromSynthParameters(processorRef.getParameters(), "preview");

    juce::AudioBuffer<float> rendered;
    renderer->render(values, previewMidiNote, previewSeconds, rendered);

    int numSamples = rendered.getNumSamples();
    if (numSamples <= 0)
        return;

    const float* data = rendered.getReadPointer(0);

    // --- Envelope over the whole note ---
    envelopeView.assign(envelopePoints, 0.0f);
    for (int p = 0; p < envelopePoints; ++p)
    {
        int start = (int) ((double) p / envelopePoints * numSamples);
        int end = juce::jmax(start + 1, (int) ((double) (p + 1) / envelopePoints * numSamples));
        end = juce::jmin(end, numSamples);

        float peak = 0.0f;
        for (int i = start; i < end; ++i)
            peak = juce::jmax(peak, std::abs(data[i]));
        envelopeView[(size_t) p] = peak;
    }

    // --- A few cycles from the sustained portion ---
    double frequency = juce::MidiMessage::getMidiNoteInHertz(previewMidiNote);
    int samplesPerCycle = (int) (previewSampleRate / frequency);
    int windowLength = juce::jmin(samplesPerCycle * cyclesToShow, numSamples);

    // Start partway in so the attack transient isn't what's shown, and try to
    // begin on a rising zero crossing so the trace doesn't jitter frame to frame.
    int searchStart = juce::jlimit(0, juce::jmax(0, numSamples - windowLength - 1), numSamples / 3);
    int windowStart = searchStart;
    for (int i = searchStart; i < juce::jmin(searchStart + samplesPerCycle, numSamples - windowLength - 1); ++i)
    {
        if (data[i] <= 0.0f && data[i + 1] > 0.0f)
        {
            windowStart = i;
            break;
        }
    }

    cycleView.assign((size_t) juce::jmax(1, windowLength), 0.0f);
    for (int i = 0; i < windowLength; ++i)
        cycleView[(size_t) i] = data[windowStart + i];

    // Normalise the cycle view so quiet patches are still legible in shape.
    float peak = 0.0f;
    for (auto v : cycleView) peak = juce::jmax(peak, std::abs(v));
    if (peak > 1.0e-6f)
        for (auto& v : cycleView) v /= peak;

    repaint();
}

// The patch through the Master page, over the patch as it went in.
//
// The same C3 note the Synth page draws, then through a private copy of the
// bus effects and of the mastering chain, set exactly as the real ones are.
// Real DSP rather than a drawing of what it ought to do, for the reason the
// drum view renders a real voice: a picture computed from a second copy of the
// maths is a picture that drifts the first time the maths changes.
void WaveformDisplay::regenerateMaster()
{
    renderer->setTables(processorRef.getActiveWavetables());

    const auto& live = processorRef.getParameters();
    auto values = FxpPreset::fromSynthParameters(live, "preview");

    // The note as the Synth page makes it - voices and the synth's own effects.
    juce::AudioBuffer<float> note;
    renderer->render(values, previewMidiNote, previewSeconds, note);

    const int noteSamples = note.getNumSamples();
    if (noteSamples <= 0)
        return;

    // Long enough to hear the bus ring on after the note, so the delay's
    // repeats and the reverb's decay are IN the picture rather than cut off
    // at the edge of it. Nothing extra while the bus is dry.
    const auto busSettings = busEffectSettingsFrom(live);
    const double tail = juce::jlimit(0.0, maxMasterTailSeconds, effectsTailSeconds(busSettings));
    const int totalSamples = noteSamples + (int) (tail * previewSampleRate);

    if (!previewChainsPrepared)
    {
        previewBus.prepare(previewSampleRate, previewBlockSize, 2);
        previewMastering.prepare(previewSampleRate, previewBlockSize, 2);
        previewChainsPrepared = true;
    }

    // From silence every time, so one picture never carries the last one's
    // reverb tail or the limiter's recovery into it.
    previewBus.reset();
    previewMastering.reset();

    WaveLatheProcessor::fillMasteringParameters(live, previewMasteringParameters);

    // The mastering chain looks ahead, so what comes out is late by this much.
    // The buffer runs that far past the end, and the reading below starts that
    // far in, so the after lines up with the before sample for sample - a
    // limiter's lookahead drawn as a shift would look like a delay it is not.
    //
    // Whether the chain is working or bypassed, since 0.47.2. It was not so
    // before: a bypassed chain handed the audio back undelayed while still
    // reporting its latency, and 0.47.1 skipped this correction while bypassed
    // to match. That same mismatch made a host play the whole track early, so
    // the chain was fixed instead - bypass is now exactly this late - and the
    // correction here is unconditional again.
    const int latency = juce::jmax(0, previewMastering.getLatencySamples());

    // Stereo, because both chains are: the delay ping-pongs, the reverb and
    // chorus are wide, and the mastering chain does nothing at all to a buffer
    // with fewer channels than it was prepared for.
    juce::AudioBuffer<float> wet(2, totalSamples + latency);
    wet.clear();

    for (int channel = 0; channel < 2; ++channel)
        wet.copyFrom(channel, 0, note, 0, 0, noteSamples);

    previewBus.process(wet, busSettings, processorRef.getEffectiveBpm());
    previewMastering.process(wet, previewMasteringParameters);

    // Both as one mono line on one time axis: the before is the note and then
    // silence, the after is the two channels averaged, with the lookahead
    // taken back out.
    std::vector<float> before((size_t) totalSamples, 0.0f);
    std::vector<float> after((size_t) totalSamples, 0.0f);

    const float* noteData = note.getReadPointer(0);
    for (int i = 0; i < noteSamples; ++i)
        before[(size_t) i] = noteData[i];

    const float* left = wet.getReadPointer(0);
    const float* right = wet.getReadPointer(1);
    for (int i = 0; i < totalSamples; ++i)
        after[(size_t) i] = 0.5f * (left[i + latency] + right[i + latency]);

    envelopeView = slicePeaks(after);
    envelopeGhost = slicePeaks(before);

    // The same three cycles the Synth page picks, from the same place in the
    // note, for both - so the ghost and the trace are the same moment and the
    // only difference between them is what this page did.
    const double frequency = juce::MidiMessage::getMidiNoteInHertz(previewMidiNote);
    const int samplesPerCycle = (int) (previewSampleRate / frequency);
    const int windowLength = juce::jmin(samplesPerCycle * cyclesToShow, noteSamples);

    const int searchStart = juce::jlimit(0, juce::jmax(0, noteSamples - windowLength - 1), noteSamples / 3);
    int windowStart = searchStart;

    for (int i = searchStart; i < juce::jmin(searchStart + samplesPerCycle, noteSamples - windowLength - 1); ++i)
    {
        if (before[(size_t) i] <= 0.0f && before[(size_t) i + 1] > 0.0f)
        {
            windowStart = i;
            break;
        }
    }

    cycleView.assign((size_t) juce::jmax(1, windowLength), 0.0f);
    cycleGhost.assign((size_t) juce::jmax(1, windowLength), 0.0f);

    for (int i = 0; i < windowLength; ++i)
    {
        cycleView[(size_t) i] = after[(size_t) (windowStart + i)];
        cycleGhost[(size_t) i] = before[(size_t) (windowStart + i)];
    }

    // ONE scale for the pair, unlike the Synth page's own-peak scaling. Scaled
    // separately, a bus Gain of -12 dB or a limiter pulling the peaks down would
    // vanish - both traces would fill the box - and a level change is one of
    // the things this page exists to make.
    float peak = 0.0f;
    for (size_t i = 0; i < cycleView.size(); ++i)
        peak = juce::jmax(peak, juce::jmax(std::abs(cycleView[i]), std::abs(cycleGhost[i])));

    if (peak > 1.0e-6f)
    {
        for (auto& v : cycleView) v /= peak;
        for (auto& v : cycleGhost) v /= peak;
    }

    repaint();
}

void WaveformDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(ui::colours::panel.withAlpha(0.55f));
    g.fillRoundedRectangle(bounds, 7.0f);
    g.setColour(ui::colours::panelEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 1.0f);

    auto content = bounds.reduced(8.0f);
    auto waveArea = content.removeFromLeft(content.getWidth() * 0.62f);
    content.removeFromLeft(8.0f);
    auto envArea = content;

    auto drawLabel = [&g](juce::Rectangle<float> area, const juce::String& text)
    {
        g.setColour(ui::colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        g.drawText(text, area.removeFromTop(11.0f).toNearestInt(), juce::Justification::centredLeft);
    };

    // ---- Waveform ----
    auto waveLabelArea = waveArea;
    drawLabel(waveLabelArea, waveLabel);
    auto wavePlot = waveArea.withTrimmedTop(12.0f);

    // The ghost is drawn in the display's own quiet colour, and the Master page
    // says so, because two traces with nothing to tell them apart are two
    // traces nobody can read.
    const bool ghosted = cycleGhost.size() == cycleView.size() && cycleGhost.size() > 1;
    const auto ghostColour = ui::colours::textDim.withAlpha(0.55f);

    if (ghosted)
    {
        g.setColour(ghostColour);
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        g.drawText("GREY = BEFORE THIS PAGE", waveArea.withHeight(11.0f).toNearestInt(),
                   juce::Justification::centredRight);
    }

    g.setColour(ui::colours::flowLine.withAlpha(0.6f));
    g.drawHorizontalLine((int) wavePlot.getCentreY(), wavePlot.getX(), wavePlot.getRight());

    const auto cyclePath = [&wavePlot](const std::vector<float>& cycle)
    {
        juce::Path path;
        for (size_t i = 0; i < cycle.size(); ++i)
        {
            float x = wavePlot.getX() + (float) i / (cycle.size() - 1) * wavePlot.getWidth();
            float y = wavePlot.getCentreY() - cycle[i] * wavePlot.getHeight() * 0.45f;
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        return path;
    };

    if (cycleView.size() > 1)
    {
        if (ghosted)
        {
            g.setColour(ghostColour);
            g.strokePath(cyclePath(cycleGhost), juce::PathStrokeType(1.2f));
        }

        const auto path = cyclePath(cycleView);

        g.setColour(ui::colours::accent.withAlpha(0.22f));
        g.strokePath(path, juce::PathStrokeType(3.6f));
        g.setColour(ui::colours::accent);
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }
    else
    {
        g.setColour(ui::colours::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("(silent)", wavePlot.toNearestInt(), juce::Justification::centred);
    }

    // ---- Envelope ----
    auto envLabelArea = envArea;
    drawLabel(envLabelArea, "ENVELOPE");
    auto envPlot = envArea.withTrimmedTop(12.0f);

    g.setColour(ui::colours::flowLine.withAlpha(0.6f));
    g.drawHorizontalLine((int) envPlot.getBottom(), envPlot.getX(), envPlot.getRight());

    if (envelopeView.size() > 1)
    {
        const bool envelopeGhosted = envelopeGhost.size() == envelopeView.size();

        // One scale for both on the Master page, for the reason the cycles
        // share one: a limiter's squash and a gain change are only visible
        // against a before drawn to the same scale.
        float peak = 0.0f;
        for (auto v : envelopeView) peak = juce::jmax(peak, v);
        if (envelopeGhosted)
            for (auto v : envelopeGhost) peak = juce::jmax(peak, v);
        if (peak < 1.0e-6f)
            peak = 1.0f;

        const auto envelopePath = [&envPlot, peak](const std::vector<float>& envelope)
        {
            juce::Path filled;
            filled.startNewSubPath(envPlot.getX(), envPlot.getBottom());
            for (size_t i = 0; i < envelope.size(); ++i)
            {
                float x = envPlot.getX() + (float) i / (envelope.size() - 1) * envPlot.getWidth();
                float y = envPlot.getBottom() - (envelope[i] / peak) * envPlot.getHeight() * 0.92f;
                filled.lineTo(x, y);
            }
            filled.lineTo(envPlot.getRight(), envPlot.getBottom());
            filled.closeSubPath();
            return filled;
        };

        // An outline only, under the real one's fill, so the note's own shape
        // shows through wherever the bus has added a tail beyond it.
        if (envelopeGhosted)
        {
            g.setColour(ghostColour);
            g.strokePath(envelopePath(envelopeGhost), juce::PathStrokeType(1.2f));
        }

        const auto filled = envelopePath(envelopeView);

        g.setColour(ui::colours::accentWarm.withAlpha(0.20f));
        g.fillPath(filled);
        g.setColour(ui::colours::accentWarm);
        g.strokePath(filled, juce::PathStrokeType(1.3f));
    }
}
} // namespace wavelathe
