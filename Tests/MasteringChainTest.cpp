// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Mastering/MasteringChain.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace mastering;

namespace
{
int failures = 0;

void check(bool condition, const juce::String& what)
{
    if (!condition)
    {
        std::cout << "  FAIL: " << what << std::endl;
        ++failures;
    }
}

constexpr double sampleRate = 44100.0;

// What the chain is told to expect. A live host's block size, so that the long
// buffers an offline render hands over are genuinely longer than the chain was
// prepared for - the case that has already caught this project's other effect
// chain out once.
constexpr int preparedBlock = 512;

// Samples ignored at the start of a comparison. The oversampler's halfband
// filters begin empty and take a moment to fill; what they do while filling is
// real and correct, it is simply not the steady state being measured.
constexpr int settleSamples = 256;

juce::AudioBuffer<float> makeSine(double hz, int numSamples, float amplitude = 0.5f)
{
    juce::AudioBuffer<float> buffer(2, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto value =
            (float) (amplitude * std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / sampleRate));
        buffer.setSample(0, i, value);
        buffer.setSample(1, i, value);
    }

    return buffer;
}

bool identical(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;

    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (a.getSample(channel, i) != b.getSample(channel, i))
                return false;

    return true;
}

bool allFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (!std::isfinite(buffer.getSample(channel, i)))
                return false;

    return true;
}

double rmsOf(const juce::AudioBuffer<float>& buffer)
{
    double sum = 0.0;
    const auto* data = buffer.getReadPointer(0);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        sum += (double) data[i] * (double) data[i];

    return std::sqrt(sum / (double) juce::jmax(1, buffer.getNumSamples()));
}

// The worst the output differs from the input it should be reproducing, in dB.
//
// Bit-exactness stopped being available the moment the signal started going
// through a resampler and back, so the question changed from "is it identical"
// to "by how much is it not". A number is a better answer than a yes anyway: it
// says how much headroom the claim has, and it will move if anything upstream
// of it gets worse.
double worstErrorDb(const juce::AudioBuffer<float>& output,
                    const juce::AudioBuffer<float>& input,
                    int latency,
                    float expectedGain = 1.0f)
{
    double worst = 0.0;

    for (int i = latency + settleSamples; i < output.getNumSamples(); ++i)
    {
        const double got = (double) output.getSample(0, i);
        const double want = (double) input.getSample(0, i - latency) * (double) expectedGain;
        worst = std::max(worst, std::abs(got - want));
    }

    // An explicit floor far below anything meaningful. The default is -100 dB,
    // which this round trip is comfortably better than - so the default turned
    // every reading into "-100" and hid both how good it was and any change in
    // it. A clamped number that happens to pass is not a measurement.
    return juce::Decibels::gainToDecibels(worst, -200.0);
}

// Saturation done the old way: the same curve, evaluated once per sample at
// the base rate. This is what the chain did before it was oversampled, kept
// here as the thing to measure against. Without a reference, "it does not
// alias" is an assertion nobody can check.
float naiveSaturate(float x, float drive)
{
    if (drive <= 0.0f)
        return x;

    const float gain = 1.0f + drive * 9.0f;
    const float shaped = std::tanh(x * gain) / std::tanh(gain);
    return x + drive * (shaped - x);
}

// The loudest thing in a band, in dB. Used to look at one place in the spectrum
// where a harmonic cannot be and an alias can.
double bandPeakDb(const juce::AudioBuffer<float>& buffer, double centreHz, double halfWidthHz)
{
    constexpr int order = 12;
    constexpr int size = 1 << order;

    // performFrequencyOnlyForwardTransform wants room for the complex result,
    // so the array is twice the transform length.
    std::vector<float> data((size_t) size * 2, 0.0f);
    const int count = juce::jmin(buffer.getNumSamples(), size);
    for (int i = 0; i < count; ++i)
        data[(size_t) i] = buffer.getSample(0, i);

    // Windowed, or the discontinuity where the block wraps smears energy across
    // every bin and buries the very thing being looked for.
    juce::dsp::WindowingFunction<float> window((size_t) size,
                                               juce::dsp::WindowingFunction<float>::hann);
    window.multiplyWithWindowingTable(data.data(), (size_t) size);

    juce::dsp::FFT fft(order);
    fft.performFrequencyOnlyForwardTransform(data.data());

    const double binHz = sampleRate / (double) size;
    const int lowBin = juce::jmax(0, (int) std::floor((centreHz - halfWidthHz) / binHz));
    const int highBin = juce::jmin(size / 2 - 1, (int) std::ceil((centreHz + halfWidthHz) / binHz));

    float peak = 0.0f;
    for (int i = lowBin; i <= highBin; ++i)
        peak = std::max(peak, data[(size_t) i]);

    // Scaled by the transform length so the number means something on its own.
    // Both sides of the comparison get the same treatment either way.
    return juce::Decibels::gainToDecibels(peak / (float) (size / 2), -200.0f);
}

// Dynamics wound right off, so that a test of one stage is a test of one stage.
//
// Needed the moment the compressor and the limiter became real. Saturation at
// drive 0.9 lifts a 0.7 sine to about 0.97, which is past the compressor's knee
// and past the default ceiling - so a measurement meant to be about aliasing
// quietly became a measurement about three stages at once.
void disableDynamics(Parameters& params)
{
    params.compressorRatio.store(1.0f);  // 1:1 is not compression
    params.limiterCeilingDb.store(0.0f); // nothing below full scale is touched
}

// How transparent the round trip has to be before this suite is happy.
//
// Set from what the filters actually achieve with a comfortable margin, not
// from what they just scrape past. If this starts failing, something changed
// about the oversampling - which is exactly when somebody should be told.
constexpr double transparencyThresholdDb = -70.0;
} // namespace

int main()
{
    std::cout << "Mastering chain" << std::endl << std::endl;

    // ---- Before prepare ----------------------------------------------------
    // A processor handed audio before it has been told the sample rate has been
    // asked to do something impossible. Doing nothing is the only honest
    // answer; crashing is what a host would actually see.
    {
        Chain chain;
        Parameters params;
        auto buffer = makeSine(440.0, 256);
        auto before = buffer;

        chain.process(buffer, params);

        check(identical(buffer, before), "an unprepared chain leaves the buffer alone");
        std::cout << "  unprepared chain is inert" << std::endl;
    }

    // ---- Too few channels --------------------------------------------------
    // prepare() fixes the channel count because the oversampler is built around
    // it. A caller that then hands over fewer must get nothing rather than a
    // read past the end of its own buffer.
    {
        Chain chain;
        Parameters params;
        chain.prepare(sampleRate, preparedBlock, 2);

        juce::AudioBuffer<float> mono(1, 256);
        for (int i = 0; i < 256; ++i)
            mono.setSample(0, i, 0.25f);
        auto before = mono;

        chain.process(mono, params);

        check(identical(mono, before), "a buffer with fewer channels than prepared is refused");
        std::cout << "  mismatched channel count is refused, not guessed at" << std::endl;
    }

    // ---- Bypass ------------------------------------------------------------
    // Still exactly transparent, and now the only thing that is. This is what
    // makes an honest A/B possible once the rest of the chain has a sound.
    //
    // Bit for bit, but LATE by the reported latency - since 0.47.2. This test
    // used to demand the input back with no delay at all, and that is exactly
    // what hid the fault: the chain told the host it cost N samples and then,
    // bypassed, cost none, so a host compensating by N played the track early.
    {
        Chain chain;
        Parameters params;
        chain.prepare(sampleRate, preparedBlock, 2);

        params.bypass.store(1.0f);
        params.saturationDrive.store(1.0f);   // deliberately loud settings that
        params.outputGainDb.store(12.0f);     // WOULD change the signal if read

        auto buffer = makeSine(440.0, 1024);
        const auto before = buffer;
        const int latency = chain.getLatencySamples();

        chain.process(buffer, params);

        // Every sample, both channels: the input from exactly `latency` ago,
        // and silence before the first one arrives.
        int wrong = 0;
        for (int channel = 0; channel < 2; ++channel)
        {
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float want = i >= latency ? before.getSample(channel, i - latency) : 0.0f;
                if (buffer.getSample(channel, i) != want)
                    ++wrong;
            }
        }

        check(latency > 0 && wrong == 0,
              "bypass is bit-identical, exactly the reported latency late, whatever else is set ("
                  + juce::String(wrong) + " samples wrong)");
        std::cout << "  bypass is bit-identical and " << latency << " samples late" << std::endl;
    }

    // ---- The latency the host is told is the latency it gets ---------------
    //
    // The test that was missing. The one further down proves the NUMBER does
    // not move when bypassed; nothing proved the AUDIO kept to it. So the same
    // sine goes through a bypassed chain and a working one at neutral
    // settings, and the two have to agree in time - which is what lets a host
    // compensate by one fixed figure and have mastering switched on and off
    // under it without the track shifting.
    {
        Chain bypassedChain, workingChain;
        Parameters bypassedParams, workingParams;

        bypassedChain.prepare(sampleRate, preparedBlock, 2);
        workingChain.prepare(sampleRate, preparedBlock, 2);

        bypassedParams.bypass.store(1.0f);

        auto viaBypass = makeSine(440.0, 4096);
        auto viaWorking = viaBypass;

        bypassedChain.process(viaBypass, bypassedParams);
        workingChain.process(viaWorking, workingParams);

        // No latency argument: they are compared as they came out. The working
        // chain is a resampler round trip, so this is how close rather than
        // identical - the same threshold the neutral test holds it to.
        const double error = worstErrorDb(viaWorking, viaBypass, 0);

        std::cout << "  bypassed and working agree in time to " << error << " dBFS" << std::endl;
        check(error < transparencyThresholdDb,
              "a bypassed chain and a working one at neutral line up sample for sample");
    }

    // ---- Switching bypass on carries straight on ---------------------------
    //
    // The bypass line is fed on every block, working or not, so switching to
    // bypass plays on from the last samples that went in. Without that it
    // would replay whatever was left in the line from the last time the chain
    // was bypassed - here, the loud burst below, from long before.
    {
        Chain chain;
        Parameters params;
        chain.prepare(sampleRate, preparedBlock, 2);
        const int latency = chain.getLatencySamples();

        // Bypassed through something loud, then working through silence for
        // well over a latency's worth - long enough that the loud part is
        // ancient history by the time bypass comes back on.
        params.bypass.store(1.0f);
        auto loud = makeSine(440.0, 1024, 0.9f);
        chain.process(loud, params);

        params.bypass.store(0.0f);
        juce::AudioBuffer<float> silence(2, 4096);
        silence.clear();
        chain.process(silence, params);

        params.bypass.store(1.0f);
        juce::AudioBuffer<float> afterSwitch(2, latency * 2);
        afterSwitch.clear();
        chain.process(afterSwitch, params);

        const float stale = afterSwitch.getMagnitude(0, 0, afterSwitch.getNumSamples());
        check(stale == 0.0f, "switching bypass on plays on from what went in last, not from old audio ("
                                 + juce::String(stale) + ")");
    }

    // ---- Neutral settings --------------------------------------------------
    // With no drive and unity output the chain should give back what it was
    // given, delayed by the latency it reports. Not bit-identical any more: the
    // signal goes up to four times the rate and back, and a resampler is not an
    // identity. How close it gets is the thing worth measuring.
    {
        Chain chain;
        Parameters params;
        chain.prepare(sampleRate, preparedBlock, 2);

        auto buffer = makeSine(440.0, 4096);
        const auto reference = buffer;

        chain.process(buffer, params);

        const double error = worstErrorDb(buffer, reference, chain.getLatencySamples());
        check(error < transparencyThresholdDb,
              "neutral settings return the signal within the round trip's accuracy");
        std::cout << "  neutral round trip is accurate to " << error << " dBFS" << std::endl;
    }

    // ---- Buffers longer than the prepared block ----------------------------
    // An offline render hands over a whole note in one call. The result must be
    // the same as the live path that arrives in block-sized pieces, or a bounce
    // sounds different from what was played. Still exact: both routes drive the
    // same oversampler through the same states in the same order.
    {
        Chain oneGo, inPieces;
        Parameters params;
        params.saturationDrive.store(0.7f);
        params.outputGainDb.store(3.0f);

        oneGo.prepare(sampleRate, preparedBlock, 2);
        inPieces.prepare(sampleRate, preparedBlock, 2);

        constexpr int longLength = preparedBlock * 8;
        auto whole = makeSine(220.0, longLength);
        auto pieces = whole;

        oneGo.process(whole, params);

        for (int start = 0; start < longLength; start += preparedBlock)
        {
            juce::AudioBuffer<float> slice(2, preparedBlock);
            for (int channel = 0; channel < 2; ++channel)
                slice.copyFrom(channel, 0, pieces, channel, start, preparedBlock);

            inPieces.process(slice, params);

            for (int channel = 0; channel < 2; ++channel)
                pieces.copyFrom(channel, start, slice, channel, 0, preparedBlock);
        }

        check(allFinite(whole), "a long buffer produces finite samples");
        check(identical(whole, pieces),
              "one long call matches the same audio arriving in blocks");
        std::cout << "  " << longLength << " samples in one call matches " << (longLength / preparedBlock)
                  << " blocks" << std::endl;
    }

    // ---- Determinism across reset ------------------------------------------
    // Render, reset, render the same thing: the two must agree. This is what
    // catches an oversampler that was not reset along with everything else -
    // its filters would still hold the tail of the previous pass.
    {
        Chain chain;
        Parameters params;
        params.saturationDrive.store(0.5f);
        params.outputGainDb.store(-2.0f);

        chain.prepare(sampleRate, preparedBlock, 2);

        auto first = makeSine(330.0, 2048);
        chain.process(first, params);

        chain.reset();

        auto second = makeSine(330.0, 2048);
        chain.process(second, params);

        check(identical(first, second), "reset makes a second render match the first");
        std::cout << "  reset restores a repeatable starting state" << std::endl;
    }

    // ---- Output gain -------------------------------------------------------
    {
        Chain chain;
        Parameters params;
        chain.prepare(sampleRate, preparedBlock, 2);

        // +6.0206 dB is exactly a doubling. Asking for the round number and
        // expecting exactly 2.0 would be testing the tolerance, not the gain.
        params.outputGainDb.store(6.0205999f);

        auto buffer = makeSine(440.0, 4096, 0.25f);
        const auto reference = buffer;

        chain.process(buffer, params);

        const double error = worstErrorDb(buffer, reference, chain.getLatencySamples(), 2.0f);
        check(error < transparencyThresholdDb, "+6.02 dB of output gain doubles the signal");
        std::cout << "  output gain is accurate to " << error << " dBFS" << std::endl;
    }

    // ---- Saturation --------------------------------------------------------
    {
        Chain chain;
        Parameters params;
        disableDynamics(params);
        chain.prepare(sampleRate, preparedBlock, 2);

        auto untouched = makeSine(440.0, 4096);
        const auto reference = untouched;
        chain.process(untouched, params); // drive defaults to 0

        const double offError = worstErrorDb(untouched, reference, chain.getLatencySamples());
        check(offError < transparencyThresholdDb, "drive at zero leaves the signal alone");

        chain.reset();
        params.saturationDrive.store(0.9f);
        auto driven = makeSine(440.0, 4096);
        chain.process(driven, params);

        check(allFinite(driven), "a driven signal stays finite");

        // Saturation fills a waveform out towards a square, so the gap between
        // its peak and its average closes. A sine starts at a crest factor of
        // root two; anything that squashes the tips brings that down. This is
        // the property being claimed, and it survived the stage being
        // oversampled - which a sample-by-sample comparison would not have.
        const double referenceCrest = reference.getMagnitude(0, reference.getNumSamples()) / rmsOf(reference);
        const double drivenCrest = driven.getMagnitude(0, driven.getNumSamples()) / rmsOf(driven);

        check(drivenCrest < referenceCrest, "saturation lowers the crest factor");
        std::cout << "  crest factor " << referenceCrest << " -> " << drivenCrest << std::endl;
    }

    // ---- Aliasing, which is the entire point of this step -------------------
    //
    // tanh is an ODD function, so a 5 kHz sine driven through it comes out with
    // energy at 15, 25, 35, 45 kHz and upwards - the odd harmonics only, with
    // nothing at the even ones. At 44100 everything above 22050 has nowhere to
    // go and folds back down:
    //
    //     3rd  15 kHz  stays put
    //     5th  25 kHz  folds to 19.1 kHz
    //     7th  35 kHz  folds to  9.1 kHz   <- measured here
    //     9th  45 kHz  folds to  0.9 kHz
    //
    // 9.1 kHz is not a multiple of 5 kHz, which is what makes it findable: a
    // harmonic cannot be there, so anything there is fold-back and nothing
    // else. Picking a frequency where no harmonic lands reads as a clean pass
    // and measures nothing at all, which is how the first version of this went.
    //
    // Measured against the same curve run once per sample, because otherwise
    // "it does not alias" is a claim with nothing behind it - and because if
    // oversamplingStages were ever set back to zero, every other test in this
    // file would still pass.
    {
        constexpr double tone = 5000.0;
        constexpr double aliasHz = 9100.0; // the seventh harmonic, folded
        constexpr float drive = 0.9f;

        Chain chain;
        Parameters params;
        params.saturationDrive.store(drive);
        disableDynamics(params);
        chain.prepare(sampleRate, preparedBlock, 2);

        auto oversampled = makeSine(tone, 4096, 0.7f);
        auto naive = oversampled;

        chain.process(oversampled, params);

        for (int channel = 0; channel < naive.getNumChannels(); ++channel)
            for (int i = 0; i < naive.getNumSamples(); ++i)
                naive.setSample(channel, i, naiveSaturate(naive.getSample(channel, i), drive));

        // Both paths must still be doing the same job. Without this, "less
        // aliasing" would be satisfied perfectly by not saturating at all.
        const double naiveFundamental = bandPeakDb(naive, tone, 100.0);
        const double chainFundamental = bandPeakDb(oversampled, tone, 100.0);
        check(std::abs(naiveFundamental - chainFundamental) < 1.0,
              "both paths saturate to the same fundamental level");

        const double naiveAlias = bandPeakDb(naive, aliasHz, 100.0);
        const double chainAlias = bandPeakDb(oversampled, aliasHz, 100.0);

        // 50 dB, against roughly 83 dB actually achieved. Chosen to leave real
        // headroom while still being a number that means something: halving the
        // oversampling factor or weakening the halfband filters would not clear
        // it. A threshold set just under the measured value is a tripwire for
        // every harmless change and catches nothing else.
        check(chainAlias < naiveAlias - 50.0,
              "oversampling puts the folded harmonic far below where it was");

        std::cout << "  alias at " << aliasHz << " Hz: " << naiveAlias << " dB at base rate, "
                  << chainAlias << " dB oversampled ("
                  << (naiveAlias - chainAlias) << " dB better)" << std::endl;
    }

    // ---- Compressor: 1:1 is not compression --------------------------------
    {
        Chain chain;
        Parameters params;
        params.compressorRatio.store(1.0f);
        params.compressorThresholdDb.store(-40.0f); // far below the signal
        params.limiterCeilingDb.store(0.0f);
        chain.prepare(sampleRate, preparedBlock, 2);

        auto buffer = makeSine(440.0, 4096);
        const auto reference = buffer;

        chain.process(buffer, params);

        const double error = worstErrorDb(buffer, reference, chain.getLatencySamples());
        check(error < transparencyThresholdDb,
              "a ratio of one leaves the signal alone however low the threshold");
        std::cout << "  compressor at 1:1 is transparent to " << error << " dBFS" << std::endl;
    }

    // ---- Compressor: the arithmetic ----------------------------------------
    // Ten decibels over a 4:1 threshold should come back two and a half over,
    // which is seven and a half decibels of reduction. Far enough past the knee
    // that the answer is the straight-line one and no curve is involved.
    {
        Chain chain;
        Parameters params;
        params.compressorThresholdDb.store(-20.0f);
        params.compressorRatio.store(4.0f);
        params.compressorAttackMs.store(1.0f);
        params.compressorReleaseMs.store(300.0f);
        params.limiterCeilingDb.store(0.0f); // keep the limiter out of this
        chain.prepare(sampleRate, preparedBlock, 2);

        const float amplitude = juce::Decibels::decibelsToGain(-10.0f);
        auto buffer = makeSine(200.0, 44100, amplitude);

        chain.process(buffer, params);

        // Read near the end, long after the envelope has stopped moving.
        const double settledDb =
            juce::Decibels::gainToDecibels(buffer.getMagnitude(40000, 4000), -200.0f);
        constexpr double expectedDb = -17.5;

        check(std::abs(settledDb - expectedDb) < 0.5,
              "a tone 10 dB over a 4:1 threshold comes back 7.5 dB down");
        std::cout << "  compressor: -10 dBFS in, " << settledDb << " dBFS out (wanted "
                  << expectedDb << ")" << std::endl;
    }

    // ---- Compressor: parallel mix ------------------------------------------
    // Mix at zero has to be silence from the compressed copy, not a quieter
    // version of it. This is the check that makeup rides with the wet signal:
    // if makeup were applied to the blend, a mix of zero would still be louder.
    {
        Chain chain;
        Parameters params;
        params.compressorThresholdDb.store(-40.0f);
        params.compressorRatio.store(20.0f);
        params.compressorMakeupDb.store(12.0f);
        params.compressorMix.store(0.0f);
        params.limiterCeilingDb.store(0.0f);
        chain.prepare(sampleRate, preparedBlock, 2);

        auto buffer = makeSine(440.0, 4096);
        const auto reference = buffer;

        chain.process(buffer, params);

        const double error = worstErrorDb(buffer, reference, chain.getLatencySamples());
        check(error < transparencyThresholdDb,
              "a mix of zero is transparent even with heavy settings and makeup");
        std::cout << "  compressor at mix 0 is transparent to " << error << " dBFS" << std::endl;
    }

    // ---- Compressor: the channels are linked -------------------------------
    // Loud on the left, quiet on the right. One detector reads the louder side
    // and one gain goes to both, so the right must come down by the same amount
    // it did not ask for. Two independent detectors would leave it untouched,
    // and the stereo image would lean left every time the left side got loud.
    {
        Chain chain;
        Parameters params;
        params.compressorThresholdDb.store(-30.0f);
        params.compressorRatio.store(8.0f);
        params.compressorAttackMs.store(1.0f);
        params.compressorReleaseMs.store(300.0f);
        params.limiterCeilingDb.store(0.0f);
        chain.prepare(sampleRate, preparedBlock, 2);

        constexpr int length = 44100;
        juce::AudioBuffer<float> buffer(2, length);
        for (int i = 0; i < length; ++i)
        {
            const auto phase = 2.0 * juce::MathConstants<double>::pi * 200.0 * i / sampleRate;
            buffer.setSample(0, i, (float) (0.5 * std::sin(phase)));   // loud
            buffer.setSample(1, i, (float) (0.05 * std::sin(phase)));  // quiet
        }

        chain.process(buffer, params);

        // The quiet side started 20 dB below the loud one and must still be
        // 20 dB below it: one shared gain scales both by the same factor.
        const double leftDb = juce::Decibels::gainToDecibels(buffer.getMagnitude(0, 40000, 4000), -200.0f);
        const double rightDb = juce::Decibels::gainToDecibels(buffer.getMagnitude(1, 40000, 4000), -200.0f);

        check(std::abs((leftDb - rightDb) - 20.0) < 0.5,
              "both channels are moved by one shared gain");
        std::cout << "  compressor is stereo-linked: L " << leftDb << " dB, R " << rightDb
                  << " dB" << std::endl;
    }

    // ---- The limiter's ceiling is a guarantee -------------------------------
    // The whole reason the limiter looks ahead rather than clipping. Driven
    // 18 dB too hard, with bare transients that go from nothing to almost full
    // scale in a single sample, nothing may come out above the ceiling.
    {
        Chain chain;
        Parameters params;
        params.compressorRatio.store(1.0f); // the limiter, alone
        params.limiterCeilingDb.store(-1.0f);
        params.limiterReleaseMs.store(50.0f);
        params.outputGainDb.store(18.0f);
        chain.prepare(sampleRate, preparedBlock, 2);

        constexpr int length = 16384;
        juce::AudioBuffer<float> buffer(2, length);
        for (int i = 0; i < length; ++i)
        {
            const auto phase = 2.0 * juce::MathConstants<double>::pi * 110.0 * i / sampleRate;
            auto value = (float) (0.35 * std::sin(phase));

            // Spaced by a prime, so the spikes never settle into step with the
            // tone underneath and the limiter never gets a rhythm to learn.
            if (i % 977 == 0)
                value = 0.98f;

            buffer.setSample(0, i, value);
            buffer.setSample(1, i, -value); // opposite phase, to exercise the link
        }

        chain.process(buffer, params);

        const float ceiling = juce::Decibels::decibelsToGain(-1.0f);
        const float worst = buffer.getMagnitude(0, length);
        const double overshootDb = juce::Decibels::gainToDecibels(worst / ceiling, -200.0f);

        check(allFinite(buffer), "a savagely over-driven signal stays finite");
        check(worst <= ceiling * 1.001f,
              "nothing gets past the ceiling, however hard the limiter is driven");
        std::cout << "  limiter: worst sample is " << overshootDb << " dB relative to the ceiling"
                  << std::endl;
    }

    // ---- Gain reduction metering -------------------------------------------
    {
        Chain chain;
        Parameters params;
        params.compressorThresholdDb.store(-30.0f);
        params.compressorRatio.store(8.0f);
        params.limiterCeilingDb.store(0.0f);
        chain.prepare(sampleRate, preparedBlock, 2);

        auto buffer = makeSine(200.0, 8192, 0.5f);
        chain.process(buffer, params);

        check(chain.getGainReductionDb() > 5.0f, "the compressor meter moves while it works");
        check(chain.getLimiterReductionDb() < 0.01f,
              "the limiter meter stays still while the limiter is not needed");
        std::cout << "  metering: compressor " << chain.getGainReductionDb() << " dB, limiter "
                  << chain.getLimiterReductionDb() << " dB" << std::endl;
    }

    // ---- Extremes ----------------------------------------------------------
    // Everything at once, over a signal far past full scale. A mastering chain
    // is the last thing in the path, so whatever arrives is what it gets. The
    // alternating sample pattern is deliberately the worst case for a
    // resampler: it is energy sitting exactly at Nyquist.
    {
        Chain chain;
        Parameters params;
        chain.prepare(sampleRate, preparedBlock, 2);

        params.saturationDrive.store(1.0f);
        params.saturationMix.store(1.0f);
        params.outputGainDb.store(24.0f);

        juce::AudioBuffer<float> buffer(2, 512);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 512; ++i)
                buffer.setSample(channel, i, (i % 2 == 0) ? 10.0f : -10.0f);

        chain.process(buffer, params);

        check(allFinite(buffer), "a wildly over-scale input produces no NaN or infinity");
        std::cout << "  survives full-scale Nyquist 20 dB too loud" << std::endl;
    }

    // ---- Metering ----------------------------------------------------------
    {
        Chain chain;
        Parameters params;
        chain.prepare(sampleRate, preparedBlock, 2);

        auto buffer = makeSine(440.0, 512, 0.5f);
        chain.process(buffer, params);

        const float peak = chain.takeInputPeak();
        check(peak > 0.45f && peak <= 0.5001f, "the input peak meter reads the signal that went in");
        check(chain.takeInputPeak() == 0.0f, "reading a peak clears it for the next window");

        check(chain.getGainReductionDb() == 0.0f,
              "gain reduction reads zero while nothing reduces gain");
        std::cout << "  metering reads and clears correctly" << std::endl;
    }

    // ---- Latency -----------------------------------------------------------
    // It used to be zero and a check said so, on the understanding that the
    // check would fail the day oversampling landed. This is that day, and this
    // is what the check became.
    //
    // Two claims, and the second is the one that bites. A whole number, because
    // a host cannot be told a fraction. And CONSTANT - it must not move when a
    // dial moves, or the host would have to resynchronise mid-performance.
    {
        Chain chain;
        Parameters params;
        chain.prepare(sampleRate, preparedBlock, 2);

        const int atRest = chain.getLatencySamples();
        check(atRest > 0, "the chain reports the latency its oversampling costs");

        auto quiet = makeSine(440.0, 1024);
        chain.process(quiet, params);
        check(chain.getLatencySamples() == atRest, "latency is unchanged with no drive");

        params.saturationDrive.store(1.0f);
        auto driven = makeSine(440.0, 1024);
        chain.process(driven, params);
        check(chain.getLatencySamples() == atRest, "latency is unchanged with full drive");

        params.bypass.store(1.0f);
        auto bypassed = makeSine(440.0, 1024);
        chain.process(bypassed, params);
        check(chain.getLatencySamples() == atRest, "latency is unchanged while bypassed");

        std::cout << "  latency is " << atRest << " samples and does not move" << std::endl;
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL MASTERING CHAIN TESTS PASSED" << std::endl;
    else
        std::cout << failures << " MASTERING CHAIN TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
