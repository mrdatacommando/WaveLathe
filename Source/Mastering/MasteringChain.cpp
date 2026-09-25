// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MasteringChain.h"

#include <algorithm>
#include <cmath>

namespace mastering
{
namespace
{
// How long a moved dial takes to arrive. Long enough that a jump cannot click,
// short enough that a deliberate move still feels immediate.
constexpr double smoothingSeconds = 0.02;

// Raise a peak meter to the loudest thing in this span, without a lock. The
// audio thread is the only writer, but the UI reads it on its own thread and a
// torn float would show as a spike that never happened.
void trackPeak(std::atomic<float>& slot, const juce::AudioBuffer<float>& buffer,
               int startSample, int numSamples)
{
    const float magnitude = buffer.getMagnitude(startSample, numSamples);

    float current = slot.load(std::memory_order_relaxed);
    while (magnitude > current
           && !slot.compare_exchange_weak(current, magnitude, std::memory_order_relaxed))
    {
        // compare_exchange_weak writes the value it found back into current,
        // so the condition is re-tested against whatever won the race.
    }
}

float takeAndClear(std::atomic<float>& slot)
{
    return slot.exchange(0.0f, std::memory_order_relaxed);
}
} // namespace

void Chain::prepare(double newSampleRate, int newMaximumBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    maximumBlockSize = juce::jmax(1, newMaximumBlockSize);
    numChannels = juce::jmax(1, newNumChannels);

    for (auto* smoother : {&smoothedDrive, &smoothedSaturationMix, &smoothedOutputGain})
        smoother->reset(sampleRate, smoothingSeconds);

    // Polyphase IIR rather than the equiripple FIR.
    //
    // The FIR is linear phase, which sounds like the better choice for a
    // mastering stage until you ask what is being mastered. Linear phase costs
    // several times the latency and rings BEFORE a transient, which is a thing
    // no analogue circuit has ever done - and this chain exists to sound more
    // analogue, not less. The IIR is minimum phase, like the hardware it is
    // imitating, and its latency is small enough to play through.
    //
    // useIntegerLatency is the last argument, and it is what makes the number
    // this reports a whole one. Without it the latency is fractional, a host
    // can only be told an integer, and the difference is a permanent sub-sample
    // offset nobody would ever find.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        (size_t) numChannels,
        (size_t) oversamplingStages,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,  // isMaxQuality
        true); // useIntegerLatency

    oversampler->initProcessing((size_t) maximumBlockSize);

    // The limiter's lookahead, in whole samples. At 44100 this is 66.
    limiterLookaheadSamples = juce::jmax(1, (int) std::lround(limiterLookaheadSeconds * sampleRate));
    limiterDelayLine.setSize(numChannels, limiterLookaheadSamples);

    // One longer than the delay, and the extra entry is not an off-by-one.
    //
    // At each step the newest requirement is written and the oldest falls out.
    // The sample being played at that moment is the one that went into the
    // delay line a full lookahead ago, so its own requirement has to still be
    // in the window when the window is consulted. At exactly the lookahead
    // length it has just been overwritten, and the one sample the limiter most
    // needed to know about is the one it cannot see.
    limiterRequiredGain.assign((size_t) limiterLookaheadSamples + 1, 1.0f);

    // Both costs, summed, because the host needs the total and neither half is
    // meaningful on its own.
    latencySamples = (int) std::lround((double) oversampler->getLatencyInSamples())
                     + limiterLookaheadSamples;

    // Exactly as long as the latency, so reading an entry just before it is
    // overwritten gives back the sample from latencySamples ago.
    bypassDelayLine.setSize(numChannels, juce::jmax(1, latencySamples));

    prepared = true;
    reset();
}

void Chain::reset()
{
    smoothedDrive.setCurrentAndTargetValue(0.0f);
    smoothedSaturationMix.setCurrentAndTargetValue(1.0f);
    smoothedOutputGain.setCurrentAndTargetValue(1.0f);

    if (oversampler != nullptr)
        oversampler->reset();

    compressorEnvelopeDb = 0.0f;
    limiterGain = 1.0f;
    limiterDelayIndex = 0;
    limiterRequiredIndex = 0;
    limiterDelayLine.clear();
    std::fill(limiterRequiredGain.begin(), limiterRequiredGain.end(), 1.0f);

    bypassDelayLine.clear();
    bypassDelayIndex = 0;

    // Forced back to an impossible value so the coefficients are rebuilt from
    // whatever the first block asks for, rather than kept from a previous run
    // at a different sample rate.
    builtAttackMs = -1.0f;
    builtReleaseMs = -1.0f;
    builtLimiterReleaseMs = -1.0f;

    // The next block snaps to whatever it is given rather than gliding from
    // these. See the comment on the member: an offline render that depends on
    // what was rendered before it cannot be compared against itself.
    snapSmoothers = true;

    inputPeak.store(0.0f, std::memory_order_relaxed);
    outputPeak.store(0.0f, std::memory_order_relaxed);
    gainReductionDb.store(0.0f, std::memory_order_relaxed);
}

void Chain::process(juce::AudioBuffer<float>& buffer, const Parameters& params)
{
    if (!prepared || oversampler == nullptr)
        return;

    // The oversampler is built for a fixed channel count and cannot be rebuilt
    // here - allocating on the audio thread is how a plugin drops out. Handing
    // it fewer channels than it expects would read past the end of the block it
    // was given, so a caller that has not honoured prepare() gets nothing
    // rather than something dangerous.
    if (buffer.getNumChannels() < numChannels)
        return;

    int remaining = buffer.getNumSamples();
    int startSample = 0;

    while (remaining > 0)
    {
        const int thisTime = juce::jmin(remaining, maximumBlockSize);
        processChunk(buffer, startSample, thisTime, params);
        startSample += thisTime;
        remaining -= thisTime;
    }
}

void Chain::processChunk(juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                         const Parameters& params)
{
    if (numSamples <= 0)
        return;

    // Measured whatever happens next, including when the chain is bypassed -
    // which is exactly when somebody is looking at the meter to decide whether
    // to switch it back on.
    trackPeak(inputPeak, buffer, startSample, numSamples);

    // A hard bypass, so it is EXACTLY transparent rather than nearly so - every
    // sample comes back bit for bit, just latencySamples later. That is what
    // lets this be dropped into a signal path and trusted not to have coloured
    // anything, which is the only honest way to A/B it.
    //
    // LATE, and that is the point. A host is told once what this chain costs
    // and keeps compensating by it whether the chain is working or not, so the
    // bypassed audio has to be exactly that late too. It used to come straight
    // back instead, and the track played early by the whole latency - about a
    // millisecond and a half - with mastering off, which is the default. The
    // reported number was right; the audio was not keeping to it.
    //
    // Two known costs, both deliberate. It will click if switched under a loud
    // signal; a crossfade is the fix and cannot be bit-transparent, so it waits
    // until there is something worth trading that property for. And the
    // oversampler's filters go stale while bypassed, so the first block after
    // switching back carries a very short settling transient.
    const bool bypassed = params.bypass.load() != 0.0f;

    // The input goes into the bypass line on every block, working or not - see
    // bypassDelayLine for why - and replaces the block only when bypassed.
    // Read before write at the same index, which in a line exactly
    // latencySamples long is the sample from exactly that long ago.
    {
        const int length = bypassDelayLine.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* data = buffer.getWritePointer(channel, startSample);
                const float delayed = bypassDelayLine.getSample(channel, bypassDelayIndex);
                bypassDelayLine.setSample(channel, bypassDelayIndex, data[i]);

                if (bypassed)
                    data[i] = delayed;
            }

            bypassDelayIndex = (bypassDelayIndex + 1) % length;
        }
    }

    if (bypassed)
    {
        gainReductionDb.store(0.0f, std::memory_order_relaxed);
        trackPeak(outputPeak, buffer, startSample, numSamples);
        return;
    }

    const float driveTarget = juce::jlimit(0.0f, 1.0f, params.saturationDrive.load());
    const float mixTarget = juce::jlimit(0.0f, 1.0f, params.saturationMix.load());
    const float gainTarget =
        juce::Decibels::decibelsToGain(juce::jlimit(-24.0f, 24.0f, params.outputGainDb.load()));

    if (snapSmoothers)
    {
        smoothedDrive.setCurrentAndTargetValue(driveTarget);
        smoothedSaturationMix.setCurrentAndTargetValue(mixTarget);
        smoothedOutputGain.setCurrentAndTargetValue(gainTarget);
        snapSmoothers = false;
    }
    else
    {
        smoothedDrive.setTargetValue(driveTarget);
        smoothedSaturationMix.setTargetValue(mixTarget);
        smoothedOutputGain.setTargetValue(gainTarget);
    }

    // ---- Saturation, at four times the rate ---------------------------------

    juce::dsp::AudioBlock<float> whole(buffer);
    auto chunk = whole.getSubsetChannelBlock(0, (size_t) numChannels)
                      .getSubBlock((size_t) startSample, (size_t) numSamples);

    auto upsampled = oversampler->processSamplesUp(chunk);

    const int upsampledLength = (int) upsampled.getNumSamples();

    for (int n = 0; n < numSamples; ++n)
    {
        // Advanced once per BASE-rate sample, outside the inner loops, for two
        // reasons. Both sides of a stereo pair must be shaped by the same
        // value, and a ramp that stepped once per oversampled sample would
        // finish four times too early.
        const float drive = smoothedDrive.getNextValue();
        const float mix = smoothedSaturationMix.getNextValue();

        for (int step = 0; step < oversamplingFactor; ++step)
        {
            const int index = n * oversamplingFactor + step;
            if (index >= upsampledLength)
                break;

            for (int channel = 0; channel < numChannels; ++channel)
            {
                const float dry = upsampled.getSample(channel, index);

                // Drive is how hard it is pushed; mix is how much of the result
                // is heard. Both, because parallel saturation - a heavily
                // shaped copy sitting under an untouched one - is a different
                // and often better sound than the same amount applied to
                // everything.
                //
                // The blend happens HERE, inside the oversampled domain, rather
                // than around the whole stage. Mixing a dry signal from outside
                // against a wet one that has been through the oversampler would
                // comb: the wet side is delayed by the filters and the dry side
                // is not. Blending both sides of the same delayed signal cannot
                // misalign, whatever the latency turns out to be.
                const float shaped = saturateSample(dry, drive);
                upsampled.setSample(channel, index, dry + mix * (shaped - dry));
            }
        }
    }

    oversampler->processSamplesDown(chunk);

    // ---- Back at base rate --------------------------------------------------

    // Neither dynamics stage is oversampled, and that is on purpose. They change
    // gain rather than shape, so they generate very little that could fold back,
    // and running them at four times the rate would buy almost nothing.

    const float thresholdDb = juce::jlimit(-48.0f, 0.0f, params.compressorThresholdDb.load());
    const float ratio = juce::jlimit(1.0f, 20.0f, params.compressorRatio.load());
    const float makeupDb = juce::jlimit(0.0f, 24.0f, params.compressorMakeupDb.load());
    const float compressorMix = juce::jlimit(0.0f, 1.0f, params.compressorMix.load());

    // Rebuilt only when the dial has actually moved. exp() per block is
    // nothing; exp() per sample for a number that changes once a minute is
    // work done for its own sake.
    const float attackMs = juce::jlimit(0.1f, 100.0f, params.compressorAttackMs.load());
    if (attackMs != builtAttackMs)
    {
        compressorAttackCoefficient = timeToCoefficient(attackMs);
        builtAttackMs = attackMs;
    }

    const float releaseMs = juce::jlimit(10.0f, 1000.0f, params.compressorReleaseMs.load());
    if (releaseMs != builtReleaseMs)
    {
        compressorReleaseCoefficient = timeToCoefficient(releaseMs);
        builtReleaseMs = releaseMs;
    }

    const float ceiling =
        juce::Decibels::decibelsToGain(juce::jlimit(-12.0f, 0.0f, params.limiterCeilingDb.load()));

    const float limiterReleaseMs = juce::jlimit(1.0f, 500.0f, params.limiterReleaseMs.load());
    if (limiterReleaseMs != builtLimiterReleaseMs)
    {
        limiterReleaseCoefficient = timeToCoefficient(limiterReleaseMs);
        builtLimiterReleaseMs = limiterReleaseMs;
    }

    // The most the limiter's gain may fall in one sample. One over the
    // lookahead, so the whole range from unity to silence can be crossed in
    // exactly the time the limiter gets to see a peak coming. That is what
    // makes the ceiling a guarantee rather than a hope: however savage the
    // peak, there is always enough runway to be ready for it.
    const float maximumGainStep = 1.0f / (float) limiterLookaheadSamples;

    const int requiredWindow = (int) limiterRequiredGain.size();

    for (int n = 0; n < numSamples; ++n)
    {
        const float outputGain = smoothedOutputGain.getNextValue();

        // ---- Compressor ----------------------------------------------------
        // Stereo-linked: one detector reading the louder side, one gain applied
        // to both. Two independent detectors would pull the image towards
        // whichever channel happened to be quieter at the time, which on a mix
        // bus is heard as the stereo picture wandering.
        float detector = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            detector = juce::jmax(detector, std::abs(buffer.getSample(channel, startSample + n)));

        const float levelDb = juce::Decibels::gainToDecibels(detector, -120.0f);
        const float wantedDb = compressorReductionDb(levelDb, thresholdDb, ratio);

        // Attack when more reduction is called for, release when less. The
        // comparison is against where the envelope IS, not against the
        // threshold, so a signal that stays put uses neither.
        const float coefficient = wantedDb > compressorEnvelopeDb ? compressorAttackCoefficient
                                                                 : compressorReleaseCoefficient;
        compressorEnvelopeDb = coefficient * compressorEnvelopeDb + (1.0f - coefficient) * wantedDb;

        const float compressorGain = juce::Decibels::decibelsToGain(makeupDb - compressorEnvelopeDb);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float dry = buffer.getSample(channel, startSample + n);

            // Makeup rides with the compressed copy rather than the blend, so
            // turning the mix down turns down the made-up-for signal too. The
            // alternative quietly makes parallel compression a volume control.
            const float wet = dry * compressorGain;

            buffer.setSample(channel, startSample + n,
                             (dry + compressorMix * (wet - dry)) * outputGain);
        }

        // ---- Limiter -------------------------------------------------------
        // What this sample would need, recorded now and acted on a lookahead
        // later, when it finally comes out of the delay line.
        float limiterDetector = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            limiterDetector =
                juce::jmax(limiterDetector, std::abs(buffer.getSample(channel, startSample + n)));

        limiterRequiredGain[(size_t) limiterRequiredIndex] =
            limiterDetector > ceiling ? ceiling / limiterDetector : 1.0f;
        limiterRequiredIndex = (limiterRequiredIndex + 1) % requiredWindow;

        // The worst thing anywhere in the window, scanned in full. Linear in
        // the lookahead, which at 66 samples is a few dozen comparisons - real
        // work, but this runs once on a mix bus rather than once per voice, and
        // a sliding-window minimum that is clever enough to avoid it is a data
        // structure nobody would enjoy debugging at three in the morning.
        float target = 1.0f;
        for (float required : limiterRequiredGain)
            target = juce::jmin(target, required);

        if (target < limiterGain)
            limiterGain = juce::jmax(target, limiterGain - maximumGainStep);
        else
            limiterGain += (1.0f - limiterReleaseCoefficient) * (target - limiterGain);

        // Read before write, so what comes out is what went in a lookahead ago.
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float incoming = buffer.getSample(channel, startSample + n);
            const float delayed = limiterDelayLine.getSample(channel, limiterDelayIndex);
            limiterDelayLine.setSample(channel, limiterDelayIndex, incoming);
            buffer.setSample(channel, startSample + n, delayed * limiterGain);
        }

        limiterDelayIndex = (limiterDelayIndex + 1) % limiterLookaheadSamples;
    }

    gainReductionDb.store(compressorEnvelopeDb, std::memory_order_relaxed);
    limiterReductionDb.store(-juce::Decibels::gainToDecibels(limiterGain, -60.0f),
                             std::memory_order_relaxed);

    trackPeak(outputPeak, buffer, startSample, numSamples);
}

float Chain::saturateSample(float x, float drive)
{
    if (drive <= 0.0f)
        return x;

    // The same shape as SynthVoice::softClip and MasterEffects::softClip, at a
    // gain between the two. Three stages in this project saturate; they should
    // be relatives rather than three unrelated curves that happen to share a
    // name.
    //
    // Dividing by tanh(gain) keeps the loudest part of the signal where it
    // started, so turning drive up changes the tone without also changing the
    // level - which is what makes it possible to judge by ear.
    const float gain = 1.0f + drive * 9.0f;
    const float shaped = std::tanh(x * gain) / std::tanh(gain);
    return x + drive * (shaped - x);
}

float Chain::compressorReductionDb(float levelDb, float thresholdDb, float ratio)
{
    // A ratio of one is not compression at all. Saying so here means the rest
    // of the arithmetic never has to divide by anything awkward.
    if (ratio <= 1.0f)
        return 0.0f;

    const float slope = 1.0f - 1.0f / ratio;
    const float over = levelDb - thresholdDb;
    const float halfKnee = compressorKneeDb * 0.5f;

    if (over <= -halfKnee)
        return 0.0f;

    if (over >= halfKnee)
        return slope * over;

    // Through the knee, a quadratic that leaves the flat part and meets the
    // straight part with the same slope at each end. Anything simpler - a
    // straight line across the gap, say - joins with a corner, and a corner in
    // a gain curve is heard as the compressor switching on.
    const float t = over + halfKnee;
    return slope * t * t / (2.0f * compressorKneeDb);
}

float Chain::timeToCoefficient(float milliseconds) const
{
    const double seconds = (double) milliseconds * 0.001;

    if (seconds <= 0.0)
        return 0.0f; // immediately, rather than a division by zero

    return (float) std::exp(-1.0 / (seconds * sampleRate));
}

float Chain::takeInputPeak() { return takeAndClear(inputPeak); }
float Chain::takeOutputPeak() { return takeAndClear(outputPeak); }

float Chain::getGainReductionDb() const
{
    return gainReductionDb.load(std::memory_order_relaxed);
}

float Chain::getLimiterReductionDb() const
{
    return limiterReductionDb.load(std::memory_order_relaxed);
}
} // namespace mastering
