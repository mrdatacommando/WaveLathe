// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SampleMatcher.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace wavelathe
{
namespace SampleMatch
{
namespace
{
struct PitchResult
{
    float frequencyHz = 0.0f;
    // 0..1 periodicity. Near 1 means the waveform genuinely repeats at this
    // period (one clean note); low values mean it does not repeat well at any
    // period, which is what a chord, two overlapping notes, or noise looks like.
    float clarity = 0.0f;
};

// Pitch detection using the normalised square difference function (McLeod
// Pitch Method). Plain autocorrelation is biased towards long lags, which is
// how a chord can lock onto a spurious very-low "fundamental" whose harmonic
// comb appears to explain everything. Normalising removes that bias, and
// picking the FIRST strong peak rather than the tallest avoids the classic
// octave-below error.
PitchResult estimatePitchDetailed(const float* data, int numSamples, double sampleRate)
{
    // The range of pitches this will look for. The top end used to be 500 Hz,
    // which is only B4 - a note above that has a period shorter than the
    // shortest lag examined, so its own peak was never even looked at and the
    // first subharmonic that fell inside the range won instead. A C5 came back
    // as a C4, and matching then rendered every candidate an octave low.
    // Four thousand two hundred covers the top of a piano.
    constexpr double highestDetectableHz = 4200.0;
    constexpr double lowestDetectableHz = 20.0;

    int minLag = juce::jmax(2, (int) (sampleRate / highestDetectableHz));
    int maxLag = (int) (sampleRate / lowestDetectableHz);
    maxLag = juce::jmin(maxLag, numSamples / 2);
    if (maxLag <= minLag)
        return {};

    // A few periods of the lowest pitch we look for is plenty, and keeps this
    // affordable inside the optimiser's inner loops.
    int window = juce::jmin(numSamples, maxLag * 2);

    std::vector<double> nsdf((size_t) maxLag + 1, 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double correlation = 0.0, energy = 0.0;
        int count = window - lag;
        if (count <= 0)
            break;

        for (int i = 0; i < count; ++i)
        {
            double a = data[i];
            double b = data[i + lag];
            correlation += a * b;
            energy += a * a + b * b;
        }

        nsdf[(size_t) lag] = energy > 1.0e-12 ? 2.0 * correlation / energy : 0.0;
    }

    double globalMax = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        globalMax = juce::jmax(globalMax, nsdf[(size_t) lag]);

    if (globalMax <= 0.0)
        return {};

    // First local maximum that is nearly as tall as the tallest one.
    const double acceptThreshold = 0.85 * globalMax;
    int chosenLag = -1;
    for (int lag = minLag + 1; lag < maxLag; ++lag)
    {
        if (nsdf[(size_t) lag] > nsdf[(size_t) lag - 1] && nsdf[(size_t) lag] >= nsdf[(size_t) lag + 1]
            && nsdf[(size_t) lag] >= acceptThreshold)
        {
            chosenLag = lag;
            break;
        }
    }

    if (chosenLag < 0)
        return {};

    // Parabolic interpolation for sub-sample period accuracy.
    double y0 = nsdf[(size_t) chosenLag - 1];
    double y1 = nsdf[(size_t) chosenLag];
    double y2 = nsdf[(size_t) chosenLag + 1];
    double denominator = 2.0 * (2.0 * y1 - y0 - y2);
    double refinedLag = chosenLag;
    if (std::abs(denominator) > 1.0e-12)
        refinedLag = chosenLag + (y2 - y0) / denominator;

    PitchResult result;
    result.frequencyHz = refinedLag > 0.0 ? (float) (sampleRate / refinedLag) : 0.0f;
    result.clarity = (float) juce::jlimit(0.0, 1.0, y1);
    return result;
}

float estimateFundamentalHz(const float* data, int numSamples, double sampleRate)
{
    return estimatePitchDetailed(data, numSamples, sampleRate).frequencyHz;
}

SpectralStats computeSpectralStats(const float* data, int numSamples, double sampleRate)
{
    int order = 13;
    while (order > 8 && (1 << order) > numSamples)
        --order;
    int fftSize = 1 << order;

    SpectralStats stats;
    if (numSamples < fftSize)
        return stats;

    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);

    std::vector<double> avgMagnitude((size_t) fftSize / 2, 0.0);
    int hop = juce::jmax(1, fftSize / 2);
    int frameCount = 0;

    std::vector<float> frame((size_t) fftSize * 2, 0.0f);

    for (int start = 0; start + fftSize <= numSamples; start += hop)
    {
        std::fill(frame.begin(), frame.end(), 0.0f);
        std::copy(data + start, data + start + fftSize, frame.begin());
        window.multiplyWithWindowingTable(frame.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform(frame.data());

        for (int bin = 0; bin < fftSize / 2; ++bin)
            avgMagnitude[(size_t) bin] += frame[(size_t) bin];

        ++frameCount;
    }

    if (frameCount == 0)
        return stats;

    double totalEnergy = 0.0;
    double weightedFreqSum = 0.0;
    double binHz = sampleRate / fftSize;

    for (size_t bin = 0; bin < avgMagnitude.size(); ++bin)
    {
        double mag = avgMagnitude[bin] / frameCount;
        double freq = (double) bin * binHz;
        totalEnergy += mag;
        weightedFreqSum += mag * freq;
    }

    stats.centroidHz = totalEnergy > 0.0 ? (float) (weightedFreqSum / totalEnergy) : 0.0f;

    double cumulative = 0.0;
    double target = totalEnergy * 0.85;
    for (size_t bin = 0; bin < avgMagnitude.size(); ++bin)
    {
        cumulative += avgMagnitude[bin] / frameCount;
        if (cumulative >= target)
        {
            stats.rolloff85Hz = (float) ((double) bin * binHz);
            break;
        }
    }

    return stats;
}

// Detects periodic amplitude modulation (beating) caused by two or more
// closely-detuned oscillators (or an LFO) summing/modulating over time.
// Uses a real one-pole low-pass filter (not just a light boxcar) on the
// rectified signal so a note's own fundamental-rate ripple can't leak into
// and masquerade as slow "beating". The cutoff adapts to the note's own
// pitch: low enough to reject that note's fundamental, but as high as
// possible (up to ~40Hz) so fast detune-beats on higher notes aren't
// filtered away too.
BeatStats detectAmplitudeBeating(const float* data, int numSamples, double sampleRate, float fundamentalHz)
{
    double cutoffHz = juce::jlimit(8.0, 40.0, (double) fundamentalHz * 0.4);
    if (fundamentalHz <= 0.0f)
        cutoffHz = 15.0;
    double alpha = 1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * cutoffHz / sampleRate);
    double smoothed = std::abs((double) data[0]);

    int decim = juce::jmax(1, (int) (sampleRate / 400.0));
    std::vector<float> envelope;
    envelope.reserve((size_t) (numSamples / decim) + 1);

    for (int i = 0; i < numSamples; ++i)
    {
        double rect = std::abs((double) data[i]);
        smoothed += alpha * (rect - smoothed);
        if (i % decim == 0)
            envelope.push_back((float) smoothed);
    }

    BeatStats stats;
    if (envelope.size() < 16)
        return stats;

    double mean = 0.0;
    for (auto v : envelope) mean += v;
    mean /= envelope.size();

    double envelopeSampleRate = sampleRate / decim;
    double segmentDuration = numSamples / sampleRate;

    double minBeatHz = juce::jmax(0.3, 1.5 / juce::jmax(0.2, segmentDuration));
    double beatSearchCeilingHz = cutoffHz * 0.9;
    int minLag = juce::jmax(1, (int) (envelopeSampleRate / beatSearchCeilingHz));
    int maxLag = (int) (envelopeSampleRate / minBeatHz);
    maxLag = juce::jmin(maxLag, (int) envelope.size() - 1);

    if (maxLag <= minLag)
        return stats;

    stats.reliable = segmentDuration >= (1.5 / minBeatHz) * 0.9;

    double bestScore = -1.0e30;
    int bestLag = -1;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        int count = (int) envelope.size() - lag;
        for (int i = 0; i < count; ++i)
            sum += (envelope[(size_t) i] - mean) * (envelope[(size_t) (i + lag)] - mean);
        sum /= count;

        if (sum > bestScore)
        {
            bestScore = sum;
            bestLag = lag;
        }
    }

    if (bestLag > 0)
        stats.beatHz = (float) (envelopeSampleRate / bestLag);

    double minV = mean, maxV = mean;
    for (auto v : envelope) { minV = juce::jmin(minV, (double) v); maxV = juce::jmax(maxV, (double) v); }
    stats.depthPercent = (float) (100.0 * (maxV - minV) / (maxV + minV + 1e-9));

    return stats;
}
} // namespace

juce::AudioBuffer<float> loadMono(const juce::File& file, double& sampleRateOut)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return {};

    sampleRateOut = reader->sampleRate;
    auto numSamples = (int) reader->lengthInSamples;

    juce::AudioBuffer<float> multiChannel((int) reader->numChannels, numSamples);
    reader->read(&multiChannel, 0, numSamples, 0, true, true);

    juce::AudioBuffer<float> mono(1, numSamples);
    mono.clear();
    for (int ch = 0; ch < multiChannel.getNumChannels(); ++ch)
        mono.addFrom(0, 0, multiChannel, ch, 0, numSamples, 1.0f / (float) multiChannel.getNumChannels());

    return mono;
}

juce::String noteName(float hz)
{
    if (hz <= 0.0f)
        return "?";
    double midiNote = 69.0 + 12.0 * std::log2(hz / 440.0);
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int rounded = (int) std::round(midiNote);
    int noteIndex = rounded % 12;
    int octave = rounded / 12 - 1;
    if (noteIndex < 0)
    {
        noteIndex += 12;
        --octave;
    }
    return juce::String(names[noteIndex]) + juce::String(octave);
}

// Segments the recording into individual notes by tracking pitch over time,
// not just silence - so it also works on legato/overlapping multi-note demos
// with no gaps between notes.
std::vector<NoteSegment> findNoteSegments(const float* data, int numSamples, double sampleRate)
{
    int hop = juce::jmax(1, (int) (sampleRate * 0.02));
    int win = juce::jmax(hop, (int) (sampleRate * 0.05));

    struct Frame { int startSample; float amplitude; float pitchHz; };
    std::vector<Frame> frames;
    for (int start = 0; start + win <= numSamples; start += hop)
    {
        double sum = 0.0;
        for (int j = 0; j < win; ++j) sum += std::abs(data[start + j]);
        frames.push_back({start, (float) (sum / win), 0.0f});
    }
    if (frames.empty())
        return {};

    float peak = 0.0f;
    for (auto& f : frames) peak = juce::jmax(peak, f.amplitude);
    float threshold = peak * 0.06f;

    for (auto& f : frames)
        if (f.amplitude > threshold)
            f.pitchHz = estimateFundamentalHz(data + f.startSample, win, sampleRate);

    constexpr double centsSplitThreshold = 60.0;
    constexpr int confirmFrames = 2;
    int maxGapFrames = juce::jmax(1, (int) (0.08 * sampleRate / hop));
    int minSegmentFrames = juce::jmax(1, (int) (0.15 * sampleRate / hop));

    std::vector<NoteSegment> segments;
    int n = (int) frames.size();
    int i = 0;
    while (i < n)
    {
        if (frames[(size_t) i].amplitude <= threshold) { ++i; continue; }

        int segStart = i;
        float refPitch = frames[(size_t) i].pitchHz;
        int lastActive = i;
        int j = i + 1;

        while (j < n)
        {
            if (frames[(size_t) j].amplitude <= threshold)
            {
                if (j - lastActive > maxGapFrames)
                    break;
                ++j;
                continue;
            }

            if (refPitch <= 0.0f)
                refPitch = frames[(size_t) j].pitchHz;

            if (refPitch > 0.0f && frames[(size_t) j].pitchHz > 0.0f)
            {
                double cents = 1200.0 * std::log2(frames[(size_t) j].pitchHz / refPitch);
                if (std::abs(cents) > centsSplitThreshold)
                {
                    bool sustained = true;
                    for (int k = 1; k <= confirmFrames && j + k < n; ++k)
                    {
                        auto& fk = frames[(size_t) (j + k)];
                        if (fk.amplitude <= threshold) { sustained = false; break; }
                        if (fk.pitchHz <= 0.0f) continue;
                        double cents2 = 1200.0 * std::log2(fk.pitchHz / frames[(size_t) j].pitchHz);
                        if (std::abs(cents2) > centsSplitThreshold) { sustained = false; break; }
                    }
                    if (sustained)
                        break;
                }
            }

            lastActive = j;
            ++j;
        }

        int startSample = frames[(size_t) segStart].startSample;
        int endSample = juce::jmin(numSamples, frames[(size_t) lastActive].startSample + win);

        if (lastActive - segStart + 1 >= minSegmentFrames)
            segments.push_back({startSample, endSample});

        i = j;
    }

    return segments;
}

SegmentReport analyzeSegment(const float* fullData, const NoteSegment& seg, double sampleRate)
{
    int length = seg.endSample - seg.startSample;
    int trim = juce::jmin(length / 4, (int) (sampleRate * 0.04));
    int start = seg.startSample + trim;
    int len = juce::jmax(1, length - 2 * trim);

    SegmentReport report;
    report.startTimeSec = seg.startSample / sampleRate;
    report.durationSec = length / sampleRate;
    report.spectral = computeSpectralStats(fullData + start, len, sampleRate);
    auto pitch = estimatePitchDetailed(fullData + start, len, sampleRate);
    report.fundamentalHz = pitch.frequencyHz;
    report.harmonicFitPercent = 100.0f * pitch.clarity;
    report.beat = detectAmplitudeBeating(fullData + start, len, sampleRate, report.fundamentalHz);

    float peakAbs = 0.0f;
    double sumSquares = 0.0;
    for (int i = 0; i < len; ++i)
    {
        peakAbs = juce::jmax(peakAbs, std::abs(fullData[start + i]));
        sumSquares += (double) fullData[start + i] * fullData[start + i];
    }
    double rms = std::sqrt(sumSquares / juce::jmax(1, len));
    report.crestFactorDb = (float) (20.0 * std::log10((peakAbs + 1e-9) / (rms + 1e-9)));

    return report;
}

// Rough attack/sustain/release shape from the segment's own amplitude envelope,
// used as a starting point for the ADSR knobs - not exact, but a reasonable guess.
EnvelopeTiming analyzeEnvelopeTiming(const float* fullData, const NoteSegment& seg, double sampleRate)
{
    int length = seg.endSample - seg.startSample;
    int hop = juce::jmax(1, (int) (sampleRate * 0.003));
    std::vector<float> env;
    for (int i = 0; i < length; i += hop)
    {
        int end = juce::jmin(i + hop, length);
        double sum = 0.0;
        for (int j = i; j < end; ++j) sum += std::abs(fullData[seg.startSample + j]);
        env.push_back((float) (sum / (end - i)));
    }

    EnvelopeTiming timing;
    if (env.size() < 4)
        return timing;

    float peak = 0.0f;
    int peakIndex = 0;
    for (int i = 0; i < (int) env.size(); ++i)
        if (env[(size_t) i] > peak) { peak = env[(size_t) i]; peakIndex = i; }

    if (peak <= 0.0f)
        return timing;

    int attackIndex = peakIndex;
    for (int i = 0; i <= peakIndex; ++i)
        if (env[(size_t) i] >= peak * 0.9f) { attackIndex = i; break; }
    timing.attackSec = juce::jmax(0.001f, (float) (attackIndex * hop / sampleRate));

    int n = (int) env.size();
    int sustainStart = (int) (n * 0.4);
    int sustainEnd = juce::jmax(sustainStart + 1, (int) (n * 0.8));
    double sustainSum = 0.0;
    int sustainCount = 0;
    for (int i = sustainStart; i < sustainEnd && i < n; ++i) { sustainSum += env[(size_t) i]; ++sustainCount; }
    timing.sustainLevelRatio = sustainCount > 0 ? juce::jlimit(0.0f, 1.0f, (float) (sustainSum / sustainCount / peak)) : 0.7f;

    int lastHigh = peakIndex;
    for (int i = n - 1; i >= 0; --i)
        if (env[(size_t) i] >= peak * 0.5f) { lastHigh = i; break; }
    int releaseEnd = n - 1;
    for (int i = lastHigh; i < n; ++i)
        if (env[(size_t) i] < peak * 0.1f) { releaseEnd = i; break; }
    timing.releaseSec = juce::jmax(0.001f, (float) ((releaseEnd - lastHigh) * hop / sampleRate));

    return timing;
}

// Maps measured acoustic features onto WaveLathe's own parameters. This is an
// approximation - an inverse-problem estimate from the rendered audio, the
// same kind of judgment call a sound designer makes dialing in a patch by
// ear from a reference, not an exact solve. Wave Position and Resonance in
// particular can't be inferred reliably from a single spectral snapshot and
// are left at sensible defaults to tune by ear.
PresetValues estimatePreset(const SegmentReport& report, const EnvelopeTiming& timing, const juce::String& name)
{
    PresetValues v;
    v.name = name;

    v.filterCutoffHz = juce::jlimit(200.0f, 18000.0f,
                                     report.spectral.rolloff85Hz > 0.0f ? report.spectral.rolloff85Hz : 6000.0f);
    v.filterResonance = 0.5f;

    if (report.beat.reliable && report.beat.beatHz > 0.0f && report.fundamentalHz > 0.0f
        && report.beat.depthPercent > 15.0f)
    {
        double detuneCents = 1200.0 * std::log2(1.0 + report.beat.beatHz / report.fundamentalHz);
        if (report.beat.beatHz < 2.0f)
        {
            v.lfoRateHz = report.beat.beatHz;
            v.lfoDepth = juce::jlimit(0.0f, 1.0f, report.beat.depthPercent / 100.0f * 0.7f);
            v.unisonVoices = 2.0f;
            v.unisonDetuneCents = juce::jlimit(1.0f, 15.0f, (float) detuneCents);
            v.unisonWidth = 0.3f;
        }
        else
        {
            v.unisonVoices = 3.0f;
            v.unisonDetuneCents = juce::jlimit(1.0f, 50.0f, (float) detuneCents);
            v.unisonWidth = juce::jlimit(0.2f, 1.0f, report.beat.depthPercent / 100.0f);
        }
    }

    // Undriven wavetable content (sine/saw/square-ish shapes) naturally sits
    // around 3-5 dB crest factor on its own - a clean sine is ~3dB, a clean
    // saw ~4.77dB, so that's the "no distortion" baseline, not silence-quiet
    // territory. crestFactorDb can never go below 0dB (RMS can't exceed peak,
    // by definition), so 1dB - close to a fully clipped square wave - is
    // about as "driven" as a real signal gets; anything lower is not
    // physically reachable. Capped conservatively since this is a rough
    // starting point, not a precise measurement, and drive is easy to
    // underestimate wrong but very audible to overestimate wrong.
    constexpr double cleanCrestFactorDb = 5.0;
    constexpr double heavilyDrivenCrestFactorDb = 1.0;
    double driveRatio = (cleanCrestFactorDb - report.crestFactorDb) / (cleanCrestFactorDb - heavilyDrivenCrestFactorDb);
    v.driveAmount = juce::jlimit(0.0f, 0.5f, (float) driveRatio);

    v.attack = juce::jlimit(0.001f, 3.0f, timing.attackSec);
    v.decay = juce::jlimit(0.001f, 3.0f, timing.attackSec * 2.0f + 0.05f);
    v.sustain = juce::jlimit(0.0f, 1.0f, timing.sustainLevelRatio);
    v.release = juce::jlimit(0.001f, 5.0f, timing.releaseSec);

    v.wavePosition = 0.5f;
    v.masterGain = 0.7f;

    return v;
}

int midiNoteForFrequency(float hz)
{
    if (hz <= 0.0f)
        return 0;
    return juce::jlimit(0, 127, (int) std::round(69.0 + 12.0 * std::log2(hz / 440.0)));
}

bool analyzeFile(const juce::File& wavFile, int noteIndex, MatchContext& outContext, juce::String& errorMessage)
{
    double sampleRate = 0.0;
    auto mono = loadMono(wavFile, sampleRate);
    if (mono.getNumSamples() == 0)
    {
        errorMessage = "Could not read audio from this file.";
        return false;
    }

    auto* data = mono.getReadPointer(0);
    int numSamples = mono.getNumSamples();

    auto segments = findNoteSegments(data, numSamples, sampleRate);
    if (segments.empty())
        segments.push_back({0, numSamples});

    std::vector<SegmentReport> reports;
    for (auto& seg : segments)
        reports.push_back(analyzeSegment(data, seg, sampleRate));

    int idx = noteIndex;
    if (idx < 0 || idx >= (int) reports.size())
    {
        // Pick the segment that is both long enough to analyse and clearly a
        // single pitch. Picking purely by duration can land on a chord or an
        // overlap, whose detected pitch is meaningless - and everything
        // downstream (wavetable sampling, optimisation) inherits that error.
        idx = 0;
        double bestScore = -1.0;
        for (int i = 0; i < (int) reports.size(); ++i)
        {
            const auto& r = reports[(size_t) i];
            double durationScore = juce::jmin(r.durationSec, 1.5) / 1.5;
            double clarityScore = juce::jlimit(0.0, 1.0, (double) r.harmonicFitPercent / 100.0);
            double score = durationScore * (0.25 + 0.75 * clarityScore * clarityScore);

            if (score > bestScore)
            {
                bestScore = score;
                idx = i;
            }
        }
    }

    outContext.mono = std::move(mono);
    outContext.sampleRate = sampleRate;
    outContext.segments = std::move(segments);
    outContext.reports = std::move(reports);
    outContext.chosenIndex = idx;
    return true;
}

int windowWithTail(const MatchContext& context, int segmentIndex, double tailSeconds, float& outHoldRatio)
{
    const auto& seg = context.segments[(size_t) segmentIndex];
    int noteLength = seg.endSample - seg.startSample;

    // Only extend into a genuine gap. In a riff the next note starts almost
    // immediately, and including it would ask the synth to reproduce a note it
    // was never given - which scores terribly and drags every other setting
    // off course.
    int limit = context.mono.getNumSamples();
    if (segmentIndex + 1 < (int) context.segments.size())
        limit = juce::jmin(limit, context.segments[(size_t) segmentIndex + 1].startSample);

    int availableTail = juce::jmax(0, limit - seg.endSample);
    int tailSamples = juce::jmin((int) (tailSeconds * context.sampleRate), availableTail);
    int windowLength = juce::jmax(1, noteLength + tailSamples);

    outHoldRatio = juce::jlimit(0.05f, 0.98f, (float) noteLength / (float) windowLength);
    return windowLength;
}

bool writeAbComparison(const juce::File& outputFile, const float* reference, int referenceNumSamples,
                       const float* rendered, int renderedNumSamples, double sampleRate,
                       juce::String& errorMessage)
{
    if (referenceNumSamples <= 0 || renderedNumSamples <= 0)
    {
        errorMessage = "Nothing to compare.";
        return false;
    }

    auto normalisedCopy = [](const float* source, int count)
    {
        std::vector<float> out(source, source + count);
        double sumSquares = 0.0;
        for (auto v : out) sumSquares += (double) v * v;
        double rms = std::sqrt(sumSquares / juce::jmax(1, count));
        if (rms > 1.0e-9)
        {
            float scale = (float) (0.15 / rms);
            for (auto& v : out) v *= scale;
        }
        // Guard against clipping after level matching.
        float peak = 0.0f;
        for (auto v : out) peak = juce::jmax(peak, std::abs(v));
        if (peak > 0.99f)
            for (auto& v : out) v *= 0.99f / peak;
        return out;
    };

    auto referenceNormalised = normalisedCopy(reference, referenceNumSamples);
    auto renderedNormalised = normalisedCopy(rendered, renderedNumSamples);

    int gapSamples = (int) (sampleRate * 0.35);
    int total = referenceNumSamples + gapSamples + renderedNumSamples;

    juce::AudioBuffer<float> output(1, total);
    output.clear();
    output.copyFrom(0, 0, referenceNormalised.data(), referenceNumSamples);
    output.copyFrom(0, referenceNumSamples + gapSamples, renderedNormalised.data(), renderedNumSamples);

    outputFile.getParentDirectory().createDirectory();
    if (outputFile.existsAsFile() && !outputFile.deleteFile())
    {
        errorMessage = "Could not overwrite " + outputFile.getFileName();
        return false;
    }

    auto file = std::make_unique<juce::FileOutputStream>(outputFile);
    if (!file->openedOk())
    {
        errorMessage = "Could not open " + outputFile.getFileName() + " for writing.";
        return false;
    }

    // The options-taking createWriterFor, rather than the six-argument one that
    // JUCE 9 deprecated. Worth more than silencing a warning: the old form took
    // a raw pointer and left ownership to be settled afterwards by hand, with a
    // release() that had to be skipped on failure and not skipped on success.
    // This one takes the unique_ptr and moves out of it only if it succeeded,
    // so the two cases cannot be got the wrong way round.
    std::unique_ptr<juce::OutputStream> stream = std::move(file);

    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withNumChannels(1)
                             .withBitsPerSample(24);

    auto writer = format.createWriterFor(stream, options);

    if (writer == nullptr)
    {
        errorMessage = "Could not create a WAV writer.";
        return false;
    }

    writer->writeFromAudioSampleBuffer(output, 0, total);
    return true;
}

bool matchFile(const juce::File& wavFile, const juce::String& presetName, int noteIndex, PresetValues& outValues,
               juce::String& errorMessage, std::vector<SegmentReport>* outAllReports)
{
    MatchContext context;
    if (!analyzeFile(wavFile, noteIndex, context, errorMessage))
        return false;

    if (outAllReports != nullptr)
        *outAllReports = context.reports;

    auto timing = analyzeEnvelopeTiming(context.mono.getReadPointer(0), context.segments[(size_t) context.chosenIndex],
                                        context.sampleRate);
    outValues = estimatePreset(context.reports[(size_t) context.chosenIndex], timing, presetName);
    return true;
}
} // namespace SampleMatch
} // namespace wavelathe
