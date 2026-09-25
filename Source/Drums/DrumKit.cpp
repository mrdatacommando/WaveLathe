// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DrumKit.h"

namespace drums
{
// The one place both halves of retirementGraceMs's arithmetic are visible, so
// this is where it is held to it. Slowest Tune is half speed, hence the x2.
static_assert((double) retirementGraceMs / 1000.0 > maxSampleSeconds * 2.0 + fxTailSeconds,
              "A displaced sample can now outlive its grace period: a voice would be "
              "reading freed memory. Raise retirementGraceMs in DrumSample.h.");

Kit::Kit()
{
    allocateWorkingBuffers();
}

void Kit::allocateWorkingBuffers()
{
    // Stereo whatever the output turns out to be. A mono host uses the first
    // channel and leaves the second alone, which costs a buffer nobody reads;
    // sizing these to the output's channel count instead would mean
    // reallocating on the audio thread the first time a host changed layout.
    voiceScratch.setSize(2, blockCapacity, false, true, false);
    fxScratch.setSize(2, blockCapacity, false, true, false);
}

void Kit::prepare(double newSampleRate, int maximumBlockSize)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    blockCapacity = juce::jmax(1, maximumBlockSize);

    allocateWorkingBuffers();

    for (auto& chain : chains)
        chain.prepare(sampleRate, blockCapacity);

    for (int i = 0; i < numVoices; ++i)
    {
        // A different seed per voice, derived from the index rather than from a
        // clock, so a render is the same every time it is run. The multiplier
        // is the odd 32-bit golden-ratio constant, which spreads consecutive
        // indices across the whole range instead of leaving twelve seeds that
        // differ in their bottom four bits.
        const auto seed = (juce::uint32) (0x9e3779b9u * (juce::uint32) (i + 1)) | 1u;
        voices[(size_t) i].prepare(sampleRate, seed);
    }

    reset();
}

void Kit::reset()
{
    for (auto& voice : voices)
        voice.reset();

    for (auto& chain : chains)
        chain.reset();

    voiceScratch.clear();
    fxScratch.clear();

    numPending = 0;
    droppedHits.store(0);
}

void Kit::trigger(int voice, float velocity, bool accent, int sampleOffset)
{
    if (voice < 0 || voice >= numVoices)
        return;

    if (numPending >= maxPendingHits)
    {
        droppedHits.fetch_add(1);
        return;
    }

    PendingHit hit;
    hit.voice = voice;
    hit.velocity = velocity;
    hit.accent = accent;
    hit.offset = juce::jmax(0, sampleOffset);

    // Insertion sort, walking back from the end. Hits usually arrive in time
    // order already, so this usually compares once and stops. Equal offsets
    // keep the order they were queued in, which is the only sane answer when a
    // closed hat and an open hat are asked for at the same instant: the second
    // one chokes the first, exactly as it would a moment later.
    int at = numPending;
    while (at > 0 && pending[(size_t) (at - 1)].offset > hit.offset)
    {
        pending[(size_t) at] = pending[(size_t) (at - 1)];
        --at;
    }

    pending[(size_t) at] = hit;
    ++numPending;
}

void Kit::startHit(const PendingHit& hit, const KitParameters& params)
{
    // The bank read once here rather than per sample: a hit plays whatever
    // was loaded at the instant it was struck, and a sample arriving
    // mid-hit does not swap the audio out from under it.
    const auto* bank = sampleBank.load(std::memory_order_acquire);
    const auto* loaded = (bank != nullptr && bank->has(hit.voice)) ? bank->slots[(size_t) hit.voice]
                                                                   : nullptr;

    // Which sound this slot is set to, latched here with the hit rather than
    // read while it rings.
    const auto engine = params.voices[(size_t) hit.voice].engine.load(std::memory_order_relaxed);

    // A blank slot with nothing on it makes no sound, and a hit on it is not an
    // event - not a silent voice that still occupies the render, and not a
    // kick because engineSpec had to return something. The grid can carry
    // hits on a slot the sample was later cleared from, so this is a state
    // somebody reaches by ordinary use rather than a corrupt one.
    if (engine == noEngine && loaded == nullptr)
        return;

    const auto& spec = engineSpec(engine);

    // The choke. A closed hat cuts an open one, and that is the whole of it on
    // an 808 - the cymbals ring over each other and are meant to.
    //
    // Applied HERE, at the sample the new hit actually starts, rather than when
    // it was queued. A choke applied at queue time would cut the open hat at
    // the top of the block however late in the block the closed hat lands,
    // which at a 512-sample buffer is up to twelve milliseconds early: audible,
    // and worse, dependent on the buffer size the host happened to choose.
    //
    // The group comes from each slot's OWN engine now, not from its position,
    // so putting a closed hat on two slots makes them cut each other the way
    // two closed hats should - and moving the open hat elsewhere takes its
    // choke with it.
    //
    // A slot playing a sample keeps the choke group of whatever engine it is
    // set to, which is what lets a sampled hat still cut a sampled open hat.
    if (spec.chokeGroup != 0 && engine != noEngine)
    {
        for (int v = 0; v < numVoices; ++v)
        {
            if (v == hit.voice)
                continue;

            const auto other = params.voices[(size_t) v].engine.load(std::memory_order_relaxed);

            if (other != noEngine && engineSpec(other).chokeGroup == spec.chokeGroup)
                voices[(size_t) v].choke();
        }
    }

    voices[(size_t) hit.voice].trigger(hit.velocity, hit.accent, spec,
                                       params.voices[(size_t) hit.voice], loaded);
}

void Kit::renderAdding(juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                       const KitParameters& params)
{
    if (numSamples <= 0)
    {
        // A zero-length render still consumes the queue. Leaving hits behind
        // would fire them at the top of some later block, which is a timing
        // bug that only appears when a host hands over an empty buffer.
        numPending = 0;
        return;
    }

    const auto masterLevel = juce::jlimit(0.0f, 1.0f, params.masterLevel.load());
    const auto lastSample = numSamples - 1;
    const auto numChannels = juce::jmin(2, buffer.getNumChannels());

    if (numChannels <= 0)
    {
        numPending = 0;
        return;
    }

    // Views onto the two scratch buffers with the OUTPUT's channel count, not
    // their own.
    //
    // They are always stereo so that a host switching layout never makes this
    // allocate, but a Voice decides between its constant-power mono path and
    // its pan from the channel count of the buffer it is handed - so handing
    // it the stereo buffer under a mono output would pan a voice into a
    // channel the mix never reads, and a hard-right hat would vanish. The
    // reverb makes the same decision, between processStereo and processMono.
    //
    // Non-owning: an AudioBuffer built over existing pointers allocates
    // nothing, which is what makes this safe on the audio thread.
    float* scratchChannels[2] = { voiceScratch.getWritePointer(0), voiceScratch.getWritePointer(1) };
    juce::AudioBuffer<float> scratchView{scratchChannels, numChannels, blockCapacity};

    float* wetChannels[2] = { fxScratch.getWritePointer(0), fxScratch.getWritePointer(1) };
    juce::AudioBuffer<float> wetView{wetChannels, numChannels, blockCapacity};

    int done = 0;
    int next = 0;

    while (done < numSamples)
    {
        // Everything due at this sample, started before anything is rendered,
        // so two hits on the same instant both sound from it.
        while (next < numPending && juce::jmin(pending[(size_t) next].offset, lastSample) <= done)
        {
            startHit(pending[(size_t) next], params);
            ++next;
        }

        auto until = numSamples;
        if (next < numPending)
            until = juce::jmin(numSamples, juce::jmin(pending[(size_t) next].offset, lastSample));

        // Capped at what the scratch buffer holds. A longer run is split into
        // several, which the segment loop was already built to do at every
        // hit - so this adds a reason to split, not a new kind of splitting.
        until = juce::jmin(until, done + blockCapacity);

        const auto segment = until - done;

        // Cannot happen: every hit at or before `done` was consumed above, so
        // the next one is strictly later. Guarded anyway, because the failure
        // mode of being wrong about that is a hang on the audio thread.
        if (segment <= 0)
            break;

        for (int v = 0; v < numVoices; ++v)
        {
            const bool active = voices[(size_t) v].isActive();

            // A silent voice whose chain is still ringing has to keep going:
            // the drum has finished but its reverb has not, and the tail is
            // as much a part of the sound as the hit was.
            if (!active && chains[(size_t) v].isIdle())
                continue;

            const auto& voiceParams = params.voices[(size_t) v];

            // Through the scratch rather than straight into the mix, so the
            // chain has the voice's own signal to work on. Post-fader: Level
            // and Pan are already on it, which is what makes a fade take its
            // reverb down with it.
            scratchView.clear(0, segment);

            if (active)
                voices[(size_t) v].renderAdding(scratchView, 0, segment, voiceParams, masterLevel);

            chains[(size_t) v].process(scratchView, wetView, segment, voiceParams, params.fx, active);

            // Metered here, between the chain and the mix, because this is the
            // one place the voice's finished contribution exists on its own.
            // A block split into several segments takes the loudest of them.
            auto peak = 0.0f;

            for (int channel = 0; channel < numChannels; ++channel)
                peak = juce::jmax(peak, scratchView.getMagnitude(channel, 0, segment));

            auto& meter = voicePeaks[(size_t) v];

            if (peak > meter.load(std::memory_order_relaxed))
                meter.store(peak, std::memory_order_relaxed);

            for (int channel = 0; channel < numChannels; ++channel)
                buffer.addFrom(channel, startSample + done, scratchView, channel, 0, segment);
        }

        done = until;
    }

    numPending = 0;
}

void Kit::renderAdding(juce::AudioBuffer<float>& buffer, const KitParameters& params)
{
    renderAdding(buffer, 0, buffer.getNumSamples(), params);
}

bool Kit::isVoiceActive(int voice) const
{
    if (voice < 0 || voice >= numVoices)
        return false;

    return voices[(size_t) voice].isActive();
}

int Kit::getNumActiveVoices() const
{
    int count = 0;

    for (const auto& voice : voices)
        if (voice.isActive())
            ++count;

    return count;
}

int Kit::takeDroppedHits()
{
    return droppedHits.exchange(0);
}

float Kit::takeVoicePeak(int voice)
{
    if (voice < 0 || voice >= numVoices)
        return 0.0f;

    return voicePeaks[(size_t) voice].exchange(0.0f, std::memory_order_relaxed);
}

double longestTailSeconds(const KitParameters& params, const SampleBank* bank)
{
    // The longest of the twelve, not the sum and not the crash's. Voices ring
    // in parallel, so what a host has to wait for after the last hit is
    // whichever single voice takes longest to finish - and which one that is
    // depends on where the Decay dials are, not on the table.
    double longest = 0.0;

    for (int voice = 0; voice < numVoices; ++voice)
    {
        const auto& voiceParams = params.voices[(size_t) voice];
        const auto decay = voiceParams.decay.load();
        const auto attack = voiceParams.attack.load();

        if (bank != nullptr && bank->has(voice))
        {
            // The same arithmetic Voice::trigger does, and it has to be: a
            // host told the wrong number here either clips a tail or pads
            // every bounce with silence.
            const auto& loaded = *bank->slots[(size_t) voice];
            const auto ratio = (double) tuneRatioFor(voiceParams.tune.load());
            const auto playable = ratio > 0.0 ? loaded.seconds() / ratio : 0.0;

            longest = juce::jmax(longest, (double) sampleAttackSeconds(loaded, attack, decay)
                                              + juce::jmin((double) sampleAudibleSeconds(loaded, decay),
                                                           playable));
            continue;
        }

        // A blank slot with no sample on it cannot ring, so it contributes
        // nothing to what a host has to wait for. Taking the kick's length for
        // it - which is what engineSpec would hand back - would pad every
        // bounce on a kit with an empty twelfth slot.
        const auto engine = voiceParams.engine.load();

        if (engine == noEngine)
            continue;

        longest = juce::jmax(longest, (double) hitLengthSeconds(engineSpec(engine), decay, attack));
    }

    // A chain adds its own tail on top, because a reverb starts ringing when
    // the hit ARRIVES and goes on after it has finished. Added rather than
    // maxed for exactly that reason: they are consecutive, not parallel.
    //
    // Only when something is actually running through one - and a slot
    // pointed at a unit with an Amount of zero is not, which is the whole
    // reason anyVoiceUsesFx asks about both. Six seconds on the end of every
    // bounce of a kit with the effects turned down would be the same tax the
    // voice tail is already careful not to charge.
    if (anyVoiceUsesFx(params))
        longest += fxTailSeconds;

    return longest;
}
} // namespace drums
