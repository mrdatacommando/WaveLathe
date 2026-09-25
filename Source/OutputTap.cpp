// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "OutputTap.h"
#include <cmath>

namespace wavelathe
{
void OutputTap::prepare(double sampleRateToUse)
{
    sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 44100.0;

    left.fill(0.0f);
    right.fill(0.0f);

    writePosition.store(0);
    written.store(0);
    peakLeft.store(0.0f);
    peakRight.store(0.0f);
    clipped.store(false);
}

void OutputTap::push(const juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    const float* sourceLeft = buffer.getReadPointer(0);

    // Mono in is written to both sides rather than left only, so a view can
    // always read two channels without asking how many there really are.
    const float* sourceRight = numChannels > 1 ? buffer.getReadPointer(1) : sourceLeft;

    int position = writePosition.load(std::memory_order_relaxed);

    float blockPeakLeft = 0.0f;
    float blockPeakRight = 0.0f;
    bool blockClipped = false;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = sourceLeft[i];
        const float r = sourceRight[i];

        left[(size_t) position] = l;
        right[(size_t) position] = r;
        position = (position + 1) & mask;

        const float magnitudeLeft = std::abs(l);
        const float magnitudeRight = std::abs(r);

        blockPeakLeft = juce::jmax(blockPeakLeft, magnitudeLeft);
        blockPeakRight = juce::jmax(blockPeakRight, magnitudeRight);

        if (magnitudeLeft >= 1.0f || magnitudeRight >= 1.0f)
            blockClipped = true;
    }

    // Published last, and with release, so a reader that sees this position
    // also sees every sample written before it.
    writePosition.store(position, std::memory_order_release);
    written.fetch_add(numSamples, std::memory_order_release);

    // Peaks accumulate rather than overwrite: two blocks can pass between one
    // screen frame and the next, and the quieter of them must not erase the
    // louder. Worst case a reader zeroing at this instant loses one block,
    // which is a meter reading a twentieth of a decibel low for a frame.
    if (blockPeakLeft > peakLeft.load(std::memory_order_relaxed))
        peakLeft.store(blockPeakLeft, std::memory_order_relaxed);

    if (blockPeakRight > peakRight.load(std::memory_order_relaxed))
        peakRight.store(blockPeakRight, std::memory_order_relaxed);

    if (blockClipped)
        clipped.store(true, std::memory_order_relaxed);
}

int OutputTap::readLatest(float* destinationLeft, float* destinationRight, int numSamples) const
{
    if (destinationLeft == nullptr || destinationRight == nullptr || numSamples <= 0)
        return 0;

    numSamples = juce::jmin(numSamples, capacity);

    const int64_t total = written.load(std::memory_order_acquire);
    if (total <= 0)
        return 0;

    numSamples = (int) juce::jmin((int64_t) numSamples, total);

    const int end = writePosition.load(std::memory_order_acquire);
    int position = (end - numSamples) & mask;

    for (int i = 0; i < numSamples; ++i)
    {
        destinationLeft[i] = left[(size_t) position];
        destinationRight[i] = right[(size_t) position];
        position = (position + 1) & mask;
    }

    return numSamples;
}

float OutputTap::takePeak(int channel)
{
    auto& slot = channel <= 0 ? peakLeft : peakRight;
    return slot.exchange(0.0f, std::memory_order_relaxed);
}

bool OutputTap::takeClipped()
{
    return clipped.exchange(false, std::memory_order_relaxed);
}

float OutputTap::getRms(int channel, int numSamples) const
{
    if (numSamples <= 0)
        return 0.0f;

    numSamples = juce::jmin(numSamples, capacity);

    const int64_t total = written.load(std::memory_order_acquire);
    if (total <= 0)
        return 0.0f;

    numSamples = (int) juce::jmin((int64_t) numSamples, total);

    const auto& samples = channel <= 0 ? left : right;
    const int end = writePosition.load(std::memory_order_acquire);
    int position = (end - numSamples) & mask;

    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const double value = samples[(size_t) position];
        sum += value * value;
        position = (position + 1) & mask;
    }

    return (float) std::sqrt(sum / (double) numSamples);
}
} // namespace wavelathe
