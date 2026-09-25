// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DrumFx.h"

#include <cmath>

namespace drums
{
namespace
{
// Where the drive's tone control and the EQ's two corners split the spectrum.
//
// Named, because each one is a claim about drums rather than a number. 3 kHz
// is where a distorted snare stops sounding bright and starts sounding harsh;
// 200 Hz is under the snare and over almost nothing else in a kit, so a low
// shelf there moves the kick without taking the snare with it; 4 kHz is hats
// and the top of everything else.
constexpr float driveToneHz = 3000.0f;
constexpr float eqLowHz = 200.0f;
constexpr float eqHighHz = 4000.0f;

// A one-pole lowpass coefficient. The plain RC form rather than a bilinear
// transform, which at these corner frequencies differ by less than the dial's
// own resolution and cost an extra tan() per update.
float onePoleCoefficient(float hz, double sampleRate)
{
    const auto x = std::exp(-2.0 * juce::MathConstants<double>::pi * (double) hz
                            / juce::jmax(1.0, sampleRate));
    return (float) juce::jlimit(0.0, 0.9999, x);
}
} // namespace

void VoiceFx::prepare(double newSampleRate, int maximumBlockSize)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    tailSamples = (int) (fxTailSeconds * sampleRate);

    reverb.setSampleRate(sampleRate);

    // Two channels always, whatever the output is. A mono host uses the first
    // and leaves the second alone, which costs a buffer nobody reads; sizing
    // to the output's channel count instead would mean reallocating when a
    // host changes its layout, which it may do while running.
    delayLine.setSize(2, juce::jmax(maximumBlockSize, (int) (maxDelaySeconds * sampleRate)),
                      false, true, false);

    reset();
}

void VoiceFx::reset()
{
    reverb.reset();
    builtReverbSize = -1.0f;
    builtReverbDamp = -1.0f;

    toneState.fill(0.0f);
    eqLowState.fill(0.0f);
    eqHighState.fill(0.0f);

    delayLine.clear();
    delayWritePosition = 0;
    delayTimeInitialised = false;

    slotMix.fill(0.0f);
    mixInitialised = false;

    idleSamples = tailSamples + 1;
}

void VoiceFx::process(juce::AudioBuffer<float>& signal, juce::AudioBuffer<float>& wet,
                      int numSamples, const VoiceParameters& voice, const FxParameters& params,
                      bool fed)
{
    if (numSamples <= 0)
        return;

    const int units[2] = { voice.send1.load(), voice.send2.load() };
    const float amounts[2] = { juce::jlimit(0.0f, 1.0f, voice.send1Amount.load()),
                               juce::jlimit(0.0f, 1.0f, voice.send2Amount.load()) };

    const bool inUse = (units[0] > 0 && amounts[0] > 0.0f)
                       || (units[1] > 0 && amounts[1] > 0.0f);

    if (fed && inUse)
        idleSamples = 0;
    else
        idleSamples += numSamples;

    if (isIdle())
    {
        // Past its tail with nothing playing through it. Reset rather than
        // merely skipped: a reverb left holding three seconds of an old hit
        // would play it back the moment somebody turned an Amount up again,
        // which is a burst of audio from a bar that has long gone.
        if (mixInitialised)
            reset();

        return;
    }

    if (!mixInitialised)
    {
        slotMix[0] = amounts[0];
        slotMix[1] = amounts[1];
        mixInitialised = true;
    }

    // In order, and the order is the whole point: slot 2 is handed what slot 1
    // produced, not a second copy of the dry voice.
    runSlot(0, units[0], amounts[0], signal, wet, numSamples, params);
    runSlot(1, units[1], amounts[1], signal, wet, numSamples, params);
}

void VoiceFx::runSlot(int slot, int unit, float amount, juce::AudioBuffer<float>& signal,
                      juce::AudioBuffer<float>& wet, int numSamples, const FxParameters& params)
{
    const auto current = slotMix[(size_t) slot];

    // Nothing selected, or the dial is at zero and was already there. The
    // second half of that matters: a mix that has only just been turned down
    // still has to glide there, or switching a slot off clicks.
    if (unit <= 0 || unit > numFxUnits || (amount == 0.0f && current == 0.0f))
    {
        slotMix[(size_t) slot] = amount;
        return;
    }

    const auto numChannels = juce::jmin(signal.getNumChannels(), wet.getNumChannels());

    for (int channel = 0; channel < numChannels; ++channel)
        wet.copyFrom(channel, 0, signal, channel, 0, numSamples);

    switch (unit)
    {
        case 1: processReverb(wet, numSamples, params); break;
        case 2: processDrive(wet, numSamples, params); break;
        case 3: processEq(wet, numSamples, params); break;
        case 4: processDelay(wet, numSamples, params); break;
        default: break;
    }

    // out = in + (wet - in) * mix, which at a mix of exactly zero is `in`
    // itself down to the last bit rather than something very close to it.
    // That exactness is what the "an unused chain changes nothing" test
    // depends on, and it costs nothing to have.
    const auto step = (amount - current) / (float) numSamples;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* dry = signal.getWritePointer(channel);
        const auto* effected = wet.getReadPointer(channel);
        auto mix = current;

        for (int i = 0; i < numSamples; ++i)
        {
            dry[i] = dry[i] + (effected[i] - dry[i]) * mix;
            mix += step;
        }
    }

    slotMix[(size_t) slot] = amount;
}

void VoiceFx::processReverb(juce::AudioBuffer<float>& bus, int numSamples, const FxParameters& params)
{
    const auto size = juce::jlimit(0.0f, 1.0f, params.reverbSize.load());
    const auto damp = juce::jlimit(0.0f, 1.0f, params.reverbDamp.load());

    // Rebuilt only when a dial has actually moved. juce::Reverb recomputes
    // every comb and allpass when its parameters are set, which is not free
    // and which happens on the audio thread - twelve times over, here.
    if (size != builtReverbSize || damp != builtReverbDamp)
    {
        // The dial's ends are not the algorithm's ends. juce::Reverb at a room
        // size of 1 is effectively frozen - a hit played through it is still
        // as loud three seconds later as it went in, which is not a large
        // room, it is a stuck one. 0.92 is the largest size that still decays,
        // and 0.2 is small enough to read as a room rather than as no reverb.
        reverbParameters.roomSize = 0.2f + size * 0.72f;
        reverbParameters.damping = damp;

        // Fully wet, always. The dry path is the caller's `signal` and the mix
        // between the two is the slot's Amount, so a second dry copy in here
        // would be the same signal arriving twice.
        reverbParameters.wetLevel = 1.0f;
        reverbParameters.dryLevel = 0.0f;
        reverbParameters.width = 1.0f;
        reverbParameters.freezeMode = 0.0f;

        reverb.setParameters(reverbParameters);
        builtReverbSize = size;
        builtReverbDamp = damp;
    }

    if (bus.getNumChannels() > 1)
        reverb.processStereo(bus.getWritePointer(0), bus.getWritePointer(1), numSamples);
    else
        reverb.processMono(bus.getWritePointer(0), numSamples);

    bus.applyGain(0, numSamples, juce::jlimit(0.0f, 1.0f, params.reverbLevel.load()));
}

void VoiceFx::processDrive(juce::AudioBuffer<float>& bus, int numSamples, const FxParameters& params)
{
    const auto amount = juce::jlimit(0.0f, 1.0f, params.driveAmount.load());
    const auto tone = juce::jlimit(0.0f, 1.0f, params.driveTone.load());

    // One at the bottom of the dial and twenty-five at the top. Exponential
    // rather than linear because the audible difference between 1x and 2x is
    // the whole of what a drive dial's first third should be spending itself
    // on, and between 20x and 21x there is none.
    const auto gain = std::pow(25.0f, amount);

    // tanh soft-clips and saturates without the odd-harmonic edge of a hard
    // clip, and its own gain at small signals is 1 - so the compensation below
    // only has to undo the drive, not the shaper.
    const auto makeup = 1.0f / std::sqrt(gain);

    // The tone dial sweeps the corner rather than mixing two paths: fully open
    // is no filtering at all, which is what somebody expects from a tone
    // control at the top of its travel and not what a fixed corner gives.
    const auto coefficient = tone >= 0.999f ? 0.0f
                                            : onePoleCoefficient(driveToneHz * std::pow(8.0f, tone - 0.5f),
                                                                 sampleRate);

    const auto numChannels = juce::jmin(2, bus.getNumChannels());

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* data = bus.getWritePointer(channel);
        auto state = toneState[(size_t) channel];

        for (int i = 0; i < numSamples; ++i)
        {
            auto value = std::tanh(data[i] * gain) * makeup;

            state = value + coefficient * (state - value);
            data[i] = state;
        }

        toneState[(size_t) channel] = state;
    }

    bus.applyGain(0, numSamples, juce::jlimit(0.0f, 1.0f, params.driveLevel.load()));
}

void VoiceFx::processEq(juce::AudioBuffer<float>& bus, int numSamples, const FxParameters& params)
{
    const auto lowGain = juce::jlimit(0.0f, 4.0f, params.eqLowGain.load());
    const auto highGain = juce::jlimit(0.0f, 4.0f, params.eqHighGain.load());

    const auto lowCoefficient = onePoleCoefficient(eqLowHz, sampleRate);
    const auto highCoefficient = onePoleCoefficient(eqHighHz, sampleRate);

    const auto numChannels = juce::jmin(2, bus.getNumChannels());

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* data = bus.getWritePointer(channel);
        auto lowState = eqLowState[(size_t) channel];
        auto highState = eqHighState[(size_t) channel];

        for (int i = 0; i < numSamples; ++i)
        {
            const auto input = data[i];

            // Three bands out of two one-poles: what is below 200 Hz, what is
            // above 4 kHz, and whatever is left in between. The middle is the
            // remainder rather than a band of its own, so the three always sum
            // back to the input exactly when both gains are 1 - which is what
            // makes "flat" mean flat rather than nearly flat.
            lowState = input + lowCoefficient * (lowState - input);
            highState = input + highCoefficient * (highState - input);

            const auto low = lowState;
            const auto high = input - highState;
            const auto mid = input - low - high;

            data[i] = low * lowGain + mid + high * highGain;
        }

        eqLowState[(size_t) channel] = lowState;
        eqHighState[(size_t) channel] = highState;
    }

    bus.applyGain(0, numSamples, juce::jlimit(0.0f, 1.0f, params.eqLevel.load()));
}

void VoiceFx::processDelay(juce::AudioBuffer<float>& bus, int numSamples, const FxParameters& params)
{
    const auto lineLength = delayLine.getNumSamples();

    if (lineLength <= 1)
        return;

    const auto seconds = juce::jlimit(0.001, maxDelaySeconds, (double) params.delaySeconds.load());
    const auto target = juce::jlimit(1.0f, (float) (lineLength - 2), (float) (seconds * sampleRate));

    if (!delayTimeInitialised)
    {
        smoothedDelaySamples = target;
        delayTimeInitialised = true;
    }

    // Reaches a new time in about a tenth of a second, which is slow enough
    // not to click and fast enough that changing the division mid-bar lands
    // before the next hit.
    const auto glide = (float) juce::jlimit(0.0, 1.0, 1.0 - std::exp(-1.0 / (0.1 * sampleRate)));

    const auto feedback = juce::jlimit(0.0f, 0.95f, params.delayFeedback.load());
    const auto numChannels = juce::jmin(2, bus.getNumChannels());

    float* lines[2] = { delayLine.getWritePointer(0), nullptr };
    float* channels[2] = { bus.getWritePointer(0), nullptr };

    if (numChannels > 1)
    {
        lines[1] = delayLine.getWritePointer(1);
        channels[1] = bus.getWritePointer(1);
    }

    auto write = delayWritePosition;
    auto delaySamples = smoothedDelaySamples;

    for (int i = 0; i < numSamples; ++i)
    {
        delaySamples += (target - delaySamples) * glide;

        const auto readPosition = (float) write - delaySamples + (float) lineLength;
        const auto index = (int) readPosition;
        const auto fraction = readPosition - (float) index;

        const auto a = index % lineLength;
        const auto b = (index + 1) % lineLength;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* line = lines[channel];
            const auto out = line[a] + (line[b] - line[a]) * fraction;

            // Read before write, and the order matters: at the shortest delay
            // the read and write heads are a sample apart, so writing first
            // would feed this sample straight back into itself.
            line[write] = channels[channel][i] + out * feedback;
            channels[channel][i] = out;
        }

        write = (write + 1) % lineLength;
    }

    smoothedDelaySamples = delaySamples;
    delayWritePosition = write;

    bus.applyGain(0, numSamples, juce::jlimit(0.0f, 1.0f, params.delayLevel.load()));
}
} // namespace drums
