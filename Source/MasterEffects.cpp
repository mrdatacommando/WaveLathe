// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MasterEffects.h"
#include <cmath>

namespace wavelathe
{
namespace
{
constexpr double maxDelaySeconds = 2.0;
constexpr float smoothingSeconds = 0.05f;
} // namespace

void MasterEffects::prepare(double sampleRateToUse, int maximumBlockSizeToUse, int numChannels)
{
    sampleRate = sampleRateToUse;
    maximumBlockSize = juce::jmax(1, maximumBlockSizeToUse);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maximumBlockSize;
    spec.numChannels = (juce::uint32) juce::jmax(1, numChannels);

    delayLine.setMaximumDelayInSamples((int) (maxDelaySeconds * sampleRate) + 4);
    delayLine.prepare(spec);
    delayLine.reset();

    reverb.setSampleRate(sampleRate);
    reverb.reset();

    chorus.prepare(spec);
    chorus.reset();
    phaser.prepare(spec);
    phaser.reset();

    for (auto* band : {&lowShelf, &midBell, &highShelf})
    {
        band->prepare(spec);
        band->reset();
    }

    // Forces the coefficients to be built on the first block.
    builtLowGain = builtMidGain = builtHighGain = 1.0e9f;

    for (auto* smoothed : {&smoothedDistortion, &smoothedDelayMix, &smoothedDelayFeedback, &smoothedDelaySamples,
                            &smoothedReverbMix})
        smoothed->reset(sampleRate, (double) smoothingSeconds);

    // A different type from the five above - multiplicative - so it cannot
    // join their loop.
    smoothedOutputGain.reset(sampleRate, (double) smoothingSeconds);

    prepared = true;
}

EffectSettings effectSettingsFrom(const SynthParameters& params)
{
    EffectSettings s;

    s.distortion = params.fxDistortion.load();

    s.chorusRate = params.chorusRate.load();
    s.chorusDepth = params.chorusDepth.load();
    s.chorusMix = params.chorusMix.load();

    s.phaserRate = params.phaserRate.load();
    s.phaserFeedback = params.phaserFeedback.load();
    s.phaserMix = params.phaserMix.load();

    s.delayTimeMs = params.delayTimeMs.load();
    s.delayFeedback = params.delayFeedback.load();
    s.delayMix = params.delayMix.load();
    s.delaySync = params.delaySync.load() > 0.5f;

    s.reverbSize = params.reverbSize.load();
    s.reverbMix = params.reverbMix.load();

    s.eqLowDb = params.eqLowGain.load();
    s.eqMidDb = params.eqMidGain.load();
    s.eqHighDb = params.eqHighGain.load();

    // Left at unity: see EffectSettings::outputGain.
    return s;
}

EffectSettings busEffectSettingsFrom(const SynthParameters& params)
{
    using B = project::BusFxControl;
    const auto read = [&params](B control) { return params.busFx[(size_t) control].load(); };

    EffectSettings s;

    s.distortion = read(B::distortion);

    s.chorusRate = read(B::chorusRate);
    s.chorusDepth = read(B::chorusDepth);
    s.chorusMix = read(B::chorusMix);

    s.phaserRate = read(B::phaserRate);
    s.phaserFeedback = read(B::phaserFeedback);
    s.phaserMix = read(B::phaserMix);

    s.delayTimeMs = read(B::delayTime);
    s.delayFeedback = read(B::delayFeedback);
    s.delayMix = read(B::delayMix);
    s.delaySync = read(B::delaySync) > 0.5f;

    s.reverbSize = read(B::reverbSize);
    s.reverbMix = read(B::reverbMix);

    s.eqLowDb = read(B::eqLow);
    s.eqMidDb = read(B::eqMid);
    s.eqHighDb = read(B::eqHigh);

    // The dial's own range, clamped here rather than trusted: a hand-edited
    // project could hold anything, and +200 dB into the mastering chain is not
    // a setting, it is an accident with a speaker on the end of it.
    s.outputGain = juce::Decibels::decibelsToGain(juce::jlimit(-24.0f, 24.0f, read(B::outputGain)));

    return s;
}

void MasterEffects::reset()
{
    delayLine.reset();
    reverb.reset();
    chorus.reset();
    phaser.reset();

    for (auto* band : {&lowShelf, &midBell, &highShelf})
        band->reset();

    // Whatever is played next starts from its own settings, not from a glide
    // out of the ones that were in force when this was reset.
    snapSmoothers = true;
}

// Rebuilt only when a gain actually moves: making biquad coefficients every
// block would cost more than the filtering does.
void MasterEffects::updateEqualiser(float lowDb, float midDb, float highDb)
{
    if (std::abs(lowDb - builtLowGain) < 0.01f && std::abs(midDb - builtMidGain) < 0.01f
        && std::abs(highDb - builtHighGain) < 0.01f)
        return;

    builtLowGain = lowDb;
    builtMidGain = midDb;
    builtHighGain = highDb;

    auto gain = [](float db) { return juce::Decibels::decibelsToGain(db, -30.0f); };

    *lowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sampleRate, eq::lowFrequency, eq::lowQ, gain(lowDb));
    *midBell.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, eq::midFrequency, eq::midQ, gain(midDb));
    *highShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, eq::highFrequency, eq::highQ, gain(highDb));
}


float MasterEffects::getEqualiserMagnitude(float lowDb, float midDb, float highDb, double frequency,
                                            double sampleRate)
{
    if (sampleRate <= 0.0)
        return 1.0f;

    // Clamped the same way the audio path clamps them, so a curve cannot show a
    // boost the filtering would not actually apply.
    lowDb = juce::jlimit(-12.0f, 12.0f, lowDb);
    midDb = juce::jlimit(-12.0f, 12.0f, midDb);
    highDb = juce::jlimit(-12.0f, 12.0f, highDb);

    auto gain = [](float db) { return juce::Decibels::decibelsToGain(db, -30.0f); };

    // The same three makers with the same three sets of numbers. Asking the
    // coefficients for their magnitude rather than working one out by hand is
    // what makes this the response rather than a drawing of one.
    auto low = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate, eq::lowFrequency,
                                                                  eq::lowQ, gain(lowDb));
    auto mid = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, eq::midFrequency,
                                                                    eq::midQ, gain(midDb));
    auto high = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, eq::highFrequency,
                                                                    eq::highQ, gain(highDb));

    // Multiplied, because the three run one after another. Adding the decibels
    // would come to the same thing; multiplying the magnitudes is what the
    // signal actually has done to it.
    return (float) (low->getMagnitudeForFrequency(frequency, sampleRate)
                    * mid->getMagnitudeForFrequency(frequency, sampleRate)
                    * high->getMagnitudeForFrequency(frequency, sampleRate));
}

float MasterEffects::softClip(float x, float amount)
{
    if (amount <= 0.0f)
        return x;

    float gain = 1.0f + amount * 12.0f;
    float shaped = std::tanh(x * gain) / std::tanh(gain);
    return x + amount * (shaped - x);
}

void MasterEffects::process(juce::AudioBuffer<float>& buffer, const EffectSettings& params, double bpm)
{
    if (!prepared)
        return;

    int total = buffer.getNumSamples();

    // Split rather than trust the caller. Offline rendering hands over a whole
    // note at once, which is far longer than any block size this was prepared
    // for, and the effects that allocate on prepare cannot survive that.
    if (total > maximumBlockSize)
    {
        for (int start = 0; start < total; start += maximumBlockSize)
        {
            int count = juce::jmin(maximumBlockSize, total - start);
            juce::AudioBuffer<float> chunk(buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                            start, count);
            processChunk(chunk, params, bpm);
        }

        return;
    }

    processChunk(buffer, params, bpm);
}

void MasterEffects::processChunk(juce::AudioBuffer<float>& buffer, const EffectSettings& params, double bpm)
{

    int numSamples = buffer.getNumSamples();
    int numChannels = juce::jmin(2, buffer.getNumChannels());
    if (numSamples <= 0 || numChannels <= 0)
        return;

    float distortion = juce::jlimit(0.0f, 1.0f, params.distortion);
    float delayMix = juce::jlimit(0.0f, 1.0f, params.delayMix);
    float delayFeedback = juce::jlimit(0.0f, 0.95f, params.delayFeedback);
    float delayTimeMs = juce::jlimit(10.0f, (float) (maxDelaySeconds * 1000.0), params.delayTimeMs);
    // Snapped to the tempo grid when the delay is set to sync.
    if (params.delaySync)
        delayTimeMs = (float) juce::jlimit(10.0, maxDelaySeconds * 1000.0,
                                            tempo::divisionToMs(tempo::nearestDivisionForMs(delayTimeMs, bpm), bpm));
    float reverbMix = juce::jlimit(0.0f, 1.0f, params.reverbMix);
    float reverbSize = juce::jlimit(0.0f, 1.0f, params.reverbSize);

    // Never zero: a multiplicative smoother cannot glide out of nothing.
    const float outputGain = juce::jmax(1.0e-4f, params.outputGain);

    smoothedDistortion.setTargetValue(distortion);
    smoothedDelayMix.setTargetValue(delayMix);
    smoothedDelayFeedback.setTargetValue(delayFeedback);
    smoothedDelaySamples.setTargetValue((float) (delayTimeMs * 0.001 * sampleRate));
    smoothedReverbMix.setTargetValue(reverbMix);
    smoothedOutputGain.setTargetValue(outputGain);

    // On the first block after a reset, every smoother jumps straight to the
    // value it was asked for. Normally they glide over ~50ms so a dial moved
    // mid-note does not zipper, but at the start of a note that glide comes out
    // of whatever the previous note left behind - which is inaudible when
    // playing and ruinous when measuring, because it makes an offline render
    // depend on which candidate a worker thread happened to render before it.
    if (snapSmoothers)
    {
        smoothedDistortion.setCurrentAndTargetValue(distortion);
        smoothedDelayMix.setCurrentAndTargetValue(delayMix);
        smoothedDelayFeedback.setCurrentAndTargetValue(delayFeedback);
        smoothedDelaySamples.setCurrentAndTargetValue((float) (delayTimeMs * 0.001 * sampleRate));
        smoothedReverbMix.setCurrentAndTargetValue(reverbMix);
        smoothedOutputGain.setCurrentAndTargetValue(outputGain);
    }


    // ---- Distortion ----
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float amount = smoothedDistortion.getNextValue();
        if (amount <= 0.0f)
            continue;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = buffer.getWritePointer(channel);
            data[sample] = softClip(data[sample], amount);
        }
    }

    // ---- Chorus and phaser ----
    // Both are skipped entirely at zero mix rather than run and thrown away,
    // since a patch using neither is the common case.
    {
        juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(),
                                            (size_t) numChannels, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context(block);

        float chorusMix = juce::jlimit(0.0f, 1.0f, params.chorusMix);
        if (chorusMix > 0.0001f)
        {
            chorus.setRate(juce::jlimit(0.05f, 8.0f, params.chorusRate));
            chorus.setDepth(juce::jlimit(0.0f, 1.0f, params.chorusDepth));
            chorus.setCentreDelay(12.0f);
            chorus.setFeedback(0.0f);
            chorus.setMix(chorusMix);
            // Setting the targets and then resetting is what snaps the chorus's own
            // internal smoothers to them; JUCE offers no other way in.
            if (snapSmoothers)
                chorus.reset();

            chorus.process(context);
        }

        float phaserMix = juce::jlimit(0.0f, 1.0f, params.phaserMix);
        if (phaserMix > 0.0001f)
        {
            phaser.setRate(juce::jlimit(0.05f, 8.0f, params.phaserRate));
            phaser.setDepth(0.7f);
            phaser.setCentreFrequency(600.0f);
            phaser.setFeedback(juce::jlimit(0.0f, 0.9f, params.phaserFeedback));
            phaser.setMix(phaserMix);
            if (snapSmoothers)
                phaser.reset();

            phaser.process(context);
        }
    }

    // ---- Ping-pong delay ----
    // Feedback is crossed between the channels, so repeats alternate sides
    // and the delay adds stereo movement rather than just depth.
    if (delayMix > 0.0001f || smoothedDelayMix.isSmoothing())
    {
        auto* left = buffer.getWritePointer(0);
        auto* right = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float delaySamples = smoothedDelaySamples.getNextValue();
            float feedback = smoothedDelayFeedback.getNextValue();
            float mix = smoothedDelayMix.getNextValue();

            delayLine.setDelay(delaySamples);

            float dryLeft = left[sample];
            float dryRight = right != nullptr ? right[sample] : dryLeft;

            float wetLeft = delayLine.popSample(0);
            float wetRight = numChannels > 1 ? delayLine.popSample(1) : wetLeft;

            delayLine.pushSample(0, dryLeft + wetRight * feedback);
            if (numChannels > 1)
                delayLine.pushSample(1, dryRight + wetLeft * feedback);

            left[sample] = dryLeft + (wetLeft - dryLeft) * mix;
            if (right != nullptr)
                right[sample] = dryRight + (wetRight - dryRight) * mix;
        }
    }

    // ---- Reverb ----
    if (reverbMix > 0.0001f)
    {
        reverbParameters.roomSize = juce::jlimit(0.0f, 1.0f, 0.2f + reverbSize * 0.75f);
        reverbParameters.damping = 0.4f;
        reverbParameters.width = 1.0f;
        reverbParameters.wetLevel = reverbMix;
        reverbParameters.dryLevel = 1.0f - reverbMix * 0.6f; // keep some dry signal at full wet
        reverbParameters.freezeMode = 0.0f;
        reverb.setParameters(reverbParameters);

        // JUCE smooths the reverb's wet and dry gains, and its reset() does not
        // touch them - re-stating the sample rate is the only way in, and it
        // snaps them to the levels just set instead of gliding up from the last
        // note's.
        if (snapSmoothers)
            reverb.setSampleRate(sampleRate);

        if (numChannels > 1)
            reverb.processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);
        else
            reverb.processMono(buffer.getWritePointer(0), numSamples);
    }

    // ---- EQ ----
    // Last, so it has the final word over everything that came before it,
    // including the reverb tail. Skipped when all three bands are flat.
    float lowDb = juce::jlimit(-12.0f, 12.0f, params.eqLowDb);
    float midDb = juce::jlimit(-12.0f, 12.0f, params.eqMidDb);
    float highDb = juce::jlimit(-12.0f, 12.0f, params.eqHighDb);

    if (std::abs(lowDb) > 0.01f || std::abs(midDb) > 0.01f || std::abs(highDb) > 0.01f)
    {
        updateEqualiser(lowDb, midDb, highDb);

        juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(),
                                            (size_t) numChannels, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context(block);

        lowShelf.process(context);
        midBell.process(context);
        highShelf.process(context);
    }

    // ---- Output ----
    // After the EQ, as the last word on the level. Skipped outright at unity,
    // which is always the case for the Synth page's chain - so that path is
    // not merely close to what it was before this stage existed, it is the
    // same arithmetic.
    if (smoothedOutputGain.isSmoothing() || std::abs(smoothedOutputGain.getTargetValue() - 1.0f) > 1.0e-6f)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float gain = smoothedOutputGain.getNextValue();

            for (int channel = 0; channel < numChannels; ++channel)
                buffer.getWritePointer(channel)[sample] *= gain;
        }
    }

    // Only the first block after a reset snaps; after that the smoothing is
    // there to do its real job of keeping a moved dial from clicking.
    snapSmoothers = false;
}

namespace
{
// Out of hearing: sixty decibels down on where it started.
constexpr double inaudible = 0.001;

// Below this a mix dial is off, and whatever it feeds cannot ring.
constexpr float mixFloor = 0.001f;

// The delay is capped rather than reported honestly, and the cap is the whole
// point of this constant. At the top of the feedback dial a repeat loses five
// percent, so it takes 134 of them to fall out of hearing - which at a second
// and a half apiece is over three minutes of tail. That is true, useless, and
// an export nobody would wait through. Eight seconds keeps every ordinary
// setting whole and trims only the last, inaudible part of the most extreme
// one.
constexpr double maxDelayTailSeconds = 8.0;

// Measured through the real chain rather than worked out from the reverb's
// insides: those belong to JUCE, and a number derived from them would drift
// silently the first time JUCE changed anything. MasterFxTest renders an
// impulse at both ends of the Size dial, finds where it falls below hearing,
// and fails if these do not cover it. Measured 0.4s and 2.15s, so these carry
// rather more than double the margin - generous because falling short cuts
// audio and being long only costs a moment of export.
constexpr double reverbTailAtSmallest = 1.0;
constexpr double reverbTailAtLargest = 4.5;
} // namespace

double effectsTailSeconds(const EffectSettings& settings)
{
    double tail = 0.0;

    if (settings.delayMix > mixFloor)
    {
        // The dial is read raw, and deliberately: this is not the time a synced
        // delay actually runs at. With Delay Sync on, the chain snaps to the
        // nearest note division and clamps at two seconds - a 1/1 at 120 bpm -
        // where the dial itself stops at one and a half. So the real gap
        // between repeats can be a third longer than the sum below assumes.
        //
        // Left alone rather than missed. The estimate is already about twice
        // what the chain measures, so a third more sits inside that margin -
        // and the error only appears at the long end of the Time dial, which is
        // exactly where the cap below takes over soonest: it decides the answer
        // above a quarter of the feedback dial at 1.5 seconds, against three
        // quarters at the default 320ms. Where the cap decides, precision here
        // changes nothing at all. Reading sync properly would also mean taking
        // the tempo, and a tail that moved with the transport is a worse thing
        // to hand a host than one that is slightly generous.
        const double feedback = juce::jlimit(0.0, 0.99, (double) settings.delayFeedback);
        const double repeats = feedback > inaudible ? std::log(inaudible) / std::log(feedback) : 1.0;
        const double seconds = (double) settings.delayTimeMs / 1000.0 * repeats;

        tail += juce::jmin(maxDelayTailSeconds, seconds);
    }

    if (settings.reverbMix > mixFloor)
        tail += juce::jmap((double) juce::jlimit(0.0f, 1.0f, settings.reverbSize),
                           0.0, 1.0, reverbTailAtSmallest, reverbTailAtLargest);

    return tail;
}

double tailLengthSeconds(const SynthParameters& params)
{
    // The note itself, which rings whatever the effects are set to, and then
    // whatever the Synth page's delay and reverb add on top of it.
    return juce::jmax(0.0, (double) params.release.load())
           + effectsTailSeconds(effectSettingsFrom(params));
}
} // namespace wavelathe
