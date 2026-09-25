// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

namespace wavelathe
{
// A copy of what actually leaves the plugin, for anything that wants to look at
// it. Written by the audio thread at the very end of the block - after the
// master effects and the output gain - so what it holds is the signal as the
// interface will receive it, not an earlier and more flattering version of it.
//
// Lock free by construction rather than by agreement: one writer, one reader,
// a power-of-two ring, and a single published write position. The reader can be
// overtaken mid-read if it dawdles - at 48 kHz the writer needs about a third
// of a second to lap this buffer, and a reader running at screen rate is three
// hundred times faster than that. A torn frame of a scope is a smear nobody
// sees; a lock on the audio thread is a click everybody hears.
class OutputTap
{
public:
    // 16384 frames: about a third of a second at 48 kHz, which is more history
    // than any view here asks for and enough headroom that the reader is never
    // racing the writer.
    static constexpr int capacity = 1 << 14;
    static constexpr int mask = capacity - 1;

    void prepare(double sampleRateToUse);

    // ---- Audio thread ------------------------------------------------------
    // Mono input is written to both sides, so everything downstream can assume
    // two channels rather than each view having to ask.
    void push(const juce::AudioBuffer<float>& buffer);

    // ---- Message thread ----------------------------------------------------
    // The most recent `numSamples` frames, oldest first. Returns how many were
    // actually written, which is 0 before anything has played.
    int readLatest(float* destinationLeft, float* destinationRight, int numSamples) const;

    // Loudest sample since the last call, and zeroed by it, so a meter reading
    // at screen rate sees every peak between frames rather than only the ones
    // that happened to land on a frame boundary.
    float takePeak(int channel);

    // True if anything reached full scale since the last call. In a float
    // plugin nothing is actually clipped here - it is clipped later, by the
    // converter - so this means "over 0 dBFS", which is the thing worth
    // knowing while there is still something you can do about it.
    bool takeClipped();

    // Loudness over the last `numSamples` frames rather than the loudest single
    // sample, because peak says nothing about how loud a patch actually feels
    // beside another one.
    float getRms(int channel, int numSamples) const;

    bool hasSignal() const { return written.load() > 0; }

    // What the engine is actually running at. Asked for by anything drawing a
    // filter response over this signal: a curve built at an assumed rate is a
    // curve of a filter nobody is using.
    double getSampleRate() const { return sampleRate; }

private:
    std::array<float, capacity> left{};
    std::array<float, capacity> right{};

    std::atomic<int> writePosition{0};
    std::atomic<int64_t> written{0};

    std::atomic<float> peakLeft{0.0f};
    std::atomic<float> peakRight{0.0f};
    std::atomic<bool> clipped{false};

    double sampleRate = 44100.0;

    // No JUCE_DECLARE_NON_COPYABLE here, unlike the components: the macro
    // user-declares a deleted copy constructor, which suppresses the implicit
    // default one, and this is held by value in the processor. The atomics
    // above already make it non-copyable, so the macro would cost a constructor
    // to buy something the class has anyway - which is why the other engine
    // pieces beside it do not carry it either.
};
} // namespace wavelathe
