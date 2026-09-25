// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Drums/DrumKit.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace drums;

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

// A hit, and where in the render it lands. Measured from the start of the whole
// render rather than from the start of a block, so the same list can be played
// through any block size - which is the point of the identity test below.
struct Hit
{
    int voice = 0;
    float velocity = 1.0f;
    bool accent = false;
    int sample = 0;
};

// Everything on the stack here is a pointer to something on the heap, and that
// is not fussiness. A Kit is several kilobytes and MSVC reserves a function's
// WHOLE frame on entry, summing sibling scopes - which is exactly how this
// project's sequencer tests overflowed the stack twice before the sequencer's
// lanes were moved to the heap.
std::unique_ptr<Kit> makeKit(double rate = sampleRate)
{
    auto kit = std::make_unique<Kit>();
    kit->prepare(rate);
    return kit;
}

std::unique_ptr<KitParameters> makeParams(float level = 1.0f, float tune = 0.5f,
                                          float decay = 0.5f, float pan = 0.5f,
                                          float master = 1.0f)
{
    auto params = std::make_unique<KitParameters>();
    params->masterLevel.store(master);

    for (auto& voice : params->voices)
    {
        voice.level.store(level);
        voice.tune.store(tune);
        voice.decay.store(decay);
        voice.pan.store(pan);
    }

    return params;
}

juce::AudioBuffer<float> renderHits(Kit& kit, const KitParameters& params,
                                    const std::vector<Hit>& hits, int numSamples, int blockSize)
{
    juce::AudioBuffer<float> buffer(2, numSamples);
    buffer.clear();

    int position = 0;

    while (position < numSamples)
    {
        const auto block = juce::jmin(blockSize, numSamples - position);

        for (const auto& hit : hits)
            if (hit.sample >= position && hit.sample < position + block)
                kit.trigger(hit.voice, hit.velocity, hit.accent, hit.sample - position);

        kit.renderAdding(buffer, position, block, params);
        position += block;
    }

    return buffer;
}

juce::AudioBuffer<float> renderOne(int voice, const KitParameters& params, double seconds,
                                   bool accent = false, float velocity = 1.0f,
                                   double rate = sampleRate)
{
    auto kit = makeKit(rate);
    const auto numSamples = (int) (seconds * rate);
    return renderHits(*kit, params, { Hit{ voice, velocity, accent, 0 } }, numSamples, numSamples);
}

double rmsOf(const juce::AudioBuffer<float>& buffer, int from, int to, int channel = 0)
{
    from = juce::jmax(0, from);
    to = juce::jmin(buffer.getNumSamples(), to);

    if (to <= from)
        return 0.0;

    const auto* data = buffer.getReadPointer(channel);
    double sum = 0.0;

    for (int i = from; i < to; ++i)
        sum += (double) data[i] * (double) data[i];

    return std::sqrt(sum / (double) (to - from));
}

// The biggest jump between one sample and the next, over a window.
//
// Which is what a click IS. Loudness will not find one - a click is quiet
// against a drum - and neither will RMS, which averages it away. A step is
// only visible as a step.
float maxStep(const juce::AudioBuffer<float>& buffer, int from, int to, int channel = 0)
{
    from = juce::jmax(1, from);
    to = juce::jmin(buffer.getNumSamples(), to);

    const auto* data = buffer.getReadPointer(channel);
    float biggest = 0.0f;

    for (int i = from; i < to; ++i)
        biggest = juce::jmax(biggest, std::abs(data[i] - data[i - 1]));

    return biggest;
}

float peakOf(const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = juce::jmax(peak, std::abs(buffer.getSample(channel, i)));

    return peak;
}

// The dominant frequency of a window, from how often it crosses zero. Crude,
// and entirely adequate for what it is asked here: every measurement below is
// of a signal with one obvious frequency in it, and the claims are about
// ratios and orders of magnitude rather than about cents.
double dominantHz(const juce::AudioBuffer<float>& buffer, int from, int to, double rate = sampleRate)
{
    from = juce::jmax(1, from);
    to = juce::jmin(buffer.getNumSamples(), to);

    if (to <= from)
        return 0.0;

    const auto* data = buffer.getReadPointer(0);
    int crossings = 0;

    for (int i = from; i < to; ++i)
        if ((data[i - 1] < 0.0f) != (data[i] < 0.0f))
            ++crossings;

    return (double) crossings * rate / (double) (to - from) / 2.0;
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

// Is `sum` exactly `a` plus `b`, sample for sample? Exact rather than close,
// and it is allowed to be: adding into a cleared buffer computes (0 + a) + b,
// and 0 + a is a itself, so the two roads reach the same float.
bool isExactSum(const juce::AudioBuffer<float>& sum,
                const juce::AudioBuffer<float>& a,
                const juce::AudioBuffer<float>& b)
{
    if (sum.getNumSamples() != a.getNumSamples() || sum.getNumSamples() != b.getNumSamples())
        return false;

    for (int channel = 0; channel < sum.getNumChannels(); ++channel)
        for (int i = 0; i < sum.getNumSamples(); ++i)
            if (sum.getSample(channel, i) != a.getSample(channel, i) + b.getSample(channel, i))
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

bool isExactlySilent(const juce::AudioBuffer<float>& buffer, int from, int to)
{
    from = juce::jmax(0, from);
    to = juce::jmin(buffer.getNumSamples(), to);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = from; i < to; ++i)
            if (buffer.getSample(channel, i) != 0.0f)
                return false;

    return true;
}

constexpr int kick = 0;
constexpr int snare = 1;
constexpr int closedHat = 8;
constexpr int openHat = 9;
constexpr int crash = 10;
} // namespace

int main()
{
    std::cout << "Drum voice tests" << std::endl;
    std::cout << std::endl;

    // ---- Nothing, until something is asked for -----------------------------
    //
    // Exactly zero rather than quiet. A kit that idles at -90 dB is a kit that
    // adds sixteen voices of noise floor to every silent bar, and "quiet
    // enough" is how that goes unnoticed until it is sixteen instances deep.
    {
        auto kit = makeKit();
        auto params = makeParams();

        juce::AudioBuffer<float> buffer(2, 4096);
        buffer.clear();
        kit->renderAdding(buffer, *params);

        check(isExactlySilent(buffer, 0, buffer.getNumSamples()),
              "a kit that has not been hit is exactly silent");
        check(kit->getNumActiveVoices() == 0, "and no voice is active");
    }

    // ---- Every ENGINE makes a sound, and none of them makes a wrong one -----
    //
    // The table in DrumVoice.cpp is sixteen rows of constants, and a row with a
    // mistake in it - a decay of zero, a frequency above Nyquist, an engine
    // that never got its case - is silent or infinite rather than wrong in some
    // musical way. This is the check that a new row was actually finished.
    //
    // Sixteen engines on twelve slots, so this walks the ENGINES and puts each
    // one on slot 0 in turn. When a slot was its sound the two loops were the
    // same loop; they are not any more, and looping over slots here would stop
    // testing the last four rows of the table entirely - silently, because
    // twelve passing checks look exactly like sixteen passing checks unless
    // you count them.
    {
        double quietest = 1.0e9, loudest = 0.0;
        int quietestEngine = -1, loudestEngine = -1;

        for (int engine = 0; engine < numEngines; ++engine)
        {
            auto params = makeParams();
            params->voices[0].engine.store(engine);

            auto buffer = renderOne(0, *params, 1.0, true);
            const auto peak = (double) peakOf(buffer);

            check(allFinite(buffer), juce::String(engineName(engine)) + " produces finite samples");
            check(peak > 0.05, juce::String(engineName(engine)) + " produces a sound at all");
            check(peak <= 1.0, juce::String(engineName(engine)) + " stays inside full scale");

            if (peak < quietest) { quietest = peak; quietestEngine = engine; }
            if (peak > loudest)  { loudest = peak;  loudestEngine = engine; }

            std::cout << "  " << juce::String(engineName(engine)).paddedRight(' ', 9)
                      << " peak " << juce::String(peak, 3) << std::endl;
        }

        // A kit whose rows mean different things at the same fader position is
        // a kit you have to re-learn for every pattern. Four to one is loose -
        // a crash SHOULD be bigger than a maraca - but it catches a trim that
        // was never set.
        check(loudest <= quietest * 4.0,
              "no engine is more than four times the peak of the quietest ("
                  + juce::String(engineName(loudestEngine)) + " against "
                  + juce::String(engineName(quietestEngine)) + ")");
        std::cout << std::endl;
    }

    // ---- The choke follows the drum, not the row it is on ------------------
    //
    // A closed hat cuts an open one because of what they ARE, and now that a
    // slot can be any drum, "which rows choke each other" has to be read off
    // the drums on them rather than off the row numbers.
    //
    // Put the pair on two rows that have nothing to do with hats and they must
    // still cut each other; leave the hat rows on something else and those must
    // not. The version that looked up chokeGroup by slot index passes the first
    // half of this and fails the second, which is why both are here.
    {
        constexpr int closedHatEngine = 8;
        constexpr int openHatEngine = 9;
        constexpr int rowA = 2;   // the Rim row
        constexpr int rowB = 3;   // the Clap row

        auto params = makeParams();
        params->voices[rowA].engine.store(openHatEngine);
        params->voices[rowB].engine.store(closedHatEngine);

        const auto ringLength = (int) (sampleRate * 0.4);
        const auto cutAt = (int) (sampleRate * 0.05);

        // The open hat alone, then the same open hat with the closed hat
        // struck over it.
        auto ringing = renderHits(*makeKit(), *params, { Hit{ rowA, 1.0f, false, 0 } },
                                  ringLength, 512);

        auto choked = renderHits(*makeKit(), *params,
                                 { Hit{ rowA, 1.0f, false, 0 }, Hit{ rowB, 1.0f, false, cutAt } },
                                 ringLength, 512);

        const auto from = (int) (sampleRate * 0.25);
        const auto ringingTail = rmsOf(ringing, from, ringLength);
        const auto chokedTail = rmsOf(choked, from, ringLength);

        check(chokedTail < ringingTail * 0.5,
              "a closed hat put on any row still cuts an open hat on any other ("
                  + juce::String(ringingTail, 5) + " ringing against "
                  + juce::String(chokedTail, 5) + " choked)");

        // And the rows those two drums USUALLY live on, now holding something
        // else, must not choke anything.
        //
        // Asked of the voice rather than of the audio. The first version of
        // this measured two kicks and expected them to be louder together than
        // apart, which they are not: 52 Hz struck twice 0.05 s apart is 2.6
        // cycles, so the second arrives most of a half-cycle out of phase with
        // the first and they partly cancel. That is interference, and it looks
        // exactly like a choke from the outside. isVoiceActive cannot be fooled
        // by it - a choked voice is switched off, a cancelled one is not.
        auto moved = makeParams();
        moved->voices[8].engine.store(0);    // a kick where the closed hat was
        moved->voices[9].engine.store(0);    // and where the open hat was

        const auto stillRingingAfterSecondHit = [&](KitParameters& p, int first, int second)
        {
            auto kit = makeKit();
            juce::AudioBuffer<float> buffer(2, ringLength);
            buffer.clear();

            kit->trigger(first, 1.0f, false, 0);
            kit->renderAdding(buffer, 0, cutAt, p);

            kit->trigger(second, 1.0f, false, 0);

            // Past the choke's own 5 ms fade, so a voice on its way out has
            // finished going.
            kit->renderAdding(buffer, cutAt, (int) (sampleRate * 0.02), p);

            return kit->isVoiceActive(first);
        };

        check(stillRingingAfterSecondHit(*moved, 9, 8),
              "and the hat ROWS choke nothing once the hats have been moved off them");

        // The same measurement on the moved pair, so the check above is known
        // to be capable of reporting a choke rather than merely always true.
        check(!stillRingingAfterSecondHit(*params, rowA, rowB),
              "while the hats themselves choke wherever they have been put");
    }

    // ---- A blank slot is silent, and is not a kick -------------------------
    //
    // engineSpec hands back the kick for anything out of range, which is the
    // right answer to "give me a spec" and the wrong sound entirely for a slot
    // nobody has put a drum on. DrumKit has to refuse the hit BEFORE it asks,
    // and that is one `if` standing between an empty twelfth row and a kick
    // drum on it.
    {
        auto params = makeParams();

        check(defaultEngineFor(numVoices - 1) == noEngine,
              "the last slot starts blank, as the eleven-plus-one kit says");

        auto buffer = renderOne(numVoices - 1, *params, 1.0, true);
        check(peakOf(buffer) == 0.0f, "and striking it makes no sound at all");

        // The same slot, given a drum, does sound - so the silence above is the
        // blank and not the slot being broken.
        params->voices[(size_t) (numVoices - 1)].engine.store(0);
        auto withKick = renderOne(numVoices - 1, *params, 1.0, true);
        check(peakOf(withKick) > 0.05f, "and sounds once a drum is put on it");
    }

    // ---- A hit lands on the sample it was asked for ------------------------
    //
    // The reason the kit splits blocks at all. Phase 2a gave every hit a signed
    // nudge measured in ticks so that a beat need not sit exactly on the grid;
    // a renderer that started hits on block boundaries would quantise all of
    // that away, silently, and only at some buffer sizes.
    {
        auto kit = makeKit();
        auto params = makeParams();

        constexpr int offset = 1000;
        auto buffer = renderHits(*kit, *params, { Hit{ kick, 1.0f, false, offset } }, 4096, 4096);

        check(isExactlySilent(buffer, 0, offset), "nothing sounds before the sample a hit was queued for");
        check(rmsOf(buffer, offset, offset + 200) > 0.0, "and the hit starts exactly there");
    }

    // ---- The same hits, chopped up, are the same samples -------------------
    //
    // The property that separates a voice whose envelopes run per sample from
    // one that updates them per block. The second sounds fine at 512 and
    // different at 64, which is a bug nobody finds until a user changes their
    // buffer size and says the groove feels different.
    //
    // Bit-identical, not close. It is allowed to be, because the controls are
    // still throughout: the gain ramp in Voice::renderAdding is a de-zipper for
    // moving dials, and with a still dial its step is exactly zero.
    {
        auto params = makeParams();

        const std::vector<Hit> hits = {
            { kick,      1.0f, true,      0 },
            { closedHat, 0.7f, false,   331 },
            { snare,     0.9f, false,  4097 },
            { openHat,   0.8f, false,  9000 },
            { crash,     1.0f, true,  17321 }
        };

        constexpr int total = 32768;

        auto whole = renderHits(*makeKit(), *params, hits, total, total);
        auto in64 = renderHits(*makeKit(), *params, hits, total, 64);
        auto in512 = renderHits(*makeKit(), *params, hits, total, 512);
        auto odd = renderHits(*makeKit(), *params, hits, total, 173);

        check(identical(whole, in64), "one long render and the same render in 64-sample blocks agree exactly");
        check(identical(whole, in512), "and in 512-sample blocks");
        check(identical(whole, odd), "and in blocks of an awkward size");
        check(allFinite(whole), "and nothing in it is infinite");
    }

    // ---- Voices are independent, and the choke is the exception ------------
    //
    // One measurement making two claims. Two voices that do not choke each
    // other must add EXACTLY: render them apart, render them together, and the
    // samples agree. Two voices that do choke each other must not, because the
    // second one changed the first - and if that sum came out exact, the choke
    // did nothing.
    {
        auto params = makeParams();
        constexpr int total = 44100;

        auto kickOnly = renderHits(*makeKit(), *params, { Hit{ kick, 1.0f, false, 0 } }, total, total);
        auto snareOnly = renderHits(*makeKit(), *params, { Hit{ snare, 1.0f, false, 2000 } }, total, total);
        auto both = renderHits(*makeKit(), *params,
                               { Hit{ kick, 1.0f, false, 0 }, Hit{ snare, 1.0f, false, 2000 } },
                               total, total);

        check(isExactSum(both, kickOnly, snareOnly), "a kick and a snare together are exactly the two apart");

        auto crashOnly = renderHits(*makeKit(), *params, { Hit{ crash, 1.0f, false, 0 } }, total, total);
        auto hatOnly = renderHits(*makeKit(), *params, { Hit{ closedHat, 1.0f, false, 4410 } }, total, total);
        auto crashAndHat = renderHits(*makeKit(), *params,
                                      { Hit{ crash, 1.0f, false, 0 }, Hit{ closedHat, 1.0f, false, 4410 } },
                                      total, total);

        check(isExactSum(crashAndHat, crashOnly, hatOnly),
              "a closed hat does not choke a crash - they are in different groups");

        auto openOnly = renderHits(*makeKit(), *params, { Hit{ openHat, 1.0f, false, 0 } }, total, total);
        auto openThenClosed = renderHits(*makeKit(), *params,
                                         { Hit{ openHat, 1.0f, false, 0 }, Hit{ closedHat, 1.0f, false, 4410 } },
                                         total, total);

        check(!isExactSum(openThenClosed, openOnly, hatOnly),
              "a closed hat DOES change an open one - the choke is not a no-op");

        // And what it changed it into: the open hat is gone, so what is left
        // after the closed hat has died away is the closed hat's own tail and
        // nothing else.
        const auto from = (int) (0.30 * sampleRate);
        const auto to = (int) (0.45 * sampleRate);

        const auto unchoked = rmsOf(openOnly, from, to);
        const auto choked = rmsOf(openThenClosed, from, to);
        const auto hatAlone = rmsOf(hatOnly, from, to);

        check(unchoked > 0.0, "an open hat is still ringing 300 ms in");
        check(choked < unchoked * 0.2, "a choked one is not");
        check(choked <= hatAlone * 1.2, "what is left is the closed hat and nothing else");

        std::cout << "  open hat at 300 ms: " << juce::String(unchoked, 5)
                  << " ringing, " << juce::String(choked, 5) << " choked" << std::endl;
        std::cout << std::endl;
    }

    // ---- The kick is a kick, and not a sine at 52 Hz -----------------------
    //
    // The sweep IS the drum. Without it there is a fundamental in the right
    // place and nothing that sounds like a beater hitting a head, and a test
    // that only measured the settled pitch would pass on a sine blip.
    {
        auto params = makeParams();
        auto buffer = renderOne(kick, *params, 1.0);

        const auto early = dominantHz(buffer, (int) (0.005 * sampleRate), (int) (0.015 * sampleRate));
        const auto late = dominantHz(buffer, (int) (0.150 * sampleRate), (int) (0.650 * sampleRate));

        check(early > late * 1.8, "a kick starts well above where it settles");
        check(late > 40.0 && late < 70.0, "and settles near the 52 Hz its spec asks for");

        std::cout << "  kick sweeps " << juce::String(early, 1) << " Hz -> "
                  << juce::String(late, 1) << " Hz" << std::endl;

        // Two engines doing genuinely different things, measured the same way.
        auto hat = renderOne(closedHat, *params, 0.5);
        const auto hatHz = dominantHz(hat, 0, (int) (0.05 * sampleRate));

        check(hatHz > 3000.0, "a closed hat lives in a different part of the spectrum entirely");
        std::cout << "  closed hat sits at " << juce::String(hatHz, 0) << " Hz" << std::endl;
        std::cout << std::endl;
    }

    // ---- Tune moves the pitch, by the octave it promises -------------------
    {
        auto centre = makeParams(1.0f, 0.5f);
        auto up = makeParams(1.0f, 1.0f);
        auto down = makeParams(1.0f, 0.0f);

        const auto from = (int) (0.150 * sampleRate);
        const auto to = (int) (0.650 * sampleRate);

        auto atCentre = renderOne(kick, *centre, 1.0);
        auto atTop = renderOne(kick, *up, 1.0);
        auto atBottom = renderOne(kick, *down, 1.0);

        const auto centreHz = dominantHz(atCentre, from, to);
        const auto topHz = dominantHz(atTop, from, to);
        const auto bottomHz = dominantHz(atBottom, from, to);

        check(topHz > centreHz * 1.8 && topHz < centreHz * 2.2, "Tune at the top is an octave up");
        check(bottomHz > centreHz * 0.4 && bottomHz < centreHz * 0.6, "Tune at the bottom is an octave down");

        std::cout << "  kick tunes " << juce::String(bottomHz, 1) << " / "
                  << juce::String(centreHz, 1) << " / " << juce::String(topHz, 1) << " Hz" << std::endl;

        // The two voices that are nothing but noise have no oscillator for Tune
        // to move, so it moves their filter instead. Without that they would
        // have a dial that does nothing, which is worse than no dial.
        //
        // The maraca is engine 13 and there is no slot 13, so it goes on a slot
        // to be heard. This read `renderOne(13, ...)` while a slot WAS its
        // sound, and once the kit came down to twelve that was an out-of-range
        // slot: silent, and the check failed with "0 -> 0 Hz" rather than with
        // anything about tuning.
        constexpr int maraca = 13;
        down->voices[0].engine.store(maraca);
        up->voices[0].engine.store(maraca);

        auto maracaLow = renderOne(0, *down, 0.3);
        auto maracaHigh = renderOne(0, *up, 0.3);

        const auto lowHz = dominantHz(maracaLow, 0, (int) (0.03 * sampleRate));
        const auto highHz = dominantHz(maracaHigh, 0, (int) (0.03 * sampleRate));

        check(highHz > lowHz * 1.5, "Tune brightens a voice that is only noise");
        std::cout << "  maraca tunes " << juce::String(lowHz, 0) << " -> "
                  << juce::String(highHz, 0) << " Hz" << std::endl;
        std::cout << std::endl;
    }

    // ---- Decay lengthens the hit, and the voice eventually stops -----------
    //
    // Both halves matter. A decay control that does nothing is a dead dial; an
    // exponential with no end is sixteen voices' worth of multiplies running
    // for the rest of the session.
    {
        auto shortDecay = makeParams(1.0f, 0.5f, 0.1f);
        auto longDecay = makeParams(1.0f, 0.5f, 0.9f);

        auto brief = renderOne(openHat, *shortDecay, 2.0);
        auto sustained = renderOne(openHat, *longDecay, 2.0);

        const auto from = (int) (0.4 * sampleRate);
        const auto to = (int) (0.9 * sampleRate);

        check(rmsOf(sustained, from, to) > rmsOf(brief, from, to) * 4.0,
              "a long decay is still ringing where a short one has gone");

        // And the voice really does switch itself off, leaving exact zeros
        // rather than something small.
        auto kit = makeKit();
        auto params = makeParams();
        auto buffer = renderHits(*kit, *params, { Hit{ closedHat, 1.0f, false, 0 } },
                                 (int) (1.0 * sampleRate), 512);

        check(!kit->isVoiceActive(closedHat), "a closed hat has finished within a second");
        check(isExactlySilent(buffer, (int) (0.8 * sampleRate), buffer.getNumSamples()),
              "and what it leaves behind is exact silence, not a tail below the noise floor");

        auto crashKit = makeKit();
        auto crashBuffer = renderHits(*crashKit, *params, { Hit{ crash, 1.0f, false, 0 } },
                                      (int) (1.0 * sampleRate), 512);
        juce::ignoreUnused(crashBuffer);

        check(crashKit->isVoiceActive(crash), "a crash is still going at the same point, which is the difference");
        std::cout << std::endl;
    }

    // ---- Level, velocity, accent and pan ------------------------------------
    {
        auto full = makeParams(1.0f);
        auto half = makeParams(0.5f);
        auto silent = makeParams(0.0f);

        auto atFull = renderOne(kick, *full, 0.5);
        auto atHalf = renderOne(kick, *half, 0.5);
        auto atZero = renderOne(kick, *silent, 0.5);

        const auto ratio = (double) peakOf(atFull) / juce::jmax(1.0e-9, (double) peakOf(atHalf));
        check(ratio > 1.99 && ratio < 2.01, "halving Level halves the output");
        check(isExactlySilent(atZero, 0, atZero.getNumSamples()),
              "Level at zero is exact silence, not a very quiet drum");

        auto loud = renderOne(kick, *full, 0.5, false, 1.0f);
        auto quiet = renderOne(kick, *full, 0.5, false, 0.4f);
        check(peakOf(loud) > peakOf(quiet) * 2.0, "velocity scales the hit");

        // Accent has to do something to a hit that is ALREADY at full velocity,
        // because the accents people write are on the loudest hits in the bar.
        // That is what the headroom in DrumVoice.cpp buys.
        auto accented = renderOne(kick, *full, 0.5, true, 1.0f);
        check(peakOf(accented) > peakOf(loud) * 1.2,
              "and accent is louder still, even at full velocity");

        auto left = makeParams(1.0f, 0.5f, 0.5f, 0.0f);
        auto right = makeParams(1.0f, 0.5f, 0.5f, 1.0f);

        auto hardLeft = renderOne(snare, *left, 0.5);
        auto hardRight = renderOne(snare, *right, 0.5);

        check(rmsOf(hardLeft, 0, hardLeft.getNumSamples(), 0) > 0.0, "panned hard left, a voice is in the left channel");
        check(rmsOf(hardLeft, 0, hardLeft.getNumSamples(), 1) < 1.0e-6, "and not in the right");
        check(rmsOf(hardRight, 0, hardRight.getNumSamples(), 1) > 0.0, "and the other way round");
        check(rmsOf(hardRight, 0, hardRight.getNumSamples(), 0) < 1.0e-6, "and not in the left");
    }

    // ---- Which controls a ringing hit still listens to ---------------------
    //
    // DrumVoice.h claims a split: Tune and Decay describe the hit and are held
    // for its life, Level and Pan are the mixing desk and are read every block.
    // That is a claim about behaviour, so it gets a test rather than a comment
    // - and it is the half of the design that a later automation lane drawn
    // across a two-second cymbal depends on entirely.
    {
        auto params = makeParams(1.0f);
        auto kit = makeKit();

        constexpr int total = 22050;
        constexpr int half = 11025;
        constexpr int block = 512;

        juce::AudioBuffer<float> buffer(2, total);
        buffer.clear();
        kit->trigger(kick, 1.0f, false, 0);

        bool moved = false;

        for (int position = 0; position < total; position += block)
        {
            if (!moved && position >= half)
            {
                params->voices[kick].level.store(0.25f);
                params->voices[kick].tune.store(1.0f);
                moved = true;
            }

            kit->renderAdding(buffer, position, juce::jmin(block, total - position), *params);
        }

        auto untouched = renderOne(kick, *makeParams(1.0f), 0.5);

        const auto from = half + 2000;
        const auto changed = rmsOf(buffer, from, total);
        const auto reference = rmsOf(untouched, from, total);

        check(changed < reference * 0.35 && changed > reference * 0.15,
              "Level moves a hit that is already ringing");
        check(dominantHz(buffer, from, total) < 70.0,
              "but Tune does not - a hit keeps the pitch it was struck at");

        std::cout << "  mid-hit: Level 1.0 -> 0.25 gives "
                  << juce::String(changed / juce::jmax(1.0e-9, reference), 3)
                  << " of the level, at " << juce::String(dominantHz(buffer, from, total), 1)
                  << " Hz still" << std::endl;
        std::cout << std::endl;
    }

    // ---- The same drum at a different sample rate is the same drum ---------
    //
    // Every envelope, every sweep and the filter are specified in seconds and
    // hertz, so this is a claim about the whole file at once: a coefficient
    // written with 44100 baked into it fails here and nowhere else.
    {
        auto params = makeParams();

        auto at44 = renderOne(kick, *params, 1.0, false, 1.0f, 44100.0);
        auto at96 = renderOne(kick, *params, 1.0, false, 1.0f, 96000.0);

        const auto hz44 = dominantHz(at44, (int) (0.15 * 44100.0), (int) (0.65 * 44100.0), 44100.0);
        const auto hz96 = dominantHz(at96, (int) (0.15 * 96000.0), (int) (0.65 * 96000.0), 96000.0);

        check(std::abs(hz44 - hz96) < hz44 * 0.05, "a kick is the same pitch at 96 kHz as at 44.1");

        const auto rms44 = rmsOf(at44, (int) (0.3 * 44100.0), (int) (0.4 * 44100.0));
        const auto rms96 = rmsOf(at96, (int) (0.3 * 96000.0), (int) (0.4 * 96000.0));

        check(std::abs(rms44 - rms96) < rms44 * 0.1, "and has decayed the same distance by 300 ms");

        std::cout << "  kick at 44.1k: " << juce::String(hz44, 1) << " Hz, at 96k: "
                  << juce::String(hz96, 1) << " Hz" << std::endl;
        std::cout << std::endl;
    }

    // ---- A queue that overflows says so ------------------------------------
    //
    // Silently dropping hits is the failure this is here to prevent. A pattern
    // that loses notes at some tempos and not others is nearly impossible to
    // diagnose from the sound alone, and a counter costs one int.
    {
        auto kit = makeKit();
        auto params = makeParams();

        for (int i = 0; i < Kit::maxPendingHits + 40; ++i)
            kit->trigger(i % numVoices, 1.0f, false, i % 512);

        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        kit->renderAdding(buffer, *params);

        check(kit->takeDroppedHits() == 40, "hits past the end of the queue are counted, not lost quietly");
        check(kit->takeDroppedHits() == 0, "and the count is cleared when it is read");
        check(allFinite(buffer), "and a block that full is still finite");

        // Out of range in the other direction: a voice number nobody has, and
        // an offset past the end of the block.
        auto safe = makeKit();
        safe->trigger(-1, 1.0f, false, 0);
        safe->trigger(numVoices, 1.0f, false, 0);
        safe->trigger(kick, 1.0f, false, 999999);

        juce::AudioBuffer<float> small(2, 64);
        small.clear();
        safe->renderAdding(small, *params);

        check(allFinite(small), "a voice out of range and an offset past the block are survivable");
        check(safe->isVoiceActive(kick), "and the late hit is clamped into the block rather than dropped");
    }

    // ---- A slot plays a recording instead of its own circuit ---------------
    //
    // Everything here uses a made-up sample rather than a file, and that is
    // the point of the split: this library knows what audio IS and nothing
    // about what a WAV is, so it can be tested without one.
    //
    // A ramp from -1 to 1, which is the most useful shape available. It is
    // nothing like a drum, and that is what makes it legible: the value at
    // any point says exactly how far through the recording the read head is,
    // so playback rate is readable off the output rather than inferred.
    {
        const auto makeRamp = [](double rate, double seconds)
        {
            auto sample = std::make_shared<Sample>();
            sample->sourceRate = rate;
            sample->name = "Ramp";
            sample->data.resize((size_t) (rate * seconds));

            for (size_t i = 0; i < sample->data.size(); ++i)
                sample->data[i] = -1.0f + 2.0f * (float) i / (float) sample->data.size();

            return sample;
        };

        auto ramp = makeRamp(sampleRate, 0.25);

        SampleBank bank;
        bank.slots[(size_t) kick] = ramp.get();

        auto params = makeParams();

        // Centred Tune and a Decay at or above centre, so the recording plays
        // as it was loaded and the numbers below are about the slot rather
        // than about the envelope.
        for (auto& voice : params->voices)
        {
            voice.pan.store(0.5f);
            voice.decay.store(1.0f);
        }

        auto kit = makeKit();
        kit->setSampleBank(&bank);

        auto played = renderHits(*kit, *params, { Hit{ kick, 1.0f, false, 0 } },
                                 (int) (sampleRate * 0.5), 512);

        // The ramp read back. Sampled well inside the recording so the end
        // fade and the interpolator's edges are not what is being measured.
        //
        // The voice is centred, so each channel carries 0.707 of it, and an
        // unaccented hit at full velocity is worth accentHeadroom. Rather
        // than restate either constant here, the test asks about the SHAPE:
        // a ramp read at the right speed is linear in time, so the value a
        // tenth of the way in and the value two tenths in must differ by the
        // same amount as two tenths and three tenths.
        const auto at = [&played](double seconds)
        {
            return (double) played.getSample(0, (int) (sampleRate * seconds));
        };

        const auto firstStep = at(0.15) - at(0.10);
        const auto secondStep = at(0.20) - at(0.15);

        check(firstStep > 0.0, "a slot holding a rising ramp rises");
        check(std::abs(firstStep - secondStep) < std::abs(firstStep) * 0.05,
              "and rises evenly, which is a recording read at one sample per sample");

        // Off the end is silence rather than a held value or a wrap. A clamp
        // would leave the last sample sitting as DC for the rest of the
        // voice's life, which is a click waiting to happen.
        check(isExactlySilent(played, (int) (sampleRate * 0.3), (int) (sampleRate * 0.5)),
              "and past the end of the recording the slot is exactly silent");

        // Where the output last has anything in it, which for a ramp ending
        // at +1 is unambiguous.
        const auto lastSounding = [](const juce::AudioBuffer<float>& b)
        {
            for (int i = b.getNumSamples() - 1; i >= 0; --i)
                if (std::abs(b.getSample(0, i)) > 0.001f)
                    return i;

            return 0;
        };

        // Tune moves the read head, so the same ramp played an octave up is
        // over in half the time.
        auto up = makeParams();
        for (auto& voice : up->voices)
        {
            voice.pan.store(0.5f);
            voice.decay.store(1.0f);
            voice.tune.store(1.0f);       // an octave up
        }

        auto fast = makeKit();
        fast->setSampleBank(&bank);
        auto rushed = renderHits(*fast, *up, { Hit{ kick, 1.0f, false, 0 } },
                                 (int) (sampleRate * 0.5), 512);

        const auto normalEnd = (double) lastSounding(played) / sampleRate;
        const auto fastEnd = (double) lastSounding(rushed) / sampleRate;

        check(fastEnd < normalEnd * 0.6,
              "an octave up plays the same recording in about half the time");

        // A recording made at a DIFFERENT rate from the one being rendered.
        //
        // This is the risk the plan named before any of this was written -
        // "samples load at their own rate and must be resampled to the
        // host's, or everything is detuned" - and every other check here
        // would miss it, because they all use a sample recorded at exactly
        // the rate the test renders at, where the ratio is 1 and a playback
        // path that ignored the rate entirely would look perfect.
        //
        // The same NUMBER OF FRAMES at half the rate, which is the only
        // arrangement that discriminates. The first attempt used the same
        // DURATION at half the rate - half as many frames - and a path that
        // ignored the rate played it in half the time, which is also what
        // playing it correctly does. It passed while testing nothing.
        //
        // 0.5 seconds at half the rate is 11025 frames, exactly as many as
        // 0.25 seconds at the full rate. Read properly that is half a second
        // of audio; read as though it were at the host's rate it is a quarter.
        auto halfRate = makeRamp(sampleRate * 0.5, 0.5);

        SampleBank slowBank;
        slowBank.slots[(size_t) kick] = halfRate.get();

        auto slow = makeKit();
        slow->setSampleBank(&slowBank);
        auto stretched = renderHits(*slow, *params, { Hit{ kick, 1.0f, false, 0 } },
                                    (int) (sampleRate * 0.8), 512);

        const auto stretchedEnd = (double) lastSounding(stretched) / sampleRate;

        check(stretchedEnd > normalEnd * 1.8 && stretchedEnd < normalEnd * 2.2,
              "a recording made at half the rate plays for twice as long ("
                  + juce::String(stretchedEnd, 3) + " s against " + juce::String(normalEnd, 3) + ")");

        // A slot with nothing in it is the synthesised voice, untouched. The
        // whole claim of "either, not both" rests on this: loading a sample
        // onto one voice must not change any of the other fifteen.
        auto bare = makeKit();
        auto withoutBank = renderHits(*bare, *params, { Hit{ snare, 1.0f, false, 0 } },
                                      16384, 512);

        auto beside = makeKit();
        beside->setSampleBank(&bank);
        auto withBank = renderHits(*beside, *params, { Hit{ snare, 1.0f, false, 0 } },
                                   16384, 512);

        check(identical(withoutBank, withBank),
              "and a sample on the kick leaves the snare bit for bit unchanged");

        // The tail a host is told about has to follow the recording, not the
        // table row it replaced.
        //
        // Every slot filled, not just the kick, and the first version of this
        // made that mistake: the report is the LONGEST of the sixteen, so one
        // short sample against fifteen synthesised voices is hidden behind
        // whichever of them rings longest. With Decay at the top that is the
        // crash at thirty-six seconds, and the check passed nothing.
        SampleBank everySlot;
        for (auto& slot : everySlot.slots)
            slot = ramp.get();

        const auto synthTail = longestTailSeconds(*params);
        const auto sampleTail = longestTailSeconds(*params, &everySlot);

        check(sampleTail < 0.4 && synthTail > 10.0,
              "and the reported tail follows the recording rather than the circuit ("
                  + juce::String(sampleTail, 2) + " s against " + juce::String(synthTail, 2) + ")");

        std::cout << "  a slot plays a recording: " << juce::String(normalEnd, 3) << " s at pitch, "
                  << juce::String(fastEnd, 3) << " s an octave up" << std::endl;
    }

    // ---- Hitting a drum that is still ringing does not click ---------------
    //
    // Reported from use: a click on the Kick and the Toms when a hit lands on
    // one that has not finished, and Attack made no difference. Both halves
    // of that point at the same thing.
    //
    // The kick and the three toms are the only voices whose body is a low
    // sine. Retriggering replaces every piece of the voice's state at once,
    // including the oscillator phase - so the output jumps from wherever the
    // old hit's sine happened to be straight to where the new one starts.
    // On a hat that step hides inside noise; on a 52 Hz sine it is the only
    // discontinuity in the signal and it is plainly audible.
    //
    // Attack cannot help, and that is the giveaway rather than a second
    // problem: it shapes how the NEW hit arrives. Nothing it does can smooth
    // how the OLD one stopped.
    //
    // Measured as the biggest one-sample jump, against the biggest jump a
    // hit starting from silence makes. A fresh kick onset is genuinely fast -
    // a click envelope and a pitch sweep starting at four and a half times
    // the base - so "no jump at all" would be the wrong thing to ask for. The
    // right question is whether retriggering jumps MORE than starting does.
    {
        auto params = makeParams();
        juce::StringArray measured;

        // What is actually on the slot, rather than what the slot is called.
        // These four are at their defaults so the two agree, but asking the
        // parameters is what keeps this honest if a default ever moves.
        const auto soundOn = [&params](int slot)
        {
            return juce::String(engineName(params->voices[(size_t) slot].engine.load()));
        };

        for (const auto voice : { kick, 4, 5, 6 })   // Kick, LoTom, MidTom, HiTom
        {
            constexpr int retriggerAt = 11025;       // a quarter second in
            constexpr int total = 44100;

            const std::vector<Hit> hits = {
                { voice, 1.0f, false, 0 },
                { voice, 1.0f, false, retriggerAt }
            };

            auto rendered = renderHits(*makeKit(), *params, hits, total, 512);

            // A window either side of the retrigger, and one over the opening
            // of the same voice from silence.
            const auto atRetrigger = maxStep(rendered, retriggerAt - 4, retriggerAt + 64);
            const auto atOnset = maxStep(rendered, 1, 64);

            check(atRetrigger <= atOnset * 1.5f,
                  soundOn(voice) + ": retriggering it mid-ring jumps no harder"
                      + " than striking it from silence (" + juce::String(atRetrigger, 4)
                      + " against " + juce::String(atOnset, 4) + ")");

            measured.add(soundOn(voice) + " " + juce::String(atRetrigger, 4)
                         + "/" + juce::String(atOnset, 4));
        }

        // And the same voice with Attack turned up, because that is the
        // combination that was reported. A soft attack must not reintroduce
        // it - the old hit's ending is a separate thing from the new hit's
        // arrival, and the fix has to be on the ending.
        auto softened = makeParams();

        for (auto& voice : softened->voices)
            voice.attack.store(1.0f);

        const std::vector<Hit> hits = {
            { kick, 1.0f, false, 0 },
            { kick, 1.0f, false, 11025 }
        };

        auto soft = renderHits(*makeKit(), *softened, hits, 44100, 512);

        check(maxStep(soft, 11021, 11089) <= maxStep(soft, 1, 64) * 1.5f + 0.002f,
              "and with Attack up it still does not, which is what Attack could never fix");

        std::cout << "  retrigger jump against fresh-onset jump: " << measured.joinIntoString(", ")
                  << std::endl;
    }

    // ---- Attack shapes the front of the hit --------------------------------
    //
    // Measured at the front rather than over the whole hit, because that is
    // the only place it does anything: a crash with a 600ms swell and one
    // without are the same crash a second in.
    //
    // The reach is per voice, so the numbers below are per voice too - the
    // crash's dial goes to 600ms and the rim's to ten, and a test written
    // against one fixed maximum would be testing a table it had made up.
    {
        const auto frontRms = [](const juce::AudioBuffer<float>& b, double seconds)
        {
            return rmsOf(b, 0, (int) (sampleRate * seconds));
        };

        auto instant = makeParams();
        auto swelled = makeParams();

        for (auto& voice : swelled->voices)
            voice.attack.store(1.0f);

        // A crash: 600ms of reach, so the first fifty milliseconds should be
        // most of the way to nothing.
        auto crashFast = renderOne(crash, *instant, 3.0);
        auto crashSlow = renderOne(crash, *swelled, 3.0);

        check(frontRms(crashSlow, 0.05) < frontRms(crashFast, 0.05) * 0.2,
              "a crash at full Attack is far quieter over its first fifty milliseconds");

        // And it has not simply been made quieter: by a second in, the swell
        // is over and the two are within sight of each other. A control that
        // turned out to be a volume knob would fail this half and pass the
        // one above.
        const auto lateFast = rmsOf(crashFast, (int) (sampleRate * 1.0), (int) (sampleRate * 1.5));
        const auto lateSlow = rmsOf(crashSlow, (int) (sampleRate * 1.0), (int) (sampleRate * 1.5));

        check(lateSlow > lateFast * 0.5,
              "and a second later it is still a crash, not a quieter one");

        // The default has to be exactly what the kit did before Attack
        // existed. Not nearly - the first sample of a hit at Attack zero is
        // the hit's own first sample, because zero attack is zero samples of
        // ramp rather than a very short one.
        auto defaults = makeParams();
        auto asBefore = renderOne(kick, *defaults, 0.5);
        auto explicitZero = makeParams();

        for (auto& voice : explicitZero->voices)
            voice.attack.store(0.0f);

        check(identical(asBefore, renderOne(kick, *explicitZero, 0.5)),
              "Attack defaults to zero, and zero is the instant strike this kit always had");

        // A rim shot's dial reaches a quarter of a rim shot, so full Attack on
        // it is a nudge rather than a swell - which is the whole point of the
        // reach being relative. If every voice shared one maximum in seconds
        // this would be a rim shot that had disappeared.
        auto rimSlow = renderOne(2, *swelled, 0.5);          // Rim
        check(peakOf(rimSlow) > 0.05f,
              "and full Attack on a rim shot still leaves a rim shot");

        // ---- Attack is measured against the CURRENT decay ------------------
        //
        // The defect this replaced: the reach was a fixed number of seconds
        // per voice, measured against the voice's NOMINAL length, so it did
        // not follow the Decay dial. A Kick at Decay minimum rings for about
        // 140 ms and could still be given a 120 ms attack - 86% of the sound,
        // a swell with no drum on the end of it - while the same dial position
        // at Decay maximum was 9%. One dial meaning two different things
        // depending on where another sits.
        {
            const auto& kickSpec = engineSpec(0);

            const auto atDecay = [&kickSpec](float decay)
            { return attackSecondsFor(kickSpec, 1.0f, decay); };

            const auto shortDecay = decaySecondsFor(kickSpec, 0.0f);
            const auto longDecay = decaySecondsFor(kickSpec, 1.0f);

            check(atDecay(0.0f) < atDecay(1.0f),
                  "a shorter drum has a shorter attack range");

            // The ratio is what is actually being claimed, so the ratio is what
            // is checked. A quarter of the decay either end, which is a fifth
            // of the whole hit - the 20:80 the kit is built around.
            const auto shortShare = atDecay(0.0f) / shortDecay;
            const auto longShare = atDecay(1.0f) / longDecay;

            check(std::abs(shortShare - 0.25f) < 0.01f && std::abs(longShare - 0.25f) < 0.01f,
                  "and the share is the same at both ends: "
                      + juce::String(shortShare, 3) + " against " + juce::String(longShare, 3));

            // Every engine, not just the kick - this replaced sixteen
            // hand-tuned numbers with one rule, and a rule that held for the
            // kick alone would be sixteen numbers again.
            bool everyEngineAgrees = true;

            for (int engine = 0; engine < numEngines; ++engine)
            {
                const auto& spec = engineSpec(engine);

                for (const auto decay : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                {
                    const auto share = attackSecondsFor(spec, 1.0f, decay)
                                       / decaySecondsFor(spec, decay);

                    if (std::abs(share - 0.25f) > 0.01f)
                        everyEngineAgrees = false;
                }
            }

            check(everyEngineAgrees, "on every engine at every Decay setting");

            // Zero stays EXACTLY zero, whatever the curve does. It is the
            // default on every voice and the thing "Off" means, and an
            // exponential that bottomed out at a few microseconds instead
            // would make every untouched drum very slightly late.
            bool zeroIsZero = true;

            for (int engine = 0; engine < numEngines; ++engine)
                for (const auto decay : { 0.0f, 0.5f, 1.0f })
                    if (attackSecondsFor(engineSpec(engine), 0.0f, decay) != 0.0f)
                        zeroIsZero = false;

            check(zeroIsZero, "and Attack at zero is exactly zero, not nearly zero");

            // The curve leans to the short end: half the dial reaches well
            // under half the range. That is what makes the first few
            // milliseconds - where taking the click off a kick lives - usable
            // rather than crammed into the bottom of the travel.
            const auto half = attackSecondsFor(kickSpec, 0.5f, 0.5f);
            const auto full = attackSecondsFor(kickSpec, 1.0f, 0.5f);

            check(half < full * 0.2f,
                  "half the dial is under a fifth of the range, so the short attacks have room ("
                      + juce::String(half * 1000.0f, 1) + " ms of "
                      + juce::String(full * 1000.0f, 1) + " ms)");

            std::cout << "  kick attack range: " << juce::String(atDecay(0.0f) * 1000.0f, 1)
                      << " ms at Decay 0, " << juce::String(atDecay(1.0f) * 1000.0f, 1)
                      << " ms at Decay 1" << std::endl;
        }

        std::cout << "  attack: crash front " << juce::String(frontRms(crashSlow, 0.05), 4)
                  << " swelled against " << juce::String(frontRms(crashFast, 0.05), 4)
                  << " instant" << std::endl;
    }

    // ---- The rack costs nothing until something is sent to it --------------
    //
    // Routing every voice to Off has to give back the same samples the kit
    // gave before there were sends at all. Not approximately: the send path
    // renders each voice through a scratch buffer and adds it on, and "adds
    // the same numbers in a different order" is exactly the kind of change
    // that is inaudible until it is not.
    //
    // Exact is allowed here for the reason the block-size identity is: adding
    // a voice into a cleared scratch and then adding the scratch into the mix
    // computes (0 + v) + m, and 0 + v is v.
    {
        auto params = makeParams();

        const std::vector<Hit> hits = {
            { kick,  1.0f, false,     0 },
            { snare, 0.9f, false,  4096 },
            { crash, 1.0f, false,  8192 }
        };

        constexpr int total = 32768;

        // Every slot is already off in a fresh KitParameters, which is the
        // default this is checking as much as the arithmetic.
        auto quiet = renderHits(*makeKit(), *params, hits, total, 512);

        check(peakOf(quiet) > 0.05f, "with no effects there is still a kit");
        check(allFinite(quiet), "and nothing in it is infinite");

        // The sharp version of "inert": turn every unit in the rack up to full
        // with nothing pointed at them, and the samples must not move at all.
        //
        // Weaker phrasings of this pass without testing anything. Checking
        // that the output is non-silent would pass with the units permanently
        // in every voice's path; checking that it is finite would pass with
        // them adding a constant. Only "the same bytes" says they are out of
        // the path.
        auto loudRack = makeParams();
        loudRack->fx.reverbLevel.store(1.0f);
        loudRack->fx.driveLevel.store(1.0f);
        loudRack->fx.eqLevel.store(1.0f);
        loudRack->fx.delayLevel.store(1.0f);

        auto withRackUp = renderHits(*makeKit(), *loudRack, hits, total, 512);

        check(identical(quiet, withRackUp),
              "and turning every unit to full changes nothing while every slot is Off");

        // A slot POINTED at a unit but with its Amount at zero has to be just
        // as inert, and this is the half that could easily not be. "Off" is a
        // selector position and could plausibly be the only thing the render
        // path checks; an Amount of zero has to mix to exactly the input, and
        // "exactly" is the word doing the work - a mix that landed a bit away
        // from the input would be inaudible here and wrong everywhere.
        auto selectedButDry = makeParams();

        for (auto& voice : selectedButDry->voices)
        {
            voice.send1.store(1);
            voice.send2.store(4);
        }

        selectedButDry->fx.reverbLevel.store(1.0f);
        selectedButDry->fx.delayLevel.store(1.0f);

        auto dry = renderHits(*makeKit(), *selectedButDry, hits, total, 512);

        check(identical(quiet, dry),
              "and a slot with a unit selected but an Amount of zero is the input, bit for bit");

        // And with an Amount up, the SAME hits must come out different. A
        // chain wired to nothing would pass everything above.
        auto routed = makeParams();
        routed->voices[(size_t) snare].send1.store(1);      // the reverb
        routed->voices[(size_t) snare].send1Amount.store(0.5f);
        routed->fx.reverbLevel.store(1.0f);

        auto wet = renderHits(*makeKit(), *routed, hits, total, 512);

        check(!identical(quiet, wet), "and with the snare through the reverb it is not the same kit");
        check(allFinite(wet), "and that is still finite");

        std::cout << "  a chain costs nothing until an Amount is turned up" << std::endl;
    }

    // ---- The two slots are a chain, not two parallel sends ------------------
    //
    // The claim the whole design rests on, and the one measurement that can
    // tell the two apart.
    //
    // Distortion in slot 1 and distortion again in slot 2, both at full. In a
    // chain that is the voice driven twice - clipped, then clipped again -
    // and tanh applied twice is not tanh applied once. Run as two parallel
    // sends it would be the same distorted copy added to itself, which is the
    // same shape at twice the level; so the test asks about SHAPE rather than
    // level, by comparing against one slot at full against a doubled single
    // slot.
    {
        const auto renderThrough = [](int unit1, float amount1, int unit2, float amount2)
        {
            auto params = makeParams();
            params->voices[(size_t) kick].send1.store(unit1);
            params->voices[(size_t) kick].send1Amount.store(amount1);
            params->voices[(size_t) kick].send2.store(unit2);
            params->voices[(size_t) kick].send2Amount.store(amount2);

            // Hard drive, so a second pass has something left to do. At a
            // gentle setting tanh is nearly linear and twice through it is
            // nearly once, which would make this test true and useless.
            params->fx.driveAmount.store(1.0f);
            params->fx.driveTone.store(1.0f);
            params->fx.driveLevel.store(1.0f);

            return renderHits(*makeKit(), *params, { Hit{ kick, 1.0f, false, 0 } }, 8192, 512);
        };

        auto once = renderThrough(2, 1.0f, 0, 0.0f);
        auto twice = renderThrough(2, 1.0f, 2, 1.0f);

        check(!identical(once, twice),
              "running a voice through the distortion twice is not the same as running it once");

        // The direction is the giveaway: a second pass through a soft clipper
        // can only flatten what the first one left, so the RMS of the twice-
        // driven version sits closer to its own peak. Parallel copies of one
        // distortion would have exactly the same ratio.
        const auto crest = [](const juce::AudioBuffer<float>& b)
        {
            const auto rms = rmsOf(b, 0, 4096);
            return rms > 0.0 ? peakOf(b) / (float) rms : 0.0f;
        };

        check(crest(twice) < crest(once),
              "and the second pass flattens it further, which only a chain can do");

        std::cout << "  slot 2 is handed slot 1's output: crest "
                  << juce::String(crest(once), 3) << " once, "
                  << juce::String(crest(twice), 3) << " twice" << std::endl;
    }

    // ---- The order of the chain is the order of the slots -------------------
    //
    // Distortion then EQ is a different sound from EQ then distortion: the
    // first clips the whole signal and then tilts what came out, the second
    // tilts it and then clips the tilted version. If the two slots were a set
    // rather than a sequence these would be identical.
    {
        const auto renderOrder = [](int first, int second)
        {
            auto params = makeParams();
            params->voices[(size_t) snare].send1.store(first);
            params->voices[(size_t) snare].send1Amount.store(1.0f);
            params->voices[(size_t) snare].send2.store(second);
            params->voices[(size_t) snare].send2Amount.store(1.0f);

            params->fx.driveAmount.store(0.9f);
            params->fx.driveTone.store(1.0f);
            params->fx.driveLevel.store(1.0f);
            params->fx.eqLowGain.store(4.0f);
            params->fx.eqHighGain.store(0.25f);
            params->fx.eqLevel.store(1.0f);

            return renderHits(*makeKit(), *params, { Hit{ snare, 1.0f, false, 0 } }, 8192, 512);
        };

        auto driveThenEq = renderOrder(2, 3);
        auto eqThenDrive = renderOrder(3, 2);

        check(!identical(driveThenEq, eqThenDrive),
              "distortion into EQ is not the same as EQ into distortion");

        std::cout << "  and the slots are in order, not in a set" << std::endl;
    }

    // ---- A send survives being chopped into blocks -------------------------
    //
    // The same property as the identity test above, now with the rack in the
    // path - and it is a harder claim, because the send path splits a block at
    // every hit AND at the scratch buffer's capacity, and the four units carry
    // state across those splits.
    //
    // A unit whose state advanced per block rather than per sample would pass
    // every other check here and fail this one.
    //
    // All FOUR units at once, one voice into each, and that is not thoroughness
    // for its own sake. Routing everything to the reverb - which is what this
    // did first - proves almost nothing: juce::Reverb's shortest comb is about
    // 1100 samples, so a reverb wrongly reset at every segment boundary simply
    // outputs silence at any block size this test uses, and silence matches
    // silence. The drive and the EQ are one-pole filters whose state moves
    // every sample, and the delay reads a line written a segment or more ago;
    // those three cannot agree across two segmentations unless they really are
    // continuous.
    {
        auto params = makeParams();

        params->voices[(size_t) kick].send1.store(1);       // reverb
        params->voices[(size_t) closedHat].send1.store(2);  // distortion
        params->voices[(size_t) snare].send1.store(3);      // EQ
        params->voices[(size_t) snare].send2.store(4);      // into the delay

        for (auto& voice : params->voices)
        {
            voice.send1Amount.store(0.7f);
            voice.send2Amount.store(0.7f);
        }

        params->fx.reverbLevel.store(0.8f);
        params->fx.driveLevel.store(0.8f);
        params->fx.eqLevel.store(0.8f);
        params->fx.eqLowGain.store(2.0f);
        params->fx.eqHighGain.store(0.5f);
        params->fx.delayLevel.store(0.8f);
        params->fx.delayFeedback.store(0.5f);

        const std::vector<Hit> hits = {
            { kick,      1.0f, false,     0 },
            { closedHat, 0.7f, false,   331 },
            { snare,     0.9f, false,  4097 }
        };

        constexpr int total = 32768;

        auto whole = renderHits(*makeKit(), *params, hits, total, total);
        auto in64 = renderHits(*makeKit(), *params, hits, total, 64);
        auto odd = renderHits(*makeKit(), *params, hits, total, 173);

        check(identical(whole, in64), "a sent kit rendered whole and in 64-sample blocks agrees exactly");
        check(identical(whole, odd), "and in blocks of an awkward size");

        std::cout << "  and all four units are sample-accurate however the block is cut"
                  << std::endl;
    }

    // ---- The delay puts the hit back where it was told to ------------------
    //
    // The one unit whose behaviour can be stated as a number rather than as a
    // difference, so it is worth stating: a hit sent to a delay set to a fifth
    // of a second has to come back a fifth of a second later.
    {
        auto params = makeParams();

        params->voices[(size_t) kick].send1.store(4);   // the delay
        params->voices[(size_t) kick].send1Amount.store(0.5f);
        params->fx.delaySeconds.store(0.2f);
        params->fx.delayFeedback.store(0.0f);           // one repeat, not a train
        params->fx.delayLevel.store(1.0f);

        const int total = (int) (sampleRate * 0.6);
        auto rendered = renderHits(*makeKit(), *params, { Hit{ kick, 1.0f, false, 0 } }, total, 512);

        // A window around where the repeat should land, against one just
        // before it. The kick itself is still decaying underneath, so this
        // compares two windows of the same signal rather than asking for
        // silence: the repeat has to make its window LOUDER than the tail it
        // sits on, which the dry decay alone can never do.
        const auto beforeRepeat = rmsOf(rendered, (int) (sampleRate * 0.15), (int) (sampleRate * 0.19));
        const auto atRepeat = rmsOf(rendered, (int) (sampleRate * 0.20), (int) (sampleRate * 0.24));

        check(atRepeat > beforeRepeat,
              "a hit sent to the delay comes back louder at 0.2 s than the tail it lands on");

        std::cout << "  the delay repeats at the time it is set to: "
                  << juce::String(atRepeat, 4) << " against " << juce::String(beforeRepeat, 4)
                  << " just before" << std::endl;
    }

    // ---- What the lamps beside the Level dials are reading -----------------
    //
    // takeVoicePeak is the whole of it: the panel turns a number into a
    // brightness and a colour, and everything that could actually be wrong is
    // in what the number says.
    {
        std::cout << std::endl << "Per-voice metering" << std::endl;

        constexpr int kick = 0;
        constexpr int snare = 1;

        auto kit = makeKit();
        auto params = makeParams();

        const auto block = (int) (sampleRate * 0.1);
        juce::AudioBuffer<float> buffer(2, block);
        buffer.clear();

        kit->trigger(kick, 1.0f, false, 0);
        kit->renderAdding(buffer, 0, block, *params);

        const auto kickPeak = kit->takeVoicePeak(kick);
        check(kickPeak > 0.0f, "a voice that has been struck reports a peak");

        // The one that was struck, and not the other fifteen. A meter that
        // reads the mix would light every lamp on every hit, which is the
        // mistake worth guarding against - it looks plausible until you
        // notice the whole row flashing at once.
        check(kit->takeVoicePeak(snare) == 0.0f,
              "and the voices that were not stay dark");

        // Reading is what clears it, so the next look sees only what has
        // happened since. Without this a lamp would latch on at the first hit
        // and never go out.
        check(kit->takeVoicePeak(kick) == 0.0f, "reading the peak clears it");

        buffer.clear();
        kit->renderAdding(buffer, 0, block, *params);
        const auto tailPeak = kit->takeVoicePeak(kick);

        check(tailPeak > 0.0f && tailPeak < kickPeak,
              "and the decaying tail still reads, quieter than the hit did");
    }

    {
        // Measured AFTER the sends, which is the claim made beside the code.
        // The same hit at the same Level, once dry and once driven into the
        // distortion unit: if the meter were reading the voice before its
        // chain, the two would be identical.
        constexpr int kick = 0;
        const auto block = (int) (sampleRate * 0.1);

        const auto peakWith = [&](bool driven)
        {
            auto kit = makeKit();
            auto params = makeParams();

            if (driven)
            {
                params->voices[(size_t) kick].send1.store(2); // the distortion
                params->voices[(size_t) kick].send1Amount.store(1.0f);
                params->fx.driveAmount.store(1.0f);
                params->fx.driveLevel.store(1.0f);
            }

            juce::AudioBuffer<float> buffer(2, block);
            buffer.clear();

            kit->trigger(kick, 1.0f, false, 0);
            kit->renderAdding(buffer, 0, block, *params);

            return kit->takeVoicePeak(kick);
        };

        const auto dry = peakWith(false);
        const auto driven = peakWith(true);

        // LOWER, not higher, and that is the interesting part. The drive
        // compensates its own gain - makeup is 1/sqrt(gain), so a tanh that
        // saturates at 1 comes out at 0.2 with the dial at the top. A meter
        // reading the voice before its chain would report the same 0.53 in
        // both of these, which is what makes them worth running as a pair.
        check(driven < dry * 0.75f,
              "the meter reads after the sends, so a compensated drive lowers it");

        std::cout << "  dry " << juce::String(dry, 3) << " against driven "
                  << juce::String(driven, 3) << std::endl;
    }

    {
        // The red half of the lamp has to be reachable, or it is decoration.
        //
        // The loudest a voice can legitimately be: a recording that already
        // peaks at full scale, played at full Level and master with an accent,
        // panned hard so the whole of it lands in one channel rather than
        // being split by the constant-power law. That is a real setting
        // somebody can dial, not a contrived one.
        constexpr int kick = 0;

        auto square = std::make_shared<Sample>();
        square->sourceRate = sampleRate;
        square->name = "Full scale";
        square->data.assign((size_t) (sampleRate * 0.25), 1.0f);

        SampleBank bank;
        bank.slots[(size_t) kick] = square.get();

        auto params = makeParams();
        params->masterLevel.store(1.0f);

        for (auto& voice : params->voices)
        {
            voice.level.store(1.0f);
            voice.decay.store(1.0f);
            voice.pan.store(1.0f);
        }

        auto kit = makeKit();
        kit->setSampleBank(&bank);

        const auto block = (int) (sampleRate * 0.1);
        juce::AudioBuffer<float> buffer(2, block);
        buffer.clear();

        kit->trigger(kick, 1.0f, true, 0);
        kit->renderAdding(buffer, 0, block, *params);

        const auto peak = kit->takeVoicePeak(kick);

        check(peak >= 1.0f, "a voice can read at or above full scale, so the clip lamp lights");

        std::cout << "  loudest a voice reaches: " << juce::String(peak, 3) << std::endl;
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL DRUM VOICE TESTS PASSED" << std::endl;
    else
        std::cout << failures << " DRUM VOICE TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
