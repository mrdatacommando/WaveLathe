// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PresetOptimizer.h"
#include <cstring>
#include "OfflineRenderer.h"
#include "FactoryPresets.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <thread>

namespace wavelathe
{
namespace SoundMatch
{
namespace
{
constexpr int numMelBands = 40;
constexpr int envelopePoints = 128;
constexpr int modulationBins = 64;
constexpr double modulationMinHz = 0.3;
constexpr double modulationMaxHz = 40.0;

double hzToMel(double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); }
double melToHz(double mel) { return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0); }

// Triangular mel filterbank weights, built once per (fftSize, sampleRate).
struct MelFilterBank
{
    int fftSize = 0;
    double sampleRate = 0.0;
    std::vector<std::vector<float>> weights; // [band][bin]
    std::vector<int> binStart, binEnd;

    MelFilterBank(int fftSizeToUse, double sampleRateToUse, double lowHz, double highHz)
        : fftSize(fftSizeToUse), sampleRate(sampleRateToUse)
    {
        int numBins = fftSize / 2;
        double binHz = sampleRate / fftSize;

        double lowMel = hzToMel(lowHz);
        double highMel = hzToMel(juce::jmin(highHz, sampleRate * 0.5 - 1.0));

        std::vector<double> centres((size_t) numMelBands + 2);
        for (int i = 0; i < numMelBands + 2; ++i)
            centres[(size_t) i] = melToHz(lowMel + (highMel - lowMel) * i / (numMelBands + 1));

        weights.resize(numMelBands);
        binStart.resize(numMelBands);
        binEnd.resize(numMelBands);

        for (int band = 0; band < numMelBands; ++band)
        {
            double left = centres[(size_t) band];
            double centre = centres[(size_t) band + 1];
            double right = centres[(size_t) band + 2];

            int b0 = juce::jlimit(0, numBins - 1, (int) std::floor(left / binHz));
            int b1 = juce::jlimit(0, numBins - 1, (int) std::ceil(right / binHz));
            binStart[(size_t) band] = b0;
            binEnd[(size_t) band] = b1;

            auto& w = weights[(size_t) band];
            w.assign((size_t) (b1 - b0 + 1), 0.0f);

            for (int bin = b0; bin <= b1; ++bin)
            {
                double f = bin * binHz;
                double value = 0.0;
                if (f >= left && f <= centre && centre > left)
                    value = (f - left) / (centre - left);
                else if (f > centre && f <= right && right > centre)
                    value = (right - f) / (right - centre);
                w[(size_t) (bin - b0)] = (float) value;
            }
        }
    }
};

const MelFilterBank& getMelBank(int fftSize, double sampleRate)
{
    // One bank per resolution; sample rate is fixed within a matching run.
    static std::vector<std::unique_ptr<MelFilterBank>> cache;
    static std::mutex cacheMutex;

    std::lock_guard<std::mutex> lock(cacheMutex);
    for (auto& bank : cache)
        if (bank->fftSize == fftSize && std::abs(bank->sampleRate - sampleRate) < 1.0)
            return *bank;

    cache.push_back(std::make_unique<MelFilterBank>(fftSize, sampleRate, 40.0, 16000.0));
    return *cache.back();
}

std::vector<float> computeLogMel(const float* data, int numSamples, double sampleRate, int fftSize, int& outFrames)
{
    outFrames = 0;
    if (numSamples < fftSize)
        return {};

    int order = (int) std::round(std::log2((double) fftSize));
    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann);

    const auto& bank = getMelBank(fftSize, sampleRate);
    int hop = fftSize / 2;

    std::vector<float> result;
    std::vector<float> frame((size_t) fftSize * 2, 0.0f);

    for (int start = 0; start + fftSize <= numSamples; start += hop)
    {
        std::fill(frame.begin(), frame.end(), 0.0f);
        std::copy(data + start, data + start + fftSize, frame.begin());
        window.multiplyWithWindowingTable(frame.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform(frame.data());

        for (int band = 0; band < numMelBands; ++band)
        {
            int b0 = bank.binStart[(size_t) band];
            int b1 = bank.binEnd[(size_t) band];
            const auto& w = bank.weights[(size_t) band];

            double energy = 0.0;
            for (int bin = b0; bin <= b1; ++bin)
                energy += (double) frame[(size_t) bin] * w[(size_t) (bin - b0)];

            result.push_back((float) (20.0 * std::log10(energy + 1e-6)));
        }

        ++outFrames;
    }

    return result;
}

std::vector<float> computeEnvelopeDb(const float* data, int numSamples, int points)
{
    std::vector<float> env((size_t) points, -120.0f);
    if (numSamples <= 0)
        return env;

    for (int p = 0; p < points; ++p)
    {
        int start = (int) ((double) p / points * numSamples);
        int end = (int) ((double) (p + 1) / points * numSamples);
        end = juce::jmax(end, start + 1);
        end = juce::jmin(end, numSamples);

        double sumSquares = 0.0;
        int count = 0;
        for (int i = start; i < end; ++i) { sumSquares += (double) data[i] * data[i]; ++count; }

        double rms = count > 0 ? std::sqrt(sumSquares / count) : 0.0;
        env[(size_t) p] = (float) (20.0 * std::log10(rms + 1e-6));
    }

    return env;
}

std::vector<float> computeModulationSpectrum(const float* data, int numSamples, double sampleRate)
{
    // Rectify + smooth to get an amplitude envelope at a low sample rate,
    // then look at its own spectrum to expose beating / LFO movement.
    constexpr double envCutoffHz = 60.0;
    double alpha = 1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * envCutoffHz / sampleRate);
    double smoothed = 0.0;

    int decim = juce::jmax(1, (int) (sampleRate / 400.0));
    double envSampleRate = sampleRate / decim;

    std::vector<float> env;
    env.reserve((size_t) (numSamples / decim) + 1);
    for (int i = 0; i < numSamples; ++i)
    {
        smoothed += alpha * (std::abs((double) data[i]) - smoothed);
        if (i % decim == 0)
            env.push_back((float) smoothed);
    }

    std::vector<float> result((size_t) modulationBins, 0.0f);
    if (env.size() < 32)
        return result;

    double mean = 0.0;
    for (auto v : env) mean += v;
    mean /= env.size();

    // Normalise by mean so this measures *relative* modulation depth, not level.
    for (auto& v : env) v = mean > 1e-9 ? (float) ((v - mean) / mean) : 0.0f;

    int order = 10; // 1024-point
    int fftSize = 1 << order;
    std::vector<float> frame((size_t) fftSize * 2, 0.0f);
    int n = juce::jmin((int) env.size(), fftSize);
    std::copy(env.begin(), env.begin() + n, frame.begin());

    juce::dsp::WindowingFunction<float> window((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann);
    window.multiplyWithWindowingTable(frame.data(), (size_t) fftSize);
    juce::dsp::FFT fft(order);
    fft.performFrequencyOnlyForwardTransform(frame.data());

    double binHz = envSampleRate / fftSize;
    for (int b = 0; b < modulationBins; ++b)
    {
        double f0 = modulationMinHz + (modulationMaxHz - modulationMinHz) * b / modulationBins;
        double f1 = modulationMinHz + (modulationMaxHz - modulationMinHz) * (b + 1) / modulationBins;
        int i0 = juce::jlimit(0, fftSize / 2 - 1, (int) (f0 / binHz));
        int i1 = juce::jlimit(i0, fftSize / 2 - 1, (int) (f1 / binHz));

        double sum = 0.0;
        for (int i = i0; i <= i1; ++i) sum += frame[(size_t) i];

        // Left as a linear magnitude, which is not an oversight. It makes this
        // term carry about 93% of the distance between a chorused sound and a
        // detuned one, and it swings hard as a comb's notches slide past the
        // harmonics - a measure the search can see clearly but cannot climb.
        //
        // Compressing it was tried three ways and each was measured: log1p, a
        // cube root, and a cube root with the term's weight raised to put back
        // the influence the compression took away. Compression does exactly
        // what it promises to the landscape - the chorus mix sweep goes from a
        // ridge with a hole in the middle, ###==_==#####=_._=###, to a clean
        // basin bottoming out on the true value, ###=========___.__==# - and
        // the cube root even improved on the chorus told apart from a detune
        // (0.122 against 0.093) and the wavetable position (0.191 against
        // 0.324 of error).
        //
        // It cost the filter, on both test targets, in every variant: the dry
        // pluck's cutoff went from 1.6 octaves out to 2.0-2.3 whatever was
        // tried, and restoring the weight made the search so keen on movement
        // it hung a phaser on a sound that has none. Smoother did not mean
        // better. If this is revisited, the thing to beat is a filter within
        // 1.6 octaves on BOTH targets while keeping the chorus told apart.
        result[(size_t) b] = (float) (sum / (i1 - i0 + 1));
    }

    return result;
}

void normaliseLoudness(std::vector<float>& data)
{
    double sumSquares = 0.0;
    for (auto v : data) sumSquares += (double) v * v;
    double rms = data.empty() ? 0.0 : std::sqrt(sumSquares / data.size());
    if (rms < 1e-9)
        return;
    float scale = (float) (0.1 / rms);
    for (auto& v : data) v *= scale;
}

float meanAbsDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    size_t n = juce::jmin(a.size(), b.size());
    if (n == 0)
        return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) sum += std::abs((double) a[i] - b[i]);
    return (float) (sum / n);
}

// ---- Parameter space -------------------------------------------------------

struct ParamSpec
{
    const char* name;

    // The field this spec drives. Holding it here rather than repeating the
    // order in a decode and an encode function is what stops the three lists
    // drifting apart: there is now one list, and adding a parameter is one
    // line rather than three that have to agree on an index.
    float PresetValues::* member;

    float minValue;
    float maxValue;
    bool logScale;

    // Controls that only take whole values - a voice count, a semitone, a
    // filter type - so the search does not hand them a fraction. These are
    // also exempt from the preference against extremes below: the top of a
    // list of filter types is a choice, not an extreme.
    bool rounded = false;

    // Optional contributions (an effect mix, an extra source, a modulation
    // depth) are nudged toward "off" unless they actually earn their place.
    // Without this, a parameter the objective cannot observe drifts to an
    // arbitrary value that is silent during matching but loud when played.
    // neutralNormalised is where "off" sits in 0..1 space: 0 for a plain
    // amount, 0.5 for a bipolar depth centred on zero.
    bool preferNeutral = false;
    float neutralNormalised = 0.0f;
};

// Which stage of the search a parameter is fitted in: what the sound is, then
// what moves it, then the chain it passes through afterwards.
//
// Decided by name rather than by more flags in the table above. That table's
// fields are positional with defaults, so a flag landing one slot out is
// silent - it quietly turned a delay feedback into something that could only
// be nought or one, and nothing complained. It happened twice while this was
// being written, which is reason enough to keep the classification somewhere
// it can only be right or obviously wrong.
enum class Stage
{
    voice,
    modulation,
    effect
};

// Which stage a parameter is fitted in.
//
// The split is not "voice versus master chain", which is where it started and
// which cost the search the chorus every time: fitted dry, the voice imitates a
// chorus with unison detune, and once it has, the real chorus no longer fits
// the voice that stood in for it - measurably so, the distance climbing from 80
// to 120 as the chorus is brought up around the patch the search settled on.
//
// The split that matters is what can stand in for what. Detune, LFOs, a chorus
// and a phaser all make a sound move, and are alternatives to one another, so
// they are chosen together and the search can trade one for another in a single
// step. The equaliser is held back to the end because it is the one control
// that genuinely duplicates the filter's job - a wrong cutoff cancelled by a
// wrong shelf was the failure staging was introduced to stop.
// Which stage a parameter is fitted in: the static voice, then what moves it,
// then the master chain.
//
// An obvious-looking rearrangement of this was tried and rejected on the
// numbers. The complaint it was meant to answer is real: the voice is fitted
// with the chain dry, so it imitates a chorus with unison detune, and once it
// has, the real chorus no longer fits the voice that stood in for it - around
// the patch the search settles on, bringing the chorus up drives the distance
// from 80 to 120. Moving the chorus and the detune into one stage, so they
// could be traded against each other in a single step, did raise the chorus
// recovered on a chorused target from 0.09 to 0.34.
//
// It also raised the chorus invented on a target with NO chorus from 0.00 to
// 0.25. The gap between the two - what the search actually knows about whether
// a chorus is there - went from 0.093 to 0.087, which is to say it did not
// move. The rearrangement made the search fonder of chorus, not better at
// hearing it, and cost an octave of filter accuracy for the privilege. A
// one-target measurement would have called it a win.
//
// Whatever fixes this properly has to widen that gap, which most likely means
// the objective needs a feature that separates a chorus from a detune rather
// than the stages needing reshuffling.
Stage stageForName(const char* name)
{
    static const char* const movement[] = {
        "lfoRateHz",   "lfoDepth",      "lfoAmpDepth",   "lfoToWave",    "modEnvAttack",
        "modEnvDecay", "modEnvSustain", "modEnvRelease", "modEnvToWave", "modEnvToCutoff",
        "lfo2RateHz",  "lfo2ToWave",    "lfo2ToCutoff"};

    static const char* const chain[] = {
        "fxDistortion",   "delayTimeMs", "delayFeedback", "delayMix",  "reverbSize",
        "reverbMix",      "chorusRate",  "chorusDepth",   "chorusMix", "phaserRate",
        "phaserFeedback", "phaserMix",   "eqLowGain",     "eqMidGain", "eqHighGain"};

    for (const auto* n : movement)
        if (std::strcmp(n, name) == 0)
            return Stage::modulation;

    for (const auto* n : chain)
        if (std::strcmp(n, name) == 0)
            return Stage::effect;

    return Stage::voice;
}

// What a completed match typically scores, which is the scale the two nudges
// below are measured against.
//
// This coupling is easy to miss and has caught us twice. Compressing the
// modulation spectrum once dropped the typical distance from about 75 to about
// 19, quadrupling what these nudges were really worth without a line of them
// changing. So they are written as ratios against this number now rather than as
// bare constants, and reweighting a feature rescales them along with it.
//
// ANYONE WHO ADDS OR REWEIGHTS A FEATURE TERM must re-measure this and put the
// new number here. MatchProbe's "voice recovery" section prints it as the mean.
constexpr float tunedAtDistance = 75.0f; // where the two ratios below were set
constexpr float typicalDistance = 75.0f; // what a match scores now

// Small enough that it only decides between otherwise comparable matches.
constexpr float neutralPreferenceWeight = 0.04f * (typicalDistance / tunedAtDistance);

// How close to an end counts as jammed against it, and what that costs.
constexpr float railMargin = 0.04f;
constexpr float railPreferenceWeight = 0.05f * (typicalDistance / tunedAtDistance);

// unisonWidth, stereoLfo and masterGain are deliberately excluded: the first
// two are purely stereo effects and the objective compares mono, and gain is
// neutralised by loudness normalisation - so none of them is observable here
// and searching them would only add noise. Glide, mono and legato are excluded
// for the same reason: they describe how one note leads to the next, and the
// reference is a single note.
//
// The master effects ARE included: reference recordings usually carry some
// reverb or delay, and without these the optimiser has to distort the
// oscillator and envelope settings trying to imitate a tail it cannot produce.
const ParamSpec paramSpecs[] = {
    {"wavePosition", &PresetValues::wavePosition, 0.0f, 1.0f, false},
    {"attack", &PresetValues::attack, 0.001f, 3.0f, true},
    {"decay", &PresetValues::decay, 0.001f, 3.0f, true},
    {"sustain", &PresetValues::sustain, 0.0f, 1.0f, false},
    // Capped shorter than the dial allows, and nudged toward a short release:
    // when a note is followed immediately by another there is no tail to
    // measure, and an unconstrained release drifts long and rings on.
    {"release", &PresetValues::release, 0.001f, 2.0f, true, false, true, 0.0f},
    {"filterCutoffHz", &PresetValues::filterCutoffHz, 50.0f, 20000.0f, true},
    {"filterResonance", &PresetValues::filterResonance, 0.1f, 1.0f, false},
    {"lfoRateHz", &PresetValues::lfoRateHz, 0.05f, 20.0f, true},
    {"lfoDepth", &PresetValues::lfoDepth, 0.0f, 1.0f, false, false, true, 0.0f},
    {"lfoAmpDepth", &PresetValues::lfoAmpDepth, 0.0f, 1.0f, false, false, true, 0.0f},
    {"unisonVoices", &PresetValues::unisonVoices, 1.0f, 7.0f, false, true},
    {"unisonDetuneCents", &PresetValues::unisonDetuneCents, 0.0f, 50.0f, false, false, true, 0.0f},
    {"driveAmount", &PresetValues::driveAmount, 0.0f, 1.0f, false, false, true, 0.0f},
    {"fxDistortion", &PresetValues::fxDistortion, 0.0f, 1.0f, false, false, true, 0.0f},
    {"delayTimeMs", &PresetValues::delayTimeMs, 10.0f, 900.0f, true},
    {"delayFeedback", &PresetValues::delayFeedback, 0.0f, 0.95f, false},
    {"delayMix", &PresetValues::delayMix, 0.0f, 1.0f, false, false, true, 0.0f},
    {"reverbSize", &PresetValues::reverbSize, 0.0f, 1.0f, false},
    {"reverbMix", &PresetValues::reverbMix, 0.0f, 1.0f, false, false, true, 0.0f},
    // Modulation: without these the wavetable position and cutoff are frozen
    // for the whole note, which no amount of tuning the static values can
    // stand in for.
    {"lfoToWave", &PresetValues::lfoToWave, 0.0f, 1.0f, false, false, true, 0.0f},
    {"modEnvAttack", &PresetValues::modEnvAttack, 0.001f, 3.0f, true},
    {"modEnvDecay", &PresetValues::modEnvDecay, 0.001f, 3.0f, true},
    {"modEnvSustain", &PresetValues::modEnvSustain, 0.0f, 1.0f, false},
    {"modEnvRelease", &PresetValues::modEnvRelease, 0.001f, 5.0f, true},
    {"modEnvToWave", &PresetValues::modEnvToWave, -1.0f, 1.0f, false, false, true, 0.5f},
    {"modEnvToCutoff", &PresetValues::modEnvToCutoff, -1.0f, 1.0f, false, false, true, 0.5f},
    {"lfo2RateHz", &PresetValues::lfo2RateHz, 0.05f, 20.0f, true},
    {"lfo2ToWave", &PresetValues::lfo2ToWave, 0.0f, 1.0f, false, false, true, 0.0f},
    {"lfo2ToCutoff", &PresetValues::lfo2ToCutoff, 0.0f, 1.0f, false, false, true, 0.0f},
    // Sources and filter character.
    {"filterType", &PresetValues::filterType, 0.0f, 4.0f, false, true},
    {"osc1Level", &PresetValues::osc1Level, 0.0f, 1.0f, false},
    {"osc2Level", &PresetValues::osc2Level, 0.0f, 1.0f, false, false, true, 0.0f},
    {"osc2WavePosition", &PresetValues::osc2WavePosition, 0.0f, 1.0f, false},
    {"osc2Semitones", &PresetValues::osc2Semitones, -24.0f, 24.0f, false, true},
    {"osc2Fine", &PresetValues::osc2Fine, -100.0f, 100.0f, false},
    {"subLevel", &PresetValues::subLevel, 0.0f, 1.0f, false, false, true, 0.0f},
    {"subWave", &PresetValues::subWave, 0.0f, 1.0f, false},
    {"subOctave", &PresetValues::subOctave, 1.0f, 2.0f, false, true},
    {"noiseLevel", &PresetValues::noiseLevel, 0.0f, 1.0f, false, false, true, 0.0f},
    {"noiseColour", &PresetValues::noiseColour, 0.0f, 1.0f, false},

    // Movement the oscillators cannot fake. Detuned unison beats; a chorus
    // sweeps a delayed copy, which combs the spectrum as well as thickening
    // it. Made to stand in for one another, the search pushes unison detune
    // to extremes and still gets the wrong kind of movement. The objective
    // already measures this - the modulation spectrum is there precisely to
    // catch it - so the engine simply needed to be able to produce it.
    {"chorusRate", &PresetValues::chorusRate, 0.05f, 8.0f, true},
    {"chorusDepth", &PresetValues::chorusDepth, 0.0f, 1.0f, false},
    {"chorusMix", &PresetValues::chorusMix, 0.0f, 1.0f, false, false, true, 0.0f},
    {"phaserRate", &PresetValues::phaserRate, 0.05f, 8.0f, true},
    {"phaserFeedback", &PresetValues::phaserFeedback, 0.0f, 0.9f, false},
    {"phaserMix", &PresetValues::phaserMix, 0.0f, 1.0f, false, false, true, 0.0f},

    // Tonal balance, which the filter alone cannot express. A recording that
    // is bright at the top and scooped in the middle used to be chased by
    // bending cutoff, resonance and wavetable position - settings that then
    // had to be wrong for the timbre. Three broad shelves let the balance be
    // corrected where it belongs and free the oscillator to match the tone.
    {"eqLowGain", &PresetValues::eqLowGain, -12.0f, 12.0f, false, false, true, 0.5f},
    {"eqMidGain", &PresetValues::eqMidGain, -12.0f, 12.0f, false, false, true, 0.5f},
    {"eqHighGain", &PresetValues::eqHighGain, -12.0f, 12.0f, false, false, true, 0.5f},
};
constexpr int numParams = (int) (sizeof(paramSpecs) / sizeof(paramSpecs[0]));

float decodeParam(int index, float normalised)
{
    const auto& spec = paramSpecs[index];
    normalised = juce::jlimit(0.0f, 1.0f, normalised);

    float value = spec.logScale
                      ? spec.minValue * std::pow(spec.maxValue / spec.minValue, normalised)
                      : spec.minValue + normalised * (spec.maxValue - spec.minValue);

    return spec.rounded ? std::round(value) : value;
}

float encodeParam(int index, float value)
{
    const auto& spec = paramSpecs[index];
    value = juce::jlimit(spec.minValue, spec.maxValue, value);
    if (spec.logScale)
        return (float) (std::log(value / spec.minValue) / std::log(spec.maxValue / spec.minValue));
    return (value - spec.minValue) / (spec.maxValue - spec.minValue);
}

PresetValues decodeVector(const std::vector<float>& v, const PresetValues& base)
{
    PresetValues p = base;

    for (int i = 0; i < numParams; ++i)
        p.*(paramSpecs[i].member) = decodeParam(i, v[(size_t) i]);

    return p;
}

std::vector<float> encodePreset(const PresetValues& p)
{
    std::vector<float> v((size_t) numParams);

    for (int i = 0; i < numParams; ++i)
        v[(size_t) i] = encodeParam(i, p.*(paramSpecs[i].member));

    return v;
}
} // namespace

int numSearchedParameters() { return numParams; }

const char* searchedParameterName(int index)
{
    return paramSpecs[juce::jlimit(0, numParams - 1, index)].name;
}

bool isParameterSearched(const juce::String& name)
{
    for (int i = 0; i < numParams; ++i)
        if (name == paramSpecs[i].name)
            return true;

    return false;
}

float normalisedValueOf(const PresetValues& values, int index)
{
    index = juce::jlimit(0, numParams - 1, index);
    return encodeParam(index, values.*(paramSpecs[index].member));
}

void setNormalisedValueOf(PresetValues& values, int index, float normalised)
{
    index = juce::jlimit(0, numParams - 1, index);
    values.*(paramSpecs[index].member) = decodeParam(index, normalised);
}

const char* parameterStage(int index)
{
    switch (stageForName(paramSpecs[juce::jlimit(0, numParams - 1, index)].name))
    {
        case Stage::modulation: return "modulation";
        case Stage::effect: return "effect";
        default: return "voice";
    }
}


// ---- Settings that only matter when something else is switched on ----------
// A second oscillator's tuning does nothing while its level is zero; a reverb's
// size does nothing at zero mix. The search cannot hear those settings at all
// in that state - measured against a plain target, 20 of the 49 were inaudible
// for exactly this reason - so it leaves them wherever they happened to land.
// That is silent while matching and audible the moment the parent is turned up:
// raise the second oscillator after a match and it arrives seventeen semitones
// sharp, because nothing ever had cause to put it anywhere sensible.
//
// So after the search, anything still switched off has its dependants put back
// to a sane value. This cannot change how the patch sounds - that is what being
// inaudible means, and the test checks the distance is untouched - it only
// stops the match handing over settings nobody chose.
struct Dependency
{
    const char* child;
    const char* parents[3]; // inert while every one of these is at its "off" value
};

const Dependency dependencies[] = {
    {"osc2WavePosition", {"osc2Level"}},
    {"osc2Semitones", {"osc2Level"}},
    {"osc2Fine", {"osc2Level"}},
    {"subWave", {"subLevel"}},
    {"subOctave", {"subLevel"}},
    {"noiseColour", {"noiseLevel"}},
    {"chorusRate", {"chorusMix"}},
    {"chorusDepth", {"chorusMix"}},
    {"phaserRate", {"phaserMix"}},
    {"phaserFeedback", {"phaserMix"}},
    {"delayTimeMs", {"delayMix"}},
    {"delayFeedback", {"delayMix"}},
    {"reverbSize", {"reverbMix"}},
    // The rate matters only if something is listening to that oscillator, and
    // the envelope's shape only if it is routed anywhere.
    {"lfoRateHz", {"lfoDepth", "lfoAmpDepth", "lfoToWave"}},
    {"lfo2RateHz", {"lfo2ToWave", "lfo2ToCutoff"}},
    {"modEnvAttack", {"modEnvToWave", "modEnvToCutoff"}},
    {"modEnvDecay", {"modEnvToWave", "modEnvToCutoff"}},
    {"modEnvSustain", {"modEnvToWave", "modEnvToCutoff"}},
    {"modEnvRelease", {"modEnvToWave", "modEnvToCutoff"}},
};

int indexOfParameter(const char* name)
{
    for (int i = 0; i < numParams; ++i)
        if (std::strcmp(paramSpecs[i].name, name) == 0)
            return i;
    return -1;
}
bool isParameterAtNeutral(const PresetValues& values, int index)
{
    index = juce::jlimit(0, numParams - 1, index);
    const auto& spec = paramSpecs[index];

    if (!spec.preferNeutral)
        return false;

    return std::abs(normalisedValueOf(values, index) - spec.neutralNormalised) < 0.01f;
}


int tidyInactiveParameters(PresetValues& values)
{
    PresetValues defaults;
    int tidied = 0;

    for (const auto& dependency : dependencies)
    {
        int child = indexOfParameter(dependency.child);
        if (child < 0)
            continue;

        // Inert only while EVERY listed parent is off. One live parent is
        // enough for the setting to be doing something.
        bool inert = true;
        bool hadParent = false;

        for (const auto* parentName : dependency.parents)
        {
            if (parentName == nullptr)
                break;

            int parent = indexOfParameter(parentName);
            if (parent < 0)
                continue;

            hadParent = true;
            if (!isParameterAtNeutral(values, parent))
            {
                inert = false;
                break;
            }
        }

        if (!hadParent || !inert)
            continue;

        float current = normalisedValueOf(values, child);
        float wanted = normalisedValueOf(defaults, child);

        if (std::abs(current - wanted) > 0.01f)
        {
            setNormalisedValueOf(values, child, wanted);
            ++tidied;
        }
    }

    return tidied;
}

// ---- Settings that did not earn their place -----------------------------
// A different problem from the one above, and it needs a different remedy.
//
// tidyInactiveParameters moves settings that are provably INAUDIBLE - a
// chorus rate with the chorus at zero mix. Nothing can go wrong there, so
// nothing is measured. This is about settings that are perfectly audible and
// simply should not be there: a Deep Match of a plucked note with no unison at
// all came back at 9.5 cents of detune, so the match was audibly wider than
// the sample it was fitted to.
//
// There is already a prior nudging detune toward zero, and it is not enough:
// a prior is a thumb on the scale during the search, competing with every
// other pull on the same vector. This asks the question directly and only
// once, after the search has finished: put the setting back to nothing and
// render again - is the fit actually worse? If it is not, the search never had
// a reason for that value; it drifted there and nothing pushed it back.
//
// Why this shape rather than more work on the distance function: every change
// to the objective has failed here - six attempts at the chorus, the per-band
// feature, harmonicity, the dead-dimension freeze - while every change AROUND
// it has worked, including tidyInactiveParameters directly above. This is the
// second kind.
struct Simplification
{
    const char* name;
    float simplerNormalised;
};

// Ordered, because the later entries only make sense once the earlier ones
// have been tried: unison voices are worth keeping for the beating they
// create, so whether they earn their place can only be asked after the detune
// that does the beating has been taken away.
const Simplification simplifications[] = {
    {"unisonDetuneCents", 0.0f}, // the 9.5 cents on a sound with none
    {"unisonVoices", 0.0f},      // and the voices left doing nothing without it
};

// How much worse the fit may get before a setting counts as having earned its
// keep, as a fraction of the distance the search achieved. Kept relative
// because the achieved distance is what the tolerance has to mean something
// against - the same absolute slack is generous on a close match and stingy on
// a poor one.
//
// Measured against the whole ceiling rather than the running value, so that
// accepting one simplification cannot buy room for the next: however many are
// taken, the fit at the end is within this of where the search left it.
//
// 15% is not a guess. The same search was run twice over, once against a pluck
// with no unison at all and once against the same pluck at five voices and 20
// cents, and what taking the detune away cost was measured on each:
//
//   target has 20 cents  ->  +128%, +79%, +129%, +35%
//   target has none      ->  +6.1%, +6.5%
//
// A five-fold gap between the cheapest real case and the dearest invented one,
// so the threshold sits in the middle of a gap rather than on top of a
// measurement. The first attempt used 1%, which rejected every case including
// the ones it was built for - the invented detune is not drifting, the
// objective actively prefers it, which is worth knowing and is recorded in
// Docs/SoundMatching.md.
constexpr float simplificationTolerance = 0.15f;

template <typename Evaluate>
int simplifyUnearnedParameters(std::vector<float>& vec, Evaluate&& evaluate)
{
    // Scored without the priors. The priors already lean toward zero, and
    // scoring with them would let a setting be dropped because the prior
    // disliked it rather than because the sound did not need it - which is
    // assuming the answer.
    const float achieved = evaluate(vec, 0, false);
    const float ceiling = achieved * (1.0f + simplificationTolerance);
    int simplified = 0;

    for (const auto& simplification : simplifications)
    {
        int index = indexOfParameter(simplification.name);
        if (index < 0)
            continue;

        const float chosen = vec[(size_t) index];
        if (std::abs(chosen - simplification.simplerNormalised) < 0.01f)
            continue; // the search did not reach for it in the first place

        vec[(size_t) index] = simplification.simplerNormalised;

        if (evaluate(vec, 0, false) <= ceiling)
            ++simplified;
        else
            vec[(size_t) index] = chosen; // it earns its place; leave it alone
    }

    return simplified;
}
AudioFeatures extractFeatures(const float* mono, int numSamples, double sampleRate)
{
    std::vector<float> normalised(mono, mono + numSamples);
    normaliseLoudness(normalised);

    AudioFeatures f;
    f.numMelBands = numMelBands;
    f.logMelCoarse = computeLogMel(normalised.data(), numSamples, sampleRate, 2048, f.coarseFrames);
    f.logMelFine = computeLogMel(normalised.data(), numSamples, sampleRate, 512, f.fineFrames);
    f.envelopeDb = computeEnvelopeDb(normalised.data(), numSamples, envelopePoints);
    f.modulationSpectrum = computeModulationSpectrum(normalised.data(), numSamples, sampleRate);
    return f;
}
DistanceBreakdown featureDistanceParts(const AudioFeatures& a, const AudioFeatures& b)
{
    // Spectral terms dominate (timbre), with envelope shape and modulation
    // character weighted in. Scales chosen so each term lands in a broadly
    // comparable range for typical signals.
    DistanceBreakdown parts;
    parts.spectralCoarse = 1.0f * meanAbsDiff(a.logMelCoarse, b.logMelCoarse);
    parts.spectralFine = 0.6f * meanAbsDiff(a.logMelFine, b.logMelFine);
    parts.envelope = 0.35f * meanAbsDiff(a.envelopeDb, b.envelopeDb);
    parts.modulation = 12.0f * meanAbsDiff(a.modulationSpectrum, b.modulationSpectrum);

    parts.total = parts.spectralCoarse + parts.spectralFine + parts.envelope + parts.modulation;
    return parts;
}

float featureDistance(const AudioFeatures& a, const AudioFeatures& b)
{
    return featureDistanceParts(a, b).total;
}



float scorePreset(const WavetableSet& tableSet, const float* referenceMono, int referenceNumSamples,
                  double sampleRate, int midiNoteNumber, const PresetValues& values, float holdRatio)
{
    auto referenceFeatures = extractFeatures(referenceMono, referenceNumSamples, sampleRate);
    double durationSeconds = referenceNumSamples / sampleRate;

    OfflineRenderer renderer(tableSet, sampleRate);
    juce::AudioBuffer<float> rendered;
    renderer.render(values, midiNoteNumber, durationSeconds, rendered, holdRatio);

    if (rendered.getMagnitude(0, 0, rendered.getNumSamples()) < 1.0e-5f)
        return 1.0e9f;

    auto features = extractFeatures(rendered.getReadPointer(0), rendered.getNumSamples(), sampleRate);
    return featureDistance(referenceFeatures, features);
}

std::vector<PresetAudition> auditionFactoryPresets(const WavetableSet& tableSet, const float* referenceMono,
                                                   int referenceNumSamples, double sampleRate,
                                                   int midiNoteNumber, const PresetValues* optionalEstimate,
                                                   float holdRatio, int numThreads)
{
    auto referenceFeatures = extractFeatures(referenceMono, referenceNumSamples, sampleRate);
    double durationSeconds = referenceNumSamples / sampleRate;

    const auto& bank = FactoryPresets::all();

    std::vector<PresetAudition> results;
    std::vector<const PresetValues*> candidates;

    if (optionalEstimate != nullptr)
    {
        results.push_back({juce::String("Estimate"), -1, 0.0f});
        candidates.push_back(optionalEstimate);
    }

    for (int i = 0; i < (int) bank.size(); ++i)
    {
        results.push_back({bank[(size_t) i].name, i, 0.0f});
        candidates.push_back(&bank[(size_t) i].values);
    }

    int threadCount = numThreads > 0 ? numThreads : (int) std::thread::hardware_concurrency() - 1;
    threadCount = juce::jlimit(1, 16, threadCount);

    std::atomic<size_t> nextIndex{0};

    auto worker = [&]()
    {
        OfflineRenderer renderer(tableSet, sampleRate);
        juce::AudioBuffer<float> rendered;

        for (;;)
        {
            size_t i = nextIndex.fetch_add(1);
            if (i >= candidates.size())
                break;

            renderer.render(*candidates[i], midiNoteNumber, durationSeconds, rendered, holdRatio);

            // A patch that is silent at this pitch is no match at all, however
            // flattering the normalised comparison would otherwise be.
            if (rendered.getMagnitude(0, 0, rendered.getNumSamples()) < 1.0e-5f)
            {
                results[i].distance = 1.0e9f;
                continue;
            }

            auto features = extractFeatures(rendered.getReadPointer(0), rendered.getNumSamples(), sampleRate);
            results[i].distance = featureDistance(referenceFeatures, features);
        }
    };

    std::vector<std::thread> threads;
    for (int t = 1; t < threadCount; ++t)
        threads.emplace_back(worker);
    worker();
    for (auto& t : threads)
        t.join();

    std::sort(results.begin(), results.end(),
              [](const PresetAudition& a, const PresetAudition& b) { return a.distance < b.distance; });

    return results;
}

OptimizerResult optimize(const WavetableSet& tableSet, const std::vector<ReferenceNote>& notes,
                         double sampleRate, const PresetValues& seed,
                         const OptimizerSettings& settings, std::function<void(int, int, float)> progress)
{
    OptimizerResult result;
    result.best = seed;

    if (notes.empty())
        return result;

    // What unsearched fields come from, and what candidates are built on top of.
    // The audition below replaces this with whichever shipped patch turns out to
    // be closest, so settings a single-note comparison cannot see - unison
    // width, glide, the arpeggiator - at least come from a sound that hangs
    // together rather than from a bare default.
    PresetValues base = seed;

    // Measured once per note and kept, since every candidate is compared
    // against all of them.
    std::vector<AudioFeatures> referenceFeatures;
    std::vector<double> noteDurations;
    referenceFeatures.reserve(notes.size());
    noteDurations.reserve(notes.size());

    for (const auto& note : notes)
    {
        referenceFeatures.push_back(extractFeatures(note.mono, note.numSamples, sampleRate));
        noteDurations.push_back(note.numSamples / sampleRate);
    }

    int numThreads = settings.numThreads > 0 ? settings.numThreads
                                             : juce::jmax(1, (int) std::thread::hardware_concurrency() - 1);
    numThreads = juce::jlimit(1, 16, numThreads);

    std::vector<std::unique_ptr<OfflineRenderer>> renderers;
    for (int i = 0; i < numThreads; ++i)
        renderers.push_back(std::make_unique<OfflineRenderer>(tableSet, sampleRate));

    std::atomic<int> evaluationCount{0};

    // withPriors: the nudges toward "off" and away from the ends of a range.
    // They exist to stop the SEARCH cheating, so they are switched off when what
    // is being scored is a preset a person designed - an oscillator deliberately
    // at full is not the same thing as a search pinning one there.
    auto evaluate = [&](const std::vector<float>& vec, int threadIndex, bool withPriors) -> float
    {
        auto candidate = decodeVector(vec, base);
        juce::AudioBuffer<float> rendered;

        // The mean across the notes rather than the sum, so the numbers mean
        // the same thing whether one note is being fitted or four.
        double distanceTotal = 0.0;

        for (size_t n = 0; n < notes.size(); ++n)
        {
            renderers[(size_t) threadIndex]->render(candidate, notes[n].midiNoteNumber, noteDurations[n],
                                                    rendered, notes[n].holdRatio);
            ++evaluationCount;

            // A candidate that produces (near) silence is useless as a match;
            // reject it outright rather than letting loudness normalisation
            // amplify numerical noise into a deceptively low distance. Silent at
            // any one of the pitches is enough to disqualify it - a patch that
            // only sounds in part of the range is not the patch we are after.
            if (rendered.getMagnitude(0, 0, rendered.getNumSamples()) < 1.0e-5f)
                return 1.0e9f;

            auto features = extractFeatures(rendered.getReadPointer(0), rendered.getNumSamples(), sampleRate);
            distanceTotal += featureDistance(referenceFeatures[n], features);
        }

        float distance = (float) (distanceTotal / (double) notes.size());

        // Prefer the simpler patch when two sound equally alike: an optional
        // contribution has to actually improve the fit to justify itself.
        float neutralPenalty = 0.0f;
        for (int p = 0; p < numParams; ++p)
            if (paramSpecs[p].preferNeutral)
                neutralPenalty += std::abs(vec[(size_t) p] - paramSpecs[p].neutralNormalised)
                                  / juce::jmax(0.5f, 1.0f - paramSpecs[p].neutralNormalised);

        // And prefer one that is not jammed against the ends of its controls.
        // A setting at its absolute limit is rarely what someone dialled in; far
        // more often the search wanted to go further and could not, or is using
        // one control to cancel another - a filter thrown wide open and the
        // treble pulled to its floor to compensate. The last stretch of a range
        // barely sounds different from just inside it, so this costs almost
        // nothing when the extreme is genuinely right.
        float railPenalty = 0.0f;
        for (int p = 0; p < numParams; ++p)
        {
            if (paramSpecs[p].rounded)
                continue; // a discrete choice, not an extreme

            float v = vec[(size_t) p];
            float overshoot = juce::jmax(0.0f, v - (1.0f - railMargin), railMargin - v);
            railPenalty += overshoot / railMargin;
        }

        if (! withPriors)
            return distance;

        return distance + neutralPreferenceWeight * neutralPenalty + railPreferenceWeight * railPenalty;
    };

    // Evaluates a batch of vectors across the worker threads.
    auto evaluateBatch = [&](const std::vector<std::vector<float>>& vectors, std::vector<float>& outScores,
                              bool withPriors = true)
    {
        outScores.assign(vectors.size(), 0.0f);
        std::atomic<size_t> nextIndex{0};

        auto worker = [&](int threadIndex)
        {
            for (;;)
            {
                size_t i = nextIndex.fetch_add(1);
                if (i >= vectors.size())
                    break;
                outScores[i] = evaluate(vectors[i], threadIndex, withPriors);
            }
        };

        std::vector<std::thread> threads;
        for (int t = 1; t < numThreads; ++t)
            threads.emplace_back(worker, t);
        worker(0);
        for (auto& t : threads)
            t.join();
    };

    std::mt19937 rng((unsigned) settings.randomSeed);
    std::uniform_real_distribution<float> uniform01(0.0f, 1.0f);

    int populationSize = juce::jmax(8, settings.populationSize);

    // Classic differential evolution (DE/rand/1/bin).
    constexpr float differentialWeight = 0.6f;
    constexpr float crossoverRate = 0.9f;

    int generationsDone = 0;

    // ---- Audition the factory bank before searching -------------------------
    // Every shipped preset is played at the reference's pitch and measured
    // exactly the way a search candidate is measured. Forty-odd renders cost
    // less than one generation, and they settle a question the search is
    // otherwise very bad at: which region of the parameter space is this sound
    // even in?
    //
    // Differential evolution begins from independently random values, and real
    // patches are nothing like that. A bell is a short attack AND a high cutoff
    // AND no unison AND a particular wavetable position, all at once; drawing
    // each of those on its own lands between every real sound rather than near
    // any of them, and the early generations go on rediscovering combinations
    // that were sitting in the bank all along.
    std::vector<std::vector<float>> auditionVectors;
    std::vector<const PresetValues*> auditionValues;
    std::vector<int> auditionCategory;
    juce::StringArray auditionNames;

    // The estimate taken from the recording competes on the same terms rather
    // than being assumed best: it measured the reference, but only a handful of
    // features of it.
    auditionVectors.push_back(encodePreset(seed));
    auditionValues.push_back(&seed);
    auditionCategory.push_back(-1);
    auditionNames.add(seed.name.isNotEmpty() ? seed.name : juce::String("Estimate"));

    // Skipped entirely when the caller has turned the bank off, which leaves
    // the estimate as the only starting point and the search as it was before.
    if (settings.auditionFactoryBank)
    {
        for (const auto& entry : FactoryPresets::all())
        {
            auditionVectors.push_back(encodePreset(entry.values));
            auditionValues.push_back(&entry.values);
            auditionCategory.push_back((int) entry.category);
            auditionNames.add(entry.name);
        }
    }

    // Ranked on the raw distance, with the priors switched off. Those nudges -
    // toward "off", away from the ends of a range - exist to stop the search
    // cheating, and applying them here judges a designer's deliberate choices
    // as though they were the search's evasions. With them on, the ranking
    // preferred a tamer preset over the one that actually sounded closest.
    std::vector<float> auditionScores;
    evaluateBatch(auditionVectors, auditionScores, false);

    std::vector<int> ranking((size_t) auditionVectors.size());
    for (int i = 0; i < (int) ranking.size(); ++i)
        ranking[(size_t) i] = i;

    std::sort(ranking.begin(), ranking.end(), [&auditionScores](int a, int b)
              { return auditionScores[(size_t) a] < auditionScores[(size_t) b]; });

    // Index 0 is the estimate, and it stays the starting point. Measured
    // against a known target over eight random seeds, starting the search from
    // the closest preset instead was no better than not consulting the bank at
    // all (78.0 against 78.7, with the filter recovered worse) - being nearer
    // at the outset is not the same as being better placed to search. What the
    // bank is genuinely worth is company in the population, below.
    result.startingDistance = auditionScores[0];

    int closest = ranking.front();
    result.closestPresetName = auditionNames[closest];
    result.closestPresetDistance = auditionScores[(size_t) closest];

    // The best of each kind, rather than the best few outright. Taking the top
    // handful tends to return the same sound several times over - five pads,
    // say - which is the opposite of what a population needs: differential
    // evolution draws its steps from the differences between members, so
    // near-duplicates give it nowhere to go. One pad, one pluck, one bass
    // covers far more ground for the same number of slots, and measured better
    // than either the top-eight (71.8 against 72.3) or no bank at all (78.7).
    std::vector<std::vector<float>> stageSeeds;
    const std::vector<std::vector<float>> noSeeds;
    int numStageSeeds = juce::jlimit(1, 8, populationSize / 4);

    std::vector<bool> categoryTaken((size_t) FactoryPresets::Category::numCategories, false);
    for (int i = 0; i < (int) ranking.size() && (int) stageSeeds.size() < numStageSeeds; ++i)
    {
        int index = ranking[(size_t) i];
        int category = auditionCategory[(size_t) index];

        // -1 is the estimate, which is already the starting point.
        if (category < 0 || categoryTaken[(size_t) category])
            continue;

        categoryTaken[(size_t) category] = true;
        stageSeeds.push_back(auditionVectors[(size_t) index]);
    }

    // One run of the search over whichever parameters are unlocked. The rest
    // keep the values they arrive with, so a stage can settle part of the patch
    // without the rest shifting underneath it.
    auto runStage = [&](std::vector<float> start, const std::vector<bool>& active, int generations,
                        const std::vector<std::vector<float>>& stageSeeds) -> std::vector<float>
    {
        std::vector<int> activeIndices;
        for (int p = 0; p < numParams; ++p)
            if (active[(size_t) p])
                activeIndices.push_back(p);

        if (activeIndices.empty() || generations <= 0)
            return start;

        std::vector<std::vector<float>> population((size_t) populationSize);

        // Seeded first with what came in, so the stage can only improve on it.
        population[0] = start;

        // Then the presets that auditioned closest, each contributing only the
        // part of itself this stage is allowed to move - a known-good filter
        // and envelope for the voice stage, a known-good reverb and chorus for
        // the effects stage.
        int seeded = 1;
        for (const auto& stageSeed : stageSeeds)
        {
            if (seeded >= populationSize)
                break;

            population[(size_t) seeded] = start;
            for (int p : activeIndices)
                population[(size_t) seeded][(size_t) p] = stageSeed[(size_t) p];
            ++seeded;
        }

        // The rest spread at random. A bank of forty is a set of landmarks, not
        // a map, and a population seeded entirely from it would only ever refine
        // what it was handed.
        for (int i = seeded; i < populationSize; ++i)
        {
            population[(size_t) i] = start;
            for (int p : activeIndices)
                population[(size_t) i][(size_t) p] = uniform01(rng);
        }

        std::vector<float> scores;
        evaluateBatch(population, scores);

        int bestIndex = 0;
        for (int i = 1; i < populationSize; ++i)
            if (scores[(size_t) i] < scores[(size_t) bestIndex])
                bestIndex = i;

        std::uniform_int_distribution<int> activePicker(0, (int) activeIndices.size() - 1);

        for (int generation = 0; generation < generations; ++generation)
        {
            if (settings.cancelFlag != nullptr && settings.cancelFlag->load())
                break;

            std::vector<std::vector<float>> trials((size_t) populationSize);

            for (int i = 0; i < populationSize; ++i)
            {
                int a, b, c;
                do { a = (int) (uniform01(rng) * populationSize) % populationSize; } while (a == i);
                do { b = (int) (uniform01(rng) * populationSize) % populationSize; } while (b == i || b == a);
                do { c = (int) (uniform01(rng) * populationSize) % populationSize; } while (c == i || c == a || c == b);

                std::vector<float> trial = population[(size_t) i];
                int forced = activeIndices[(size_t) activePicker(rng)];

                for (int p : activeIndices)
                {
                    if (p == forced || uniform01(rng) < crossoverRate)
                    {
                        float mutated = population[(size_t) a][(size_t) p]
                                        + differentialWeight
                                              * (population[(size_t) b][(size_t) p]
                                                 - population[(size_t) c][(size_t) p]);
                        trial[(size_t) p] = juce::jlimit(0.0f, 1.0f, mutated);
                    }
                }

                trials[(size_t) i] = std::move(trial);
            }

            std::vector<float> trialScores;
            evaluateBatch(trials, trialScores);

            for (int i = 0; i < populationSize; ++i)
            {
                if (trialScores[(size_t) i] < scores[(size_t) i])
                {
                    population[(size_t) i] = trials[(size_t) i];
                    scores[(size_t) i] = trialScores[(size_t) i];
                    if (scores[(size_t) i] < scores[(size_t) bestIndex])
                        bestIndex = i;
                }
            }

            ++generationsDone;
            if (progress)
                progress(generationsDone, settings.generations, scores[(size_t) bestIndex]);
        }

        return population[(size_t) bestIndex];
    };

    // The voice first, with the master chain held dry and flat.
    //
    // Searched all at once, the effects are an escape hatch: the filter can be
    // thrown wide open and the treble pulled down to cancel it, and the result
    // scores well on the one note it was fitted to while the voice underneath
    // is wrong. Settling the voice against a dry chain first means the filter
    // has to be right because nothing else can stand in for it - and the
    // effects are then fitted to whatever the recording has left over, which is
    // the order they sit in anyway.
    // The static voice first - what the sound is before anything moves it -
    // then the movement, then the master chain, each fitted to what the one
    // before it leaves over. That is the order the signal travels, and also the
    // order these things stand in for one another: a sweep can hide a wrong
    // static cutoff exactly as an equaliser can.
    std::vector<bool> staticVoice((size_t) numParams), everything((size_t) numParams, true);
    for (int p = 0; p < numParams; ++p)
        staticVoice[(size_t) p] = stageForName(paramSpecs[p].name) == Stage::voice;

    // The estimate remains the starting point; the bank contributes company in
    // the population rather than a place to begin. See the audition above for
    // the measurement behind that.
    auto startVector = auditionVectors[0];
    for (int p = 0; p < numParams; ++p)
        if (stageForName(paramSpecs[p].name) != Stage::voice && paramSpecs[p].preferNeutral)
            startVector[(size_t) p] = paramSpecs[p].neutralNormalised;

    std::vector<bool> effectsOnly((size_t) numParams);
    for (int p = 0; p < numParams; ++p)
        effectsOnly[(size_t) p] = stageForName(paramSpecs[p].name) == Stage::effect;

    // Three stages: the voice against a dry chain, then the effects fitted to
    // whatever that leaves over with the voice held still, then a short pass
    // over everything to settle the two together. The middle stage is the part
    // that keeps the voice honest - given the chance to move at the same time,
    // it drifts back to cancelling whatever the effects are doing.
    std::vector<bool> modulationOnly((size_t) numParams);
    for (int p = 0; p < numParams; ++p)
        modulationOnly[(size_t) p] = stageForName(paramSpecs[p].name) == Stage::modulation;

    // The voice gets the largest share because everything after it is fitted to
    // what it leaves over, and the joint pass a share of its own to settle the
    // stages against each other at the end.
    int voiceGenerations = juce::jmax(1, settings.generations * 3 / 10);
    int modGenerations = juce::jmax(1, settings.generations / 5);
    int effectGenerations = juce::jmax(1, settings.generations / 5);
    int jointGenerations = juce::jmax(1, settings.generations - voiceGenerations - modGenerations
                                              - effectGenerations);

    auto afterVoice = runStage(startVector, staticVoice, voiceGenerations, stageSeeds);
    auto afterModulation = runStage(afterVoice, modulationOnly, modGenerations, stageSeeds);
    auto afterEffects = runStage(afterModulation, effectsOnly, effectGenerations, stageSeeds);
    auto finalVector = runStage(afterEffects, everything, jointGenerations, noSeeds);

    // Before the patch is decoded, and before it is scored: this one CAN change
    // the sound, so it has to happen while there is still a vector to score,
    // and the distance reported below has to be the distance of what actually
    // comes back. The tidy pass afterwards is the opposite case and runs the
    // opposite way round - see each for why.
    result.unearnedSimplified = simplifyUnearnedParameters(finalVector, evaluate);

    result.best = decodeVector(finalVector, base);
    result.best.name = seed.name;

    // Scored again rather than carried out of the stage, so what is reported is
    // the distance of the patch actually being returned.
    result.bestDistance = evaluate(finalVector, 0, true);

    // Done after the scoring, deliberately: this only moves settings the search
    // could not hear, so the distance it reports is unaffected either way, and
    // scoring first means a mistake here would show up as a changed number
    // rather than hiding behind one.
    result.inactiveTidied = tidyInactiveParameters(result.best);

    result.evaluations = evaluationCount.load();
    return result;
}

OptimizerResult optimize(const WavetableSet& tableSet, const float* referenceMono, int referenceNumSamples,
                         double sampleRate, int midiNoteNumber, const PresetValues& seed,
                         const OptimizerSettings& settings, std::function<void(int, int, float)> progress,
                         float holdRatio)
{
    ReferenceNote note;
    note.mono = referenceMono;
    note.numSamples = referenceNumSamples;
    note.midiNoteNumber = midiNoteNumber;
    note.holdRatio = holdRatio;

    return optimize(tableSet, {note}, sampleRate, seed, settings, std::move(progress));
}
} // namespace SoundMatch
} // namespace wavelathe
