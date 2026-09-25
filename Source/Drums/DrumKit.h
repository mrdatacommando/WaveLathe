// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "DrumFx.h"
#include "DrumVoice.h"

#include <array>
#include <atomic>

namespace drums
{
// Sixteen voices, the choke between two of them, and a queue of hits that have
// not happened yet.
//
// The queue is the part that matters. A drum machine that could only start a
// hit on a block boundary would quantise every groove to the host's buffer
// size - and this project has just spent a phase giving each hit a signed
// nudge measured in ticks, precisely so that hits DO NOT all land on the beat.
// Rounding that away at the last moment would undo the whole feature, silently,
// and only at some buffer sizes.
//
// So a hit carries the sample within the block at which it starts, and the
// renderer splits the block at every one of them.
class Kit
{
public:
    // How many hits one block may carry. Sixteen voices could each be hit on
    // every one of a block's cells, and a very long block at a very fast tempo
    // could in principle hold several cells; 128 is past anything real and
    // still small enough to live in the object rather than in an allocation
    // the audio thread would have to make.
    static constexpr int maxPendingHits = 128;

    // Spelled out because the non-copyable macro at the bottom declares a
    // constructor, and a class with any user-declared constructor does not get
    // an implicit default one.
    //
    // It now also sizes the working buffers, so a Kit that is rendered before
    // anything prepared it is quiet rather than reading off the end of a
    // zero-length scratch. Everything real does call prepare; a default that
    // only works once somebody remembers to is not a default.
    Kit();

    // maximumBlockSize sizes the scratch buffer every voice is rendered
    // through on its way to the mix and the sends. A longer run than this is
    // split, which costs nothing and changes nothing: the renderer already
    // splits at every hit, and the test that a chopped-up render matches a
    // long one sample for sample is what says so.
    //
    // Defaulted so the tests and the offline renderer can go on saying what
    // they mean - prepare(rate) - while the plugin passes what its host asked
    // for.
    void prepare(double newSampleRate, int maximumBlockSize = 1024);
    void reset();

    // Queues a hit. `sampleOffset` is where in the NEXT rendered block it
    // starts, which is how a nudged hit keeps its nudge.
    //
    // An offset past the end of the block is clamped to its last sample rather
    // than dropped: an offset out of range is a caller's arithmetic error, and
    // a hit that arrives a block late is a smaller fault than a hit that never
    // arrives at all - and much easier to hear, which is how it gets fixed.
    void trigger(int voice, float velocity, bool accent, int sampleOffset = 0);

    // ADDS into the buffer, so the kit can sit alongside the synth rather than
    // owning the output. Any length is accepted.
    void renderAdding(juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                      const KitParameters& params);
    void renderAdding(juce::AudioBuffer<float>& buffer, const KitParameters& params);

    // Which voices are playing a recording instead of their own circuit.
    //
    // One pointer, swapped whole, so a render can never see half a bank - the
    // same arrangement the synth's wavetables use and for the same reason.
    // The bank and everything in it must outlive every render that could
    // still be holding it; whatever loads samples owns that, because it is
    // the only thing that knows when a render has finished.
    //
    // Safe to call from any thread.
    void setSampleBank(const SampleBank* bank) { sampleBank.store(bank, std::memory_order_release); }
    const SampleBank* getSampleBank() const { return sampleBank.load(std::memory_order_acquire); }

    bool isVoiceActive(int voice) const;
    int getNumActiveVoices() const;

    // Hits that did not fit in the queue, taken and cleared. A number that is
    // ever non-zero means a block asked for more than maxPendingHits, and the
    // pattern that did it would otherwise be missing notes with nothing to say
    // so.
    int takeDroppedHits();

    // The loudest this voice has been since the meter last looked, and cleared
    // by the looking. Measured on the voice's own contribution AFTER its two
    // sends, which is what it actually adds to the mix - a dry hit at a
    // sensible level driven into the distortion unit clips there, not in the
    // voice, and a meter reading pre-send would call that fine.
    //
    // Peak rather than RMS because the question an indicator answers is "did
    // that hit, and did it go over", both of which are about the highest
    // sample rather than the average energy.
    float takeVoicePeak(int voice);

private:
    struct PendingHit
    {
        int voice = 0;
        float velocity = 1.0f;
        bool accent = false;
        int offset = 0;
    };

    void startHit(const PendingHit& hit, const KitParameters& params);
    void allocateWorkingBuffers();

    std::array<Voice, numVoices> voices;

    // Kept sorted by offset as they are inserted. An insertion sort over a
    // handful of nearly-ordered items, rather than a sort at render time: the
    // renderer walks the queue once and must be able to trust the order, and
    // this way it costs nothing to give it.
    std::array<PendingHit, maxPendingHits> pending;
    int numPending = 0;

    // One voice's output on its way through its chain and into the mix.
    //
    // Rendering into the output directly, as this did before there were
    // effects, is no longer possible: a chain has to be handed the voice's own
    // signal, and once it has been summed into the mix there is nothing left
    // to take. The cost is one extra pass over each sounding voice's samples.
    juce::AudioBuffer<float> voiceScratch;

    // What a unit produced, before the slot's Amount mixes it back over the
    // voice. One buffer for all sixteen chains, because only one is ever
    // mid-process.
    juce::AudioBuffer<float> fxScratch;

    // A chain per voice, because the two slots are in series and slot 2 needs
    // slot 1's output FOR THIS VOICE - which a bus carrying all sixteen summed
    // together cannot give it. See the note on VoiceFx.
    std::array<VoiceFx, numVoices> chains;

    std::atomic<const SampleBank*> sampleBank{nullptr};

    std::atomic<int> droppedHits{0};

    // Written by the render, cleared by whoever reads it. Relaxed and without
    // a compare-exchange: the worst a race can do is lose one peak to a read
    // that lands between the load and the store, which costs an indicator one
    // frame of brightness. A CAS loop on the audio thread to protect a lamp
    // would be the wrong trade.
    std::array<std::atomic<float>, numVoices> voicePeaks{};

    double sampleRate = 44100.0;
    int blockCapacity = 1024;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Kit)
};

// The longest a hit could still be sounding after the last one was struck,
// given where the Decay dials are now.
//
// A host has to be told this or it truncates a bounce at the last note, and
// the crash that was still ringing gets cut off mid-tail. It depends on the
// dials rather than being a constant because a crash at the bottom of its
// Decay range rings for under a second and at the top for half a minute, and
// reporting the worst case always would put twenty-odd seconds of silence on
// the end of every render.
// `bank` may be null, which means every voice is synthesised. When it is not,
// a slot holding a ten-second recording has a ten-second tail and the table
// row it replaced has nothing to say about it.
double longestTailSeconds(const KitParameters& params, const SampleBank* bank = nullptr);
} // namespace drums
