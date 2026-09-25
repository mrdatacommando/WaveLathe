// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <atomic>

// The mastering chain's own parameter block.
//
// Deliberately NOT SynthParameters. This chain is meant to be linked by more
// than one product - the synth's Master tab and a separate effect plugin that
// sits on a mix bus - and a library that reaches into WaveLathe's parameter
// struct could only ever be used by WaveLathe. Everything the chain needs to
// know is here, and each front end copies its own dials into it.
//
// Namespace is "mastering" rather than "wavelathe::mastering" for the same
// reason: the second product that links this will not be WaveLathe, and a name
// that says otherwise would be wrong the day it ships.
namespace mastering
{
// Plain atomics, read by the audio thread and written by whatever is driving
// it, exactly as SynthParameters does it. No locks and no message passing: a
// dial that is mid-move is allowed to be read as either side of the move, and
// the smoothers downstream make the difference inaudible either way.
struct Parameters
{
    // ---- Saturation --------------------------------------------------------
    // 0 is a true bypass of the stage rather than a very small amount of it,
    // which matters because "off" has to be exactly transparent for the chain
    // to be usable as a metering insert.
    std::atomic<float> saturationDrive{0.0f}; // 0..1
    std::atomic<float> saturationMix{1.0f};   // 0..1, dry/wet across the stage

    // ---- Compressor --------------------------------------------------------
    std::atomic<float> compressorThresholdDb{0.0f};   // -48..0
    std::atomic<float> compressorRatio{2.0f};         // 1..20, 1 is off
    std::atomic<float> compressorAttackMs{10.0f};     // 0.1..100
    std::atomic<float> compressorReleaseMs{100.0f};   // 10..1000
    std::atomic<float> compressorMakeupDb{0.0f};      // 0..24
    std::atomic<float> compressorMix{1.0f};           // 0..1, parallel squash

    // ---- Limiter -----------------------------------------------------------
    // The ceiling is where the limiter stops the signal, not where it starts
    // working. Default sits just under 0 dBFS because a file that peaks at
    // exactly 0 clips on some decoders after lossy encoding.
    std::atomic<float> limiterCeilingDb{-0.3f};  // -12..0
    std::atomic<float> limiterReleaseMs{50.0f};  // 1..500

    // ---- Output ------------------------------------------------------------
    std::atomic<float> outputGainDb{0.0f};       // -24..+24
    // Anything other than 0 bypasses the whole chain. A float rather than a
    // bool so a host can automate it like every other parameter.
    std::atomic<float> bypass{0.0f};
};
} // namespace mastering
