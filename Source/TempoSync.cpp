// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "TempoSync.h"
#include <cmath>

namespace wavelathe
{
namespace tempo
{
namespace
{
const Division divisionTable[] = {
    {4.0, "1/1"},      {3.0, "1/2."},     {2.0, "1/2"},   {4.0 / 3.0, "1/2T"}, {1.5, "1/4."},
    {1.0, "1/4"},      {2.0 / 3.0, "1/4T"}, {0.75, "1/8."}, {0.5, "1/8"},        {1.0 / 3.0, "1/8T"},
    {0.375, "1/16."},  {0.25, "1/16"},    {1.0 / 6.0, "1/16T"}, {0.125, "1/32"}};

constexpr int numDivisions = (int) (sizeof(divisionTable) / sizeof(divisionTable[0]));
} // namespace

const Division* getDivisions() { return divisionTable; }
int getNumDivisions() { return numDivisions; }

double divisionToHz(int index, double bpm)
{
    index = juce::jlimit(0, numDivisions - 1, index);
    double secondsPerBeat = 60.0 / juce::jmax(1.0, bpm);
    return 1.0 / juce::jmax(1.0e-6, secondsPerBeat * divisionTable[index].beats);
}

double divisionToMs(int index, double bpm)
{
    index = juce::jlimit(0, numDivisions - 1, index);
    double secondsPerBeat = 60.0 / juce::jmax(1.0, bpm);
    return secondsPerBeat * divisionTable[index].beats * 1000.0;
}

int nearestDivisionForHz(double hz, double bpm)
{
    // Compared in log space so the choice feels even across the range rather
    // than biased toward the fast end.
    int best = 0;
    double bestDistance = 1.0e30;
    double target = std::log(juce::jmax(1.0e-6, hz));

    for (int i = 0; i < numDivisions; ++i)
    {
        double distance = std::abs(std::log(divisionToHz(i, bpm)) - target);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

int nearestDivisionForMs(double milliseconds, double bpm)
{
    return nearestDivisionForHz(1000.0 / juce::jmax(1.0, milliseconds), bpm);
}

juce::String divisionName(int index)
{
    return divisionTable[juce::jlimit(0, numDivisions - 1, index)].name;
}
} // namespace tempo

void TempoClock::prepare(double sampleRateToUse)
{
    sampleRate = sampleRateToUse;
    reset();
}

void TempoClock::reset()
{
    samplesElapsed = 0;
    forgetClocks();
}

void TempoClock::forgetClocks()
{
    lastClockSample = 0;
    clockCount = 0;
    clockNext = 0;
    receivingClock = false;
    smoothedBpm = 0.0;
    divergentPulses = 0;
    nextPulseIndex = 0;
    multiStepPulses = 0;
}

void TempoClock::processMidi(const juce::MidiBuffer& midi, int numSamples)
{
    for (const auto metadata : midi)
        if (metadata.getMessage().isMidiClock())
            recordClock(samplesElapsed + metadata.samplePosition);

    samplesElapsed += numSamples;

    // No clock for half a second means whatever was driving it has stopped.
    if (clockCount > 0 && samplesElapsed - lastClockSample > (int64_t) (sampleRate * 0.5))
        forgetClocks();
}

void TempoClock::recordClock(int64_t at)
{
    // A gap this long is not jitter. It is the clock having stopped and come
    // back, possibly at another tempo, and nothing before it is worth fitting.
    if (clockCount > 0 && at - lastClockSample > (int64_t) (sampleRate * 0.5))
        forgetClocks();

    // Which pulse this is, rather than merely the next one to arrive. With a
    // tempo already in hand the spacing says how many should have come since
    // the last, so one lost on the way leaves a gap in the numbering instead of
    // quietly stretching the interval the line is fitted to.
    //
    // Capped at eight: a longer gap than that is not a dropped pulse, and the
    // half-second rule above has already dealt with anything really long.
    int64_t step = 1;

    if (clockCount > 0 && smoothedBpm > 0.0)
    {
        const double expected = 60.0 * sampleRate / (smoothedBpm * 24.0);
        step = (int64_t) std::llround((double) (at - lastClockSample) / juce::jmax(1.0, expected));
        step = juce::jlimit<int64_t>(1, 8, step);
    }

    if (step > 1)
    {
        // A link drops a pulse now and then. It does not drop every other one.
        // Several in a row arriving at a multiple of the spacing is the sender
        // having slowed down, not the cable misbehaving - and reading that as
        // dropped pulses would leave the fit agreeing with itself at the old
        // tempo and never noticing. Halve the tempo on a sender and this is
        // exactly what happens, so it is worth the three lines.
        if (++multiStepPulses >= 3)
        {
            forgetClocks();
            step = 1;
        }
    }
    else
    {
        multiStepPulses = 0;
    }

    if (clockCount == 0)
        nextPulseIndex = 0;
    else
        nextPulseIndex += step;

    clockAt[(size_t) clockNext] = at;
    clockIndex[(size_t) clockNext] = nextPulseIndex;
    clockNext = (clockNext + 1) % clockCapacity;

    if (clockCount < clockCapacity)
        ++clockCount;

    lastClockSample = at;
    refit();
}

namespace
{
// Enough to be worth answering with. Eight pulses is a third of a beat -
// rough, but better than the local setting the moment a clock appears, and it
// tightens on its own as the window fills.
constexpr int minimumClocks = 8;
} // namespace

int TempoClock::countWithin(double seconds) const
{
    // Walk back from the newest pulse for as long as they are inside the
    // window, so a fit always covers the same stretch of time whatever the
    // tempo. The count falls out of that rather than being fixed.
    const int64_t span = (int64_t) (sampleRate * seconds);
    int within = 0;

    for (int i = 0; i < clockCount; ++i)
    {
        const int index = ((clockNext - 1 - i) % clockCapacity + clockCapacity) % clockCapacity;

        if (lastClockSample - clockAt[(size_t) index] > span)
            break;

        ++within;
    }

    return within;
}

std::optional<double> TempoClock::fitBpm(int usable) const
{
    if (usable < minimumClocks || usable > clockCount)
        return {};

    // A straight line through every position in the window, rather than the
    // distance between the first and the last. Both average the noise down, but
    // a line uses every point instead of two, so one badly placed pulse cannot
    // drag the answer along with it.
    const int oldest = ((clockNext - usable) % clockCapacity + clockCapacity) % clockCapacity;

    double meanIndex = 0.0;
    double meanTime = 0.0;

    for (int i = 0; i < usable; ++i)
    {
        const size_t slot = (size_t) ((oldest + i) % clockCapacity);
        meanIndex += (double) clockIndex[slot];
        meanTime += (double) clockAt[slot];
    }

    meanIndex /= (double) usable;
    meanTime /= (double) usable;

    double covariance = 0.0;
    double variance = 0.0;

    for (int i = 0; i < usable; ++i)
    {
        const size_t slot = (size_t) ((oldest + i) % clockCapacity);
        const double offset = (double) clockIndex[slot] - meanIndex;

        covariance += offset * ((double) clockAt[slot] - meanTime);
        variance += offset * offset;
    }

    if (variance <= 0.0 || covariance <= 0.0)
        return {};

    // The slope is samples per pulse, and 24 pulses make a quarter note.
    const double quarterNoteSamples = (covariance / variance) * 24.0;
    const double bpm = 60.0 * sampleRate / juce::jmax(1.0, quarterNoteSamples);

    if (bpm < 20.0 || bpm > 300.0)
        return {};

    return bpm;
}

void TempoClock::refit()
{
    if (clockCount < minimumClocks)
        return;

    // A clock slower than the window is long still gets an answer, from
    // whatever it has managed to send.
    const int longCount = juce::jmax(countWithin(clockWindowSeconds),
                                      juce::jmin(clockCount, minimumClocks));

    const auto longBpm = fitBpm(longCount);

    if (! longBpm)
        return;

    // The short window is watched, not used. Noise cannot hold a steady offset
    // from the long window - it disagrees and then agrees again - while a tempo
    // change disagrees immediately and keeps disagreeing. So several pulses in
    // a row outside the threshold means the sender has moved, and two seconds
    // of averaging built at the old tempo is worse than nothing.
    const int shortCount = countWithin(clockShortWindowSeconds);

    if (shortCount >= minimumClocks && shortCount < longCount)
    {
        if (const auto shortBpm = fitBpm(shortCount))
        {
            if (std::abs(*shortBpm - *longBpm) / *longBpm > clockChangeThreshold)
            {
                if (++divergentPulses >= clockChangeConfirmations)
                {
                    // Down to the freshest handful, not down to the short
                    // window: the short window is half a second long and still
                    // holds pulses from before the change, so rebuilding from
                    // it lands halfway and then crawls the rest. Only the last
                    // few pulses are certain to be at the new tempo.
                    //
                    // Thrown away by forgetting they are there. The newest are
                    // the ones kept, so nothing has to be moved, and the count
                    // grows again from here as new pulses arrive.
                    clockCount = juce::jmin(clockCount, minimumClocks);
                    divergentPulses = 0;

                    // Left for the next pulse to fit, one pulse being some
                    // twenty milliseconds and the window now holding nothing
                    // but the new tempo.
                    return;
                }
            }
            else
            {
                divergentPulses = 0;
            }
        }
    }

    // A light hand on top. The line has already taken the noise out; this is
    // only so the last decimal of the readout does not twitch.
    smoothedBpm = (smoothedBpm <= 0.0) ? *longBpm : smoothedBpm + 0.25 * (*longBpm - smoothedBpm);
    receivingClock = true;
}

std::optional<double> TempoClock::getExternalClockBpm(const SynthParameters& params) const
{
    if (params.midiClockSync.load() > 0.5f && receivingClock && smoothedBpm > 0.0)
        return smoothedBpm;

    return {};
}

double TempoClock::getBpm(const SynthParameters& params) const
{
    if (auto external = getExternalClockBpm(params))
        return *external;

    return juce::jlimit(20.0, 300.0, (double) params.bpm.load());
}
} // namespace wavelathe

namespace wavelathe
{
namespace tempo
{
namespace
{
// Whole multiples only. A rate that is not a whole multiple of the sequencer's
// puts the two on grids that only agree occasionally, which is exactly the
// out-of-step feel this is meant to remove.
const RateLink rateLinkTable[] = {
    {0.25, "Seq / 4"}, {0.5, "Seq / 2"}, {1.0, "Seq x 1"},
    {2.0, "Seq x 2"},  {4.0, "Seq x 4"}, {0.0, "Own rate"}};

constexpr int numRateLinks = (int) (sizeof(rateLinkTable) / sizeof(rateLinkTable[0]));
} // namespace

const RateLink* getRateLinks() { return rateLinkTable; }
int getNumRateLinks() { return numRateLinks; }
int freeRateLink() { return numRateLinks - 1; }

bool isFreeRateLink(int index)
{
    return juce::jlimit(0, numRateLinks - 1, index) == freeRateLink();
}

juce::String rateLinkName(int index)
{
    return rateLinkTable[juce::jlimit(0, numRateLinks - 1, index)].name;
}

double linkedBeats(double sequencerBeats, int linkIndex)
{
    double multiplier = rateLinkTable[juce::jlimit(0, numRateLinks - 1, linkIndex)].multiplier;
    if (multiplier <= 0.0)
        return sequencerBeats;

    // Faster means shorter steps, so the multiplier divides the step length.
    return sequencerBeats / multiplier;
}

double divisionBeats(int index)
{
    return getDivisions()[juce::jlimit(0, getNumDivisions() - 1, index)].beats;
}

int nearestDivisionForBeats(double beats)
{
    int best = 0;
    double bestDistance = 1.0e30;
    double target = std::log(juce::jmax(1.0e-9, beats));

    for (int i = 0; i < getNumDivisions(); ++i)
    {
        double distance = std::abs(std::log(divisionBeats(i)) - target);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

void MusicalClock::prepare(double sampleRateToUse)
{
    sampleRate = juce::jmax(1.0, sampleRateToUse);
    reset();
}

void MusicalClock::reset()
{
    positionBeats = 0.0;
    pendingAdvance = 0.0;
}

void MusicalClock::beginBlock(double bpm, int numSamples)
{
    // The previous block's worth of time is added here rather than at the end
    // of it, so everything reading the clock during a block sees one settled
    // position for the whole of it.
    positionBeats += pendingAdvance;
    beatsPerSample = juce::jmax(1.0, bpm) / (60.0 * sampleRate);
    pendingAdvance = beatsPerSample * (double) juce::jmax(0, numSamples);
}
} // namespace tempo
} // namespace wavelathe
