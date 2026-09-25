// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <juce_audio_basics/juce_audio_basics.h>
#include "OutputTap.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace wavelathe;

namespace
{
int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition)
        ++failures;
}

bool nearly(float a, float b, float tolerance = 0.001f) { return std::abs(a - b) < tolerance; }

// A block of audio to push, left and right filled by whatever is handed in.
juce::AudioBuffer<float> makeBlock(int numSamples, std::function<float(int)> shapeLeft,
                                   std::function<float(int)> shapeRight = {})
{
    juce::AudioBuffer<float> buffer(2, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        buffer.setSample(0, i, shapeLeft(i));
        buffer.setSample(1, i, shapeRight ? shapeRight(i) : shapeLeft(i));
    }

    return buffer;
}
} // namespace

int main()
{
    std::printf("Ring capacity: %d frames\n", OutputTap::capacity);

    // ---- Nothing pushed ----------------------------------------------------
    std::printf("\nBefore anything plays:\n");
    {
        OutputTap tap;
        tap.prepare(48000.0);

        std::vector<float> left(64), right(64);
        check(tap.readLatest(left.data(), right.data(), 64) == 0, "there is nothing to read");
        check(!tap.hasSignal(), "and the tap says so");
        check(nearly(tap.takePeak(0), 0.0f), "with no peak");
        check(!tap.takeClipped(), "and nothing clipped");
    }

    // ---- What goes in comes out --------------------------------------------
    std::printf("\nReading back what was pushed:\n");
    {
        OutputTap tap;
        tap.prepare(48000.0);

        // A ramp, so every sample is distinguishable from every other and an
        // off-by-one in the ring shows up as a wrong value rather than as a
        // plausible one.
        tap.push(makeBlock(100, [](int i) { return (float) i / 100.0f; }));

        std::vector<float> left(100), right(100);
        check(tap.readLatest(left.data(), right.data(), 100) == 100, "all of it reads back");
        check(nearly(left[0], 0.0f), "starting at the oldest sample");
        check(nearly(left[99], 0.99f), "and ending at the newest");
        check(tap.hasSignal(), "and the tap reports signal");

        // Asking for more than has ever played gives what there is, not silence
        // padded onto the front.
        std::vector<float> big(500), bigRight(500);
        check(tap.readLatest(big.data(), bigRight.data(), 500) == 100,
              "asking for more than has played gives only what played");
    }

    // ---- The newest samples win --------------------------------------------
    // A scope wants the last moment, not the first. Reading the oldest samples
    // in the ring would be a display permanently a third of a second behind.
    std::printf("\nAlways the most recent:\n");
    {
        OutputTap tap;
        tap.prepare(48000.0);

        tap.push(makeBlock(64, [](int) { return 0.1f; }));
        tap.push(makeBlock(64, [](int) { return 0.2f; }));

        std::vector<float> left(64), right(64);
        tap.readLatest(left.data(), right.data(), 64);

        check(nearly(left[0], 0.2f) && nearly(left[63], 0.2f), "the latest block is what reads back");
    }

    // ---- Wrapping ----------------------------------------------------------
    // The one place a ring buffer goes wrong. Pushing past the end must join up
    // rather than start again.
    std::printf("\nWrapping past the end:\n");
    {
        OutputTap tap;
        tap.prepare(48000.0);

        // Fill it almost exactly, then push a marked block that must straddle
        // the join.
        tap.push(makeBlock(OutputTap::capacity - 32, [](int) { return 0.0f; }));
        tap.push(makeBlock(64, [](int i) { return 1.0f + (float) i; }));

        std::vector<float> left(64), right(64);
        check(tap.readLatest(left.data(), right.data(), 64) == 64, "a straddling block reads back");

        bool inOrder = true;
        for (int i = 0; i < 64; ++i)
            if (!nearly(left[(size_t) i], 1.0f + (float) i, 0.01f))
                inOrder = false;

        check(inOrder, "in the order it was written, across the join");
    }

    // ---- Peaks -------------------------------------------------------------
    std::printf("\nPeaks:\n");
    {
        OutputTap tap;
        tap.prepare(48000.0);

        tap.push(makeBlock(64, [](int i) { return i == 30 ? -0.75f : 0.1f; },
                           [](int) { return 0.25f; }));

        check(nearly(tap.takePeak(0), 0.75f), "a peak is the loudest sample, sign ignored");
        check(nearly(tap.takePeak(1), 0.25f), "and each channel keeps its own");
        check(nearly(tap.takePeak(0), 0.0f), "reading a peak clears it");

        // Two blocks between one screen frame and the next: the quieter must
        // not erase the louder, or a meter misses transients at small buffers.
        tap.push(makeBlock(64, [](int) { return 0.9f; }));
        tap.push(makeBlock(64, [](int) { return 0.1f; }));
        check(nearly(tap.takePeak(0), 0.9f), "and a quiet block does not erase a loud one before it");
    }

    // ---- Clipping ----------------------------------------------------------
    std::printf("\nClipping:\n");
    {
        OutputTap tap;
        tap.prepare(48000.0);

        tap.push(makeBlock(64, [](int) { return 0.99f; }));
        check(!tap.takeClipped(), "just under full scale is not clipping");

        tap.push(makeBlock(64, [](int i) { return i == 5 ? 1.0f : 0.1f; }));
        check(tap.takeClipped(), "exactly full scale is");
        check(!tap.takeClipped(), "and reading it clears it");

        // Negative full scale counts too. A waveform squared off at the bottom
        // is as clipped as one squared off at the top.
        tap.push(makeBlock(64, [](int i) { return i == 5 ? -1.4f : 0.1f; }));
        check(tap.takeClipped(), "and so does going under negative full scale");

        // The right channel alone is still clipping, even with a silent left.
        tap.push(makeBlock(64, [](int) { return 0.0f; }, [](int) { return 1.2f; }));
        check(tap.takeClipped(), "on either channel");
    }

    // ---- RMS ---------------------------------------------------------------
    std::printf("\nLoudness:\n");
    {
        OutputTap tap;
        tap.prepare(48000.0);

        tap.push(makeBlock(1000, [](int) { return 0.5f; }));
        check(nearly(tap.getRms(0, 1000), 0.5f), "a steady level reads as itself");

        OutputTap sine;
        sine.prepare(48000.0);

        // A sine peaking at 1.0 has an RMS of 1/sqrt(2). A meter that reported
        // its peak instead would call every sine 3 dB louder than it is.
        sine.push(makeBlock(2048, [](int i)
                            { return std::sin(2.0f * 3.14159265f * (float) i / 64.0f); }));
        check(nearly(sine.getRms(0, 2048), 0.7071f, 0.01f), "and a sine reads 3 dB below its peak");

        check(nearly(sine.getRms(0, 0), 0.0f), "asking for no samples is not a divide by zero");
    }

    // ---- Mono in -----------------------------------------------------------
    // The plugin allows a mono bus, and a view should not have to ask.
    std::printf("\nA mono block:\n");
    {
        OutputTap tap;
        tap.prepare(48000.0);

        juce::AudioBuffer<float> mono(1, 64);
        for (int i = 0; i < 64; ++i)
            mono.setSample(0, i, 0.4f);

        tap.push(mono);

        std::vector<float> left(64), right(64);
        tap.readLatest(left.data(), right.data(), 64);

        check(nearly(left[0], 0.4f) && nearly(right[0], 0.4f), "is written to both sides");
        check(nearly(tap.takePeak(1), 0.4f), "so the right meter is not stuck at silence");
    }

    // ---- Preparing again ---------------------------------------------------
    // A sample-rate change must not leave the previous session's audio in the
    // ring for the scope to draw as if it were current.
    std::printf("\nPreparing again:\n");
    {
        OutputTap tap;
        tap.prepare(48000.0);
        tap.push(makeBlock(64, [](int) { return 0.8f; }));

        tap.prepare(96000.0);

        std::vector<float> left(64), right(64);
        check(tap.readLatest(left.data(), right.data(), 64) == 0, "empties the ring");
        check(!tap.hasSignal(), "and reports no signal");
        check(nearly(tap.takePeak(0), 0.0f), "with the peak cleared");
        check(!tap.takeClipped(), "and the clip flag cleared");
    }

    std::printf("\n%s\n", failures == 0 ? "ALL OUTPUT TAP TESTS PASSED" : "SOME OUTPUT TAP TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
