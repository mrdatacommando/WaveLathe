// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include "MasteringParameters.h"

namespace mastering
{
// Saturation -> compressor -> limiter, applied to a finished mix.
//
// The order is the conventional one and each step depends on the one before
// it. Saturation is a tone decision, so it comes first and the compressor then
// controls what will actually be heard rather than something that no longer
// exists by the output. The limiter is last because it is a catch, not a sound:
// anything placed after it could push the signal back over the ceiling it just
// spent effort holding.
//
// Nothing here knows what is feeding it. That is the point - the same chain
// runs inside the synth's Master tab and inside an effect plugin on a mix bus,
// and neither arrangement is privileged.
//
// Output gain sits between the compressor and the limiter rather than at the
// end, which is the only place it can go. Anything after the limiter could push
// the signal back over the ceiling the limiter just spent effort holding, and
// then the ceiling means nothing. In practice this makes it the limiter's input
// trim, which is how every mastering limiter is actually driven.
class Chain
{
public:
    // Two stages, so four times the sample rate.
    //
    // Two is not enough for a waveshaper. Doubling only moves the point where
    // harmonics start folding back to twice Nyquist, and tanh at any real drive
    // is still making plenty above that. Four puts the fold-back far enough up
    // that the halfband filter removes it instead of the signal keeping it.
    // Eight costs twice as much again for a difference that needs a spectrum
    // analyser to find.
    static constexpr int oversamplingStages = 2;
    static constexpr int oversamplingFactor = 1 << oversamplingStages;

    // A fixed soft knee rather than a dial. Six decibels, so compression begins
    // three below the threshold and is in full effect three above it, which is
    // gentle enough for a mix bus and still recognisably a threshold.
    //
    // Not a parameter because nobody reaches for a knee control on a mastering
    // compressor in order to set it somewhere surprising, and this project has
    // already decided once - in 0.38.6, over the clock smoothing switch - that
    // a control with one sensible position is worse than no control at all.
    static constexpr float compressorKneeDb = 6.0f;

    // How far ahead the limiter sees, and therefore what it costs.
    //
    // 1.5 ms is long enough to walk the gain down before a transient arrives
    // rather than clamping on top of it, and short enough to stay playable. It
    // is what makes this a limiter rather than a clipper: a clipper would hold
    // the ceiling by generating exactly the kind of harmonics the oversampling
    // in the stage above exists to prevent, at the one point in the chain where
    // there is nothing left to catch them.
    static constexpr double limiterLookaheadSeconds = 0.0015;

    void prepare(double sampleRate, int maximumBlockSize, int numChannels);
    void reset();

    // Any length of buffer is accepted: anything longer than the block size
    // this was prepared for is split internally.
    //
    // Not a convenience. MasterEffects in this same project was caught out by
    // exactly this - an offline render hands over a whole note in one call, far
    // longer than the block size a live host ever uses, and a caller has no way
    // to know what length is safe. Handling it here means the answer is "any".
    //
    // The buffer must have at least as many channels as prepare() was told
    // about. Fewer is a caller error and nothing is processed: the oversampler
    // is built for a fixed channel count and cannot be rebuilt from the audio
    // thread, so quietly processing a subset would silently drop a channel.
    void process(juce::AudioBuffer<float>& buffer, const Parameters& params);

    // ---- Metering ----------------------------------------------------------
    // Peaks are taken and cleared, so a meter reads the loudest thing since it
    // last looked rather than whatever happened to be there on the tick. Same
    // arrangement as OutputTap, for the same reason: a UI polling at 30 Hz over
    // a signal at 44100 Hz otherwise samples almost none of it.
    float takeInputPeak();
    float takeOutputPeak();

    // How hard the compressor is working right now, as a positive number of
    // decibels. Not take-and-clear: this is a level to be shown, not an event
    // to be caught, and a gain reduction meter that reset itself on every read
    // would flicker rather than move.
    float getGainReductionDb() const;

    // What the limiter is doing, kept separate from the compressor's figure.
    //
    // Summing them would read as one number that is sometimes the compressor
    // working and sometimes the limiter rescuing a mix that is too hot, and
    // those two want opposite responses from whoever is looking. A limiter that
    // never moves is a chain with headroom; a compressor that never moves is a
    // threshold set too high.
    float getLimiterReductionDb() const;

    // ---- Latency -----------------------------------------------------------
    // What the oversampler costs, and it must reach the host. A plugin that
    // does not report its latency drifts against everything else on the
    // timeline - silently, and the sequencer would get the blame for a lateness
    // it did not cause.
    //
    // Two things cost latency and this is their sum: the oversampler's filters
    // and the limiter's lookahead, the second being much the larger.
    //
    // A WHOLE number of samples. The oversampler is built with
    // useIntegerLatency so its own share is already integer, and the lookahead
    // is a whole number of samples by construction. Hosts can only be told an
    // integer, and rounding one that was not already integer would leave the
    // signal off by the remainder.
    //
    // Constant. It does NOT change with drive, which is the reason the dry and
    // wet sides of the saturation blend are mixed inside the oversampled domain
    // rather than around it. Latency that moved when a dial moved would make
    // the host resynchronise mid-performance.
    int getLatencySamples() const { return latencySamples; }

private:
    void processChunk(juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                      const Parameters& params);

    // One sample through the waveshaper. The same tanh family the synth's drive
    // and the master distortion already use, so the three stages of this
    // project that saturate all sound related rather than merely all being
    // called drive.
    //
    // Called at FOUR TIMES the sample rate. That is the whole point of this
    // step: tanh generates harmonics without limit, and at base rate everything
    // above Nyquist folded back down into the audible band as inharmonic
    // rubbish. The curve has not changed. Where it is evaluated has.
    static float saturateSample(float x, float drive);

    // How many decibels the compressor wants to remove at this input level,
    // given where the threshold is and how hard the ratio is. Pure arithmetic
    // on one number, separate from the envelope that decides how fast that
    // figure is allowed to be reached - the two get confused constantly and
    // they are not the same thing.
    static float compressorReductionDb(float levelDb, float thresholdDb, float ratio);

    // exp(-1 / (seconds * rate)), the usual one-pole coefficient, with the
    // degenerate cases handled. A time of zero means "immediately", which as a
    // coefficient is 0 rather than a division by zero.
    float timeToCoefficient(float milliseconds) const;

    double sampleRate = 44100.0;
    int maximumBlockSize = 512;
    int numChannels = 2;
    bool prepared = false;
    int latencySamples = 0;

    // Rebuilt by prepare() and never touched from the audio thread. Held by
    // pointer because its channel count and block size are constructor
    // arguments, so changing either means a new one.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    // Smoothed per SAMPLE, not per block. Per block would make the result
    // depend on how the caller happened to chop the buffer up, and a render
    // that changes when the block size changes is a render that cannot be
    // compared against itself.
    //
    // Drive and mix advance once per BASE-rate sample even though they are used
    // four times as often, so the ramp takes the same wall-clock time it would
    // have taken without any of this.
    juce::SmoothedValue<float> smoothedDrive, smoothedSaturationMix, smoothedOutputGain;

    // Set by reset(), cleared by the first block after it. Without it the first
    // block of a render glides from whatever the previous render left behind,
    // which makes an offline bounce depend on what was bounced before it.
    // Inaudible live; fatal to a test that renders the same thing twice.
    bool snapSmoothers = true;

    // ---- Compressor state --------------------------------------------------
    // The envelope is kept in DECIBELS rather than as a gain multiplier.
    // Smoothing a multiplier makes the attack and release curves depend on how
    // much reduction is happening, so the same release time sounds different at
    // 3 dB and at 15 dB. In dB it behaves the same at any depth, which is what
    // the numbers on a compressor have always implied.
    float compressorEnvelopeDb = 0.0f;
    float compressorAttackCoefficient = 0.0f;
    float compressorReleaseCoefficient = 0.0f;
    // What the coefficients above were built from, so they are recomputed when
    // the dial moves and not on every block.
    float builtAttackMs = -1.0f;
    float builtReleaseMs = -1.0f;

    // ---- Limiter state -----------------------------------------------------
    // The audio is delayed by the lookahead while the gain is worked out from
    // the signal that has not been delayed. That is the whole trick: by the
    // time a peak is audible, the gain that tames it arrived a millisecond and
    // a half ago.
    int limiterLookaheadSamples = 0;
    juce::AudioBuffer<float> limiterDelayLine;
    int limiterDelayIndex = 0;

    // What bypass plays: the input, exactly latencySamples late.
    //
    // A bypassed chain used to hand the audio straight back, while still
    // reporting the latency it costs when working - so a host compensating
    // for that latency played the track early by it, and with mastering off
    // by default, that was everybody. Now bypass is late by exactly what the
    // chain says, and the number the host was given is true in both states.
    //
    // Fed with the input on EVERY block, working or not, so that switching
    // bypass on carries straight on from the last few samples that went in -
    // rather than replaying whatever was left in here from the last time it
    // was bypassed, which could be minutes stale.
    juce::AudioBuffer<float> bypassDelayLine;
    int bypassDelayIndex = 0;

    // One required-gain figure per sample of lookahead. The limiter's target is
    // the smallest of them - the worst thing anywhere in the window it can see.
    std::vector<float> limiterRequiredGain;
    int limiterRequiredIndex = 0;

    float limiterGain = 1.0f;
    float limiterReleaseCoefficient = 0.0f;
    float builtLimiterReleaseMs = -1.0f;

    std::atomic<float> inputPeak{0.0f};
    std::atomic<float> outputPeak{0.0f};
    std::atomic<float> gainReductionDb{0.0f};
    std::atomic<float> limiterReductionDb{0.0f};
};
} // namespace mastering
