// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MasterEffects.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace wavelathe;

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

// What the chain is told to expect. Everything here is deliberately the size a
// live host would use, so that the long buffers an offline render hands over
// are genuinely longer than the chain was prepared for - which is the case
// that broke once already.
constexpr int preparedBlock = 512;

juce::AudioBuffer<float> makeSine(double hz, int numSamples, float amplitude = 0.25f)
{
    juce::AudioBuffer<float> buffer(2, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        auto value = (float) (amplitude * std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / sampleRate));
        buffer.setSample(0, i, value);
        buffer.setSample(1, i, value);
    }

    return buffer;
}

juce::AudioBuffer<float> makeImpulse(int numSamples)
{
    juce::AudioBuffer<float> buffer(2, numSamples);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f);
    buffer.setSample(1, 0, 1.0f);
    return buffer;
}

double rmsBetween(const juce::AudioBuffer<float>& buffer, int start, int count)
{
    const auto* data = buffer.getReadPointer(0);
    double sum = 0.0;

    for (int i = start; i < start + count; ++i)
        sum += (double) data[i] * data[i];

    return std::sqrt(sum / juce::jmax(1, count));
}

// How much of one frequency a buffer contains - one bin of a DFT, worked out
// directly rather than by transforming the lot, since only two or three
// frequencies are ever asked about.
double magnitudeAt(const juce::AudioBuffer<float>& buffer, double hz, int start, int count)
{
    const auto* data = buffer.getReadPointer(0);
    double re = 0.0, im = 0.0;

    for (int i = 0; i < count; ++i)
    {
        double angle = 2.0 * juce::MathConstants<double>::pi * hz * i / sampleRate;
        re += (double) data[start + i] * std::cos(angle);
        im += (double) data[start + i] * std::sin(angle);
    }

    return 2.0 * std::sqrt(re * re + im * im) / (double) count;
}

double toDb(double ratio)
{
    return 20.0 * std::log10(juce::jmax(1.0e-12, ratio));
}

// The largest gap between two renders of the same thing, as a raw sample
// value. Returned raw rather than in dB because the interesting answer is
// exactly zero, and a logarithm has no way to say so - the first version of
// this returned dB, bottomed out at its own floor of -240, and reported a
// perfect result as a failure.
double worstDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double worst = 0.0;

    for (int channel = 0; channel < juce::jmin(a.getNumChannels(), b.getNumChannels()); ++channel)
    {
        const auto* left = a.getReadPointer(channel);
        const auto* right = b.getReadPointer(channel);

        for (int i = 0; i < juce::jmin(a.getNumSamples(), b.getNumSamples()); ++i)
            worst = juce::jmax(worst, (double) std::abs(left[i] - right[i]));
    }

    return worst;
}

// For printing: "identical" where it really is, and dB where it is not.
juce::String differenceText(double worst)
{
    if (worst <= 0.0)
        return "identical";

    return juce::String(toDb(worst), 1) + " dBFS";
}

// Renders a copy of the input through a chain of its own, handing it over in
// pieces of the given size. A chunk size of zero means one call with the lot,
// which is what an offline render does and what the chain has to split up
// internally.
juce::AudioBuffer<float> renderInChunks(const juce::AudioBuffer<float>& input, const SynthParameters& params,
                                        int chunkSamples)
{
    juce::AudioBuffer<float> work(input);

    MasterEffects fx;
    fx.prepare(sampleRate, preparedBlock, 2);
    fx.reset();

    const int total = work.getNumSamples();
    const int step = chunkSamples > 0 ? chunkSamples : total;

    for (int start = 0; start < total; start += step)
    {
        int count = juce::jmin(step, total - start);
        juce::AudioBuffer<float> chunk(work.getArrayOfWritePointers(), work.getNumChannels(), start, count);
        fx.process(chunk, params, 120.0);
    }

    return work;
}

// The rate at which a buffer's loudness wobbles, in Hz.
//
// A chorus is a delay whose length is swept, so mixed back against the dry
// signal it makes a comb filter whose notches move - and what that does to a
// steady tone is push its level up and down at the sweep rate. Measuring the
// envelope is therefore measuring the LFO, without needing to reach inside it.
//
// Counted by zero crossings of the envelope about its own mean rather than by
// transforming it: the whole point is a handful of cycles over several
// seconds, and no window short enough to localise that is long enough to
// resolve it.
double envelopeWobbleHz(const juce::AudioBuffer<float>& buffer, int start, int count, double& depthOut)
{
    const auto* data = buffer.getReadPointer(0);

    // Rectify, then a one-pole slow enough to lose the tone and keep the sweep.
    std::vector<double> envelope((size_t) count, 0.0);
    const double coefficient = 1.0 - std::exp(-1.0 / (0.01 * sampleRate)); // 10ms
    double state = 0.0;

    for (int i = 0; i < count; ++i)
    {
        state += ((double) std::abs(data[start + i]) - state) * coefficient;
        envelope[(size_t) i] = state;
    }

    // Ignore the first tenth of a second, where the one-pole is still filling.
    const int settled = juce::jmin(count - 1, (int) (0.1 * sampleRate));

    double mean = 0.0;
    for (int i = settled; i < count; ++i)
        mean += envelope[(size_t) i];
    mean /= juce::jmax(1, count - settled);

    double lowest = envelope[(size_t) settled], highest = envelope[(size_t) settled];
    for (int i = settled; i < count; ++i)
    {
        lowest = juce::jmin(lowest, envelope[(size_t) i]);
        highest = juce::jmax(highest, envelope[(size_t) i]);
    }

    // How far the level actually travels, as a fraction of where it sits. Zero
    // means a steady tone came out steady, which is what a chorus doing
    // nothing looks like.
    depthOut = mean > 1.0e-9 ? (highest - lowest) / mean : 0.0;

    // Crossings of the mean, with a dead band either side so a level that is
    // merely noisy about its average does not read as a sweep.
    const double band = (highest - lowest) * 0.15;
    if (band < 1.0e-9)
        return 0.0;

    int rises = 0;
    bool armed = false;
    int firstRise = -1, lastRise = -1;

    for (int i = settled; i < count; ++i)
    {
        double value = envelope[(size_t) i];

        if (value < mean - band)
            armed = true;
        else if (armed && value > mean + band)
        {
            armed = false;
            ++rises;
            if (firstRise < 0)
                firstRise = i;
            lastRise = i;
        }
    }

    if (rises < 2)
        return 0.0;

    return (rises - 1) * sampleRate / (double) (lastRise - firstRise);
}

// A patch with everything switched on at once, for the cases that want the
// whole chain working rather than one effect at a time.
void setupEverything(SynthParameters& p)
{
    p.fxDistortion = 0.40f;
    p.chorusMix = 0.50f;
    p.chorusRate = 1.30f;
    p.chorusDepth = 0.60f;
    p.phaserMix = 0.45f;
    p.phaserRate = 0.90f;
    p.phaserFeedback = 0.50f;
    p.delayMix = 0.40f;
    p.delayFeedback = 0.45f;
    p.delayTimeMs = 180.0f;
    p.delaySync = 0.0f;
    p.reverbMix = 0.35f;
    p.reverbSize = 0.60f;
    p.eqLowGain = 4.0f;
    p.eqMidGain = -3.0f;
    p.eqHighGain = 5.0f;
}
} // namespace

int main()
{
    std::cout << "MasterFxTest - the master chain, measured" << std::endl;

    // ---- Off means off ------------------------------------------------------
    // Every effect defaults to zero mix, and each is skipped rather than run
    // and thrown away. So a default chain has to be a wire: not approximately,
    // exactly, since nothing in it should have touched a sample.
    //
    // This is also what proves the harness before anything else leans on it. A
    // rig that quietly altered the signal would make every reading below wrong
    // in the same direction, and nothing later would notice.
    std::cout << std::endl << "A chain with nothing switched on:" << std::endl;
    {
        SynthParameters p;
        auto input = makeSine(440.0, 8192);
        auto output = renderInChunks(input, p, 0);

        double worst = worstDifference(input, output);
        std::cout << "  worst difference from the input: " << differenceText(worst) << std::endl;

        check(worst <= 0.0, "an effect chain with everything off passes the signal through untouched");
    }

    // ---- The same render, handed over in different sized pieces -------------
    // The headline. A host calls process() with its own block size; an offline
    // render calls it once with a whole note, which the chain splits up
    // internally. If any of it carries state per block rather than per sample,
    // those two disagree - and the disagreement is silent, because each sounds
    // perfectly fine on its own.
    //
    // It has happened here before. 0.19.0: "Master effects split long buffers,
    // which is what stopped the chorus working outside live playback." That was
    // found by accident. Nothing has checked it since.
    //
    // It matters beyond tidiness: sound matching scores candidates from offline
    // renders, so anything that renders differently offline is being matched
    // against a sound the user will never hear.
    std::cout << std::endl << "The same audio, handed over in different sized pieces:" << std::endl;
    {
        struct Case
        {
            const char* name;
            void (*setup)(SynthParameters&);
        };

        const Case cases[] = {
            {"drive alone", [](SynthParameters& p) { p.fxDistortion = 0.7f; }},
            {"chorus alone", [](SynthParameters& p) { p.chorusMix = 0.8f; p.chorusRate = 1.5f; p.chorusDepth = 0.7f; }},
            {"phaser alone", [](SynthParameters& p) { p.phaserMix = 0.8f; p.phaserRate = 1.1f; }},
            {"delay alone", [](SynthParameters& p) { p.delayMix = 0.6f; p.delayFeedback = 0.5f; p.delayTimeMs = 150.0f; }},
            {"reverb alone", [](SynthParameters& p) { p.reverbMix = 0.6f; p.reverbSize = 0.7f; }},
            {"tone alone", [](SynthParameters& p) { p.eqLowGain = 8.0f; p.eqMidGain = -6.0f; p.eqHighGain = 7.0f; }},
            {"all of it at once", setupEverything},
        };

        // Long enough that the internal split runs many times over, and not a
        // multiple of any chunk size below, so the last piece is a short one.
        auto input = makeSine(220.0, 30000);

        for (const auto& testCase : cases)
        {
            SynthParameters p;
            testCase.setup(p);

            auto whole = renderInChunks(input, p, 0);     // one call: the offline path
            auto host = renderInChunks(input, p, preparedBlock);
            auto small = renderInChunks(input, p, 64);
            auto odd = renderInChunks(input, p, 333);

            double vsHost = worstDifference(whole, host);
            double vsSmall = worstDifference(whole, small);
            double vsOdd = worstDifference(whole, odd);

            std::cout << "  " << juce::String(testCase.name).paddedRight(' ', 18)
                      << " vs 512: " << differenceText(vsHost).paddedLeft(' ', 11)
                      << "   vs 64: " << differenceText(vsSmall).paddedLeft(' ', 11)
                      << "  vs 333: " << differenceText(vsOdd).paddedLeft(' ', 11)
                      << std::endl;

            // A thousandth of full scale (-60 dBFS): audible if it were a
            // signal, and far beyond anything float arithmetic reordering
            // could account for.
            double worst = juce::jmax(vsHost, juce::jmax(vsSmall, vsOdd));
            check(worst < 1.0e-3,
                  juce::String(testCase.name) + " renders the same whatever size the pieces are");
        }
    }

    // ---- The chorus, measured directly --------------------------------------
    // Six attempts to make the matching objective notice a chorus have failed,
    // and every time the measure was blamed. Nobody has ever measured the
    // effect itself, which is the other end of the same question: an objective
    // cannot hear modulation that is not there.
    std::cout << std::endl << "The chorus, measured directly:" << std::endl;
    {
        // A low tone on purpose. The comb this makes has notches spaced by the
        // reciprocal of the delay, and a sweep that crosses several of them
        // would push the level up and down more than once per LFO cycle and
        // read as a multiple of the rate. Low and shallow keeps one sweep to
        // one wobble.
        const int length = (int) (6.0 * sampleRate);
        auto input = makeSine(80.0, length, 0.3f);

        // The control: mix up, depth at zero. There is still a delay, so there
        // is still a comb - it just is not moving. Any wobble measured here is
        // the instrument's own, and sets the floor for the readings below.
        double stillDepth = 0.0;
        {
            SynthParameters p;
            p.chorusMix = 0.7f;
            p.chorusRate = 2.0f;
            p.chorusDepth = 0.0f;

            auto output = renderInChunks(input, p, preparedBlock);
            envelopeWobbleHz(output, 0, length, stillDepth);

            std::cout << "  depth 0.00 (a still comb): level travels "
                      << juce::String(stillDepth * 100.0, 1) << "% of its average" << std::endl;
        }

        // And the same thing moving. If the chorus does what its name says,
        // this is a different reading entirely.
        double movingDepth = 0.0;
        {
            SynthParameters p;
            p.chorusMix = 0.7f;
            p.chorusRate = 2.0f;
            p.chorusDepth = 0.6f;

            auto output = renderInChunks(input, p, preparedBlock);
            envelopeWobbleHz(output, 0, length, movingDepth);

            std::cout << "  depth 0.60 (sweeping):     level travels "
                      << juce::String(movingDepth * 100.0, 1) << "% of its average" << std::endl;
        }

        check(movingDepth > stillDepth * 3.0 + 0.05,
              "turning the chorus depth up actually moves the sound");

        // Then whether the rate dial means anything. The reading may come back
        // at a fixed multiple of the dial - a sweep crossing two notches wobbles
        // twice per cycle - so what is checked is that it tracks in proportion,
        // not that it matches to the hertz.
        std::cout << "  and what the rate dial does:" << std::endl;

        const float rates[] = {0.5f, 1.0f, 2.0f, 4.0f};
        std::vector<double> ratios;

        for (float rate : rates)
        {
            SynthParameters p;
            p.chorusMix = 0.7f;
            p.chorusRate = rate;
            p.chorusDepth = 0.5f;

            auto output = renderInChunks(input, p, preparedBlock);
            double depth = 0.0;
            double measured = envelopeWobbleHz(output, 0, length, depth);

            std::cout << "    dial " << juce::String(rate, 2) << " Hz -> the level wobbles at "
                      << juce::String(measured, 2) << " Hz" << std::endl;

            if (measured > 0.01)
                ratios.push_back(measured / (double) rate);
        }

        bool proportional = ratios.size() == 4;
        if (proportional)
            for (double ratio : ratios)
                proportional = proportional && std::abs(ratio - ratios[0]) < ratios[0] * 0.25;

        check(proportional, "the chorus rate dial sets the rate it claims to");
    }

    // ---- The delay lands where the dial says --------------------------------
    // An impulse in, and the echo has to come back at the time set. Full wet
    // and no feedback, so there is exactly one thing to find.
    std::cout << std::endl << "The delay, timed with an impulse:" << std::endl;
    {
        const float times[] = {50.0f, 100.0f, 250.0f};

        for (float ms : times)
        {
            SynthParameters p;
            p.delayMix = 1.0f;
            p.delayFeedback = 0.0f;
            p.delayTimeMs = ms;
            p.delaySync = 0.0f;

            const int length = (int) (1.5 * sampleRate);
            auto input = makeImpulse(length);
            auto output = renderInChunks(input, p, preparedBlock);

            const auto* data = output.getReadPointer(0);
            int loudest = 0;
            float peak = 0.0f;

            for (int i = 1; i < length; ++i)
                if (std::abs(data[i]) > peak)
                {
                    peak = std::abs(data[i]);
                    loudest = i;
                }

            double measuredMs = loudest * 1000.0 / sampleRate;

            std::cout << "  dial " << juce::String(ms, 0).paddedLeft(' ', 4) << " ms -> the echo arrives at "
                      << juce::String(measuredMs, 1) << " ms" << std::endl;

            check(std::abs(measuredMs - (double) ms) < (double) ms * 0.05 + 1.0,
                  "the delay time dial is in milliseconds and means them");
        }
    }

    // ---- Drive adds harmonics -----------------------------------------------
    // What distortion IS, rather than that it changes the level - a gain stage
    // would change the level too. The shaper is a tanh, which is odd, so the
    // third harmonic is where to look.
    std::cout << std::endl << "Drive, by what it adds:" << std::endl;
    {
        const int length = 16384;
        auto input = makeSine(440.0, length, 0.35f);

        const float amounts[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
        double previous = -1000.0; // below any real reading: the first pass has nothing to beat
        bool rising = true;

        for (float amount : amounts)
        {
            SynthParameters p;
            p.fxDistortion = amount;

            auto output = renderInChunks(input, p, preparedBlock);

            double fundamental = magnitudeAt(output, 440.0, 2048, length - 2048);
            double third = magnitudeAt(output, 1320.0, 2048, length - 2048);
            double ratioDb = toDb(third / juce::jmax(1.0e-9, fundamental));

            std::cout << "  drive " << juce::String(amount, 2) << " -> third harmonic is "
                      << juce::String(ratioDb, 1) << " dB below the fundamental" << std::endl;

            if (previous > -900.0 && ratioDb <= previous)
                rising = false;

            previous = ratioDb;
        }

        check(rising, "every step up the drive dial adds more harmonics than the last");
    }

    // ---- Each tone band moves its own frequency -----------------------------
    // Three bands, three test tones, one at the centre of each. Boosting a band
    // has to lift its own tone by roughly what the dial says, and lift it more
    // than it lifts the other two - the bell in the middle is wide, so the
    // others move a little and that is correct, not a fault.
    std::cout << std::endl << "The tone controls, band by band:" << std::endl;
    {
        const double tones[] = {100.0, 1200.0, 8000.0};
        const char* bandNames[] = {"low", "mid", "high"};
        const int length = 32768;
        const int skip = 4096; // let the filters settle

        for (int band = 0; band < 3; ++band)
        {
            double lifts[3] = {0.0, 0.0, 0.0};

            for (int tone = 0; tone < 3; ++tone)
            {
                auto input = makeSine(tones[tone], length, 0.25f);

                SynthParameters flat;
                auto dry = renderInChunks(input, flat, preparedBlock);

                SynthParameters boosted;
                if (band == 0) boosted.eqLowGain = 12.0f;
                if (band == 1) boosted.eqMidGain = 12.0f;
                if (band == 2) boosted.eqHighGain = 12.0f;

                auto wet = renderInChunks(input, boosted, preparedBlock);

                double before = magnitudeAt(dry, tones[tone], skip, length - skip);
                double after = magnitudeAt(wet, tones[tone], skip, length - skip);
                lifts[tone] = toDb(after / juce::jmax(1.0e-9, before));
            }

            std::cout << "  " << juce::String(bandNames[band]).paddedRight(' ', 5) << "+12 dB lifts  100 Hz by "
                      << juce::String(lifts[0], 1).paddedLeft(' ', 6) << ",  1200 Hz by "
                      << juce::String(lifts[1], 1).paddedLeft(' ', 6) << ",  8000 Hz by "
                      << juce::String(lifts[2], 1).paddedLeft(' ', 6) << " dB" << std::endl;

            check(std::abs(lifts[band] - 12.0) < 3.0,
                  juce::String(bandNames[band]) + " band at +12 dB lifts its own frequency by about 12 dB");

            for (int other = 0; other < 3; ++other)
                if (other != band)
                    check(lifts[band] > lifts[other] + 2.0,
                          juce::String(bandNames[band]) + " band moves its own frequency most");
        }
    }

    // ---- The reverb leaves something behind ---------------------------------
    // An impulse is over in one sample. Anything still sounding a second later
    // is the room.
    std::cout << std::endl << "The reverb, by what is left after the sound stops:" << std::endl;
    {
        const int length = (int) (3.0 * sampleRate);
        auto input = makeImpulse(length);

        double tails[2] = {0.0, 0.0};
        const float sizes[] = {0.1f, 0.9f};

        for (int i = 0; i < 2; ++i)
        {
            SynthParameters p;
            p.reverbMix = 0.6f;
            p.reverbSize = sizes[i];

            auto output = renderInChunks(input, p, preparedBlock);

            // A full second after the impulse, so nothing here is the impulse.
            tails[i] = rmsBetween(output, (int) sampleRate, (int) sampleRate);

            std::cout << "  size " << juce::String(sizes[i], 2) << " -> still ringing at "
                      << juce::String(toDb(tails[i]), 1) << " dBFS one second later" << std::endl;
        }

        check(tails[0] > 1.0e-7, "the reverb puts a tail behind an impulse");
        check(tails[1] > tails[0], "and a bigger room rings for longer than a small one");
    }

    
    // ---- The tail a host is promised ---------------------------------------
    // What getTailLengthSeconds hands the host, checked against what the chain
    // actually does. A host renders for exactly this long after the last note
    // when it bounces, freezes or exports, so an estimate that falls short does
    // not sound odd - it cuts the end off the sound, silently, only on export.
    std::cout << "The tail a host is told to render for:" << std::endl;
    {
        // How long the chain keeps making sound after one impulse, in seconds.
        auto measureRingDown = [](const SynthParameters& params)
        {
            MasterEffects fx;
            fx.prepare(sampleRate, preparedBlock, 2);
    
            const int seconds = 25;
            auto buffer = makeImpulse((int) (sampleRate * seconds));
            fx.process(buffer, params, 120.0);
    
            // Walk back from the end until the signal rises out of hearing. A
            // window rather than a sample, because a decaying tail crosses zero
            // constantly and a single sample below the floor means nothing.
            const int window = (int) (sampleRate * 0.05);
            const double floorLevel = 0.001; // -60 dB on the impulse that started it
    
            for (int start = buffer.getNumSamples() - window; start >= 0; start -= window)
                if (rmsBetween(buffer, start, window) > floorLevel)
                    return (double) (start + window) / sampleRate;
    
            return 0.0;
        };
    
        // Release alone, with nothing else running.
        {
            SynthParameters p;
            p.release = 2.5f;
            p.delayMix = 0.0f;
            p.reverbMix = 0.0f;
    
            check(tailLengthSeconds(p) >= 2.5, "a long release is covered on its own");
            check(tailLengthSeconds(p) < 3.0, "and nothing is added for effects that are off");
        }
    
        // The reverb at both ends of its dial, measured.
        {
            SynthParameters p;
            p.release = 0.0f;
            p.delayMix = 0.0f;
            p.reverbMix = 1.0f;
            p.reverbSize = 0.0f;
    
            const double smallest = measureRingDown(p);
            std::cout << "  reverb at smallest size rings for " << smallest << "s, promised "
                      << tailLengthSeconds(p) << "s" << std::endl;
            check(tailLengthSeconds(p) >= smallest, "the smallest reverb is covered");
    
            p.reverbSize = 1.0f;
            const double largest = measureRingDown(p);
            std::cout << "  reverb at largest size rings for " << largest << "s, promised "
                      << tailLengthSeconds(p) << "s" << std::endl;
            check(tailLengthSeconds(p) >= largest, "and so is the largest");
            check(largest > smallest, "and a bigger room really does ring longer");
        }
    
        // The delay, which is the one that has to be capped.
        {
            SynthParameters p;
            p.release = 0.0f;
            p.reverbMix = 0.0f;
            p.delayMix = 1.0f;
            p.delayTimeMs = 500.0f;
            p.delayFeedback = 0.5f;
    
            const double measured = measureRingDown(p);
            std::cout << "  delay at half feedback rings for " << measured << "s, promised "
                      << tailLengthSeconds(p) << "s" << std::endl;
            check(tailLengthSeconds(p) >= measured, "an ordinary delay is covered in full");
    
            // At the top of the dial the honest answer is minutes, so this one is
            // deliberately short. Stated as a test so the cap is a decision on the
            // record rather than a number someone later mistakes for a bug.
            p.delayTimeMs = 1500.0f;
            p.delayFeedback = 0.95f;
            check(tailLengthSeconds(p) < 12.0, "and the most extreme one is capped, on purpose");
            check(tailLengthSeconds(p) > 7.0, "though not to nothing");
        }
    
        // Everything at once, which is what a patch that uses the whole panel asks
        // the host for.
        {
            SynthParameters p;
            p.release = 5.0f;
            p.delayMix = 1.0f;
            p.delayTimeMs = 400.0f;
            p.delayFeedback = 0.6f;
            p.reverbMix = 1.0f;
            p.reverbSize = 1.0f;
    
            check(tailLengthSeconds(p) > 5.0, "the parts add up rather than one winning");
            check(tailLengthSeconds(p) < 30.0, "and still come to something a person would wait for");
        }
    
        // The default patch, which is what most sessions actually render.
        {
            const SynthParameters p;
            check(tailLengthSeconds(p) > 0.0, "even a default patch is promised something");
    }
}

    // ---- The mix bus: the same chain a second time --------------------------
    //
    // The Master page runs this class over synth and drums together, from its
    // own dials. Three things matter about that copy. A bus nobody has touched
    // must change NOTHING - not nearly nothing - or every project saved before
    // the bus existed stops sounding the way it did. Its gain stage, which the
    // Synth page's chain never engages, has to do what the dial says. And the
    // two copies have to read their own dials and not each other's.
    std::cout << std::endl << "The mix bus:" << std::endl;
    {
        const SynthParameters defaults;
        const auto bus = busEffectSettingsFrom(defaults);

        check(effectsTailSeconds(bus) == 0.0, "an untouched bus adds no tail");

        // Bit for bit. Dry, flat and unity means every stage skips itself, so
        // the samples have to come out exactly as they went in.
        const auto input = makeSine(220.0, 44100);
        juce::AudioBuffer<float> work(input);

        MasterEffects fx;
        fx.prepare(sampleRate, preparedBlock, 2);
        fx.reset();
        fx.process(work, bus, 120.0);

        int differing = 0;
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < input.getNumSamples(); ++i)
                if (work.getSample(channel, i) != input.getSample(channel, i))
                    ++differing;

        check(differing == 0, "an untouched bus passes the mix through bit for bit ("
                                  + juce::String(differing) + " samples differ)");

        // The gain stage.
        SynthParameters quieter;
        quieter.busFx[(size_t) project::BusFxControl::outputGain] = -6.0f;

        juce::AudioBuffer<float> attenuated(input);
        MasterEffects gainFx;
        gainFx.prepare(sampleRate, preparedBlock, 2);
        gainFx.reset();
        gainFx.process(attenuated, busEffectSettingsFrom(quieter), 120.0);

        const auto ratio = rmsBetween(attenuated, 4410, 22050) / rmsBetween(input, 4410, 22050);
        std::cout << "  -6 dB of bus gain came out as a ratio of " << ratio << std::endl;
        check(std::abs(ratio - 0.5012) < 0.005, "the bus gain is in decibels and lands where it says");

        // A value no dial can reach - only a hand-edited project could hold
        // it - is held at the dial's end rather than obeyed.
        SynthParameters wild;
        wild.busFx[(size_t) project::BusFxControl::outputGain] = 200.0f;
        check(std::abs(busEffectSettingsFrom(wild).outputGain - juce::Decibels::decibelsToGain(24.0f)) < 1.0e-3f,
              "an impossible bus gain is clamped to +24 dB");

        // Its tail, by the same arithmetic the Synth page's is promised by.
        SynthParameters roomy;
        roomy.busFx[(size_t) project::BusFxControl::reverbMix] = 0.5f;
        roomy.busFx[(size_t) project::BusFxControl::reverbSize] = 1.0f;
        std::cout << "  a full-size bus reverb promises "
                  << effectsTailSeconds(busEffectSettingsFrom(roomy)) << "s" << std::endl;
        check(effectsTailSeconds(busEffectSettingsFrom(roomy)) >= 4.0, "a large bus reverb promises its tail");

        // Two copies, two sets of dials.
        SynthParameters split;
        split.busFx[(size_t) project::BusFxControl::reverbMix] = 1.0f;
        check(effectSettingsFrom(split).reverbMix == 0.0f, "the Synth page's chain does not read the bus's dials");
        check(busEffectSettingsFrom(split).reverbMix == 1.0f, "and the bus does");
        check(effectSettingsFrom(split).outputGain == 1.0f,
              "and the Synth page's chain is always at unity gain, as it was before the stage existed");
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL MASTER FX TESTS PASSED" << std::endl;
    else
        std::cout << failures << " MASTER FX TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
