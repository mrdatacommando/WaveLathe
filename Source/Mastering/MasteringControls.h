// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

namespace mastering
{
// Every control the Master panel offers, named once so that two products can
// agree about them without either one owning the list.
//
// Appended to, not inserted into, for the same reason ParameterRegistry is: the
// effect plugin publishes these to a host by index, and a host that has saved
// automation against index 4 will still be asking for index 4 next year.
enum class Control
{
    enabled,
    satDrive,
    satMix,
    compThreshold,
    compRatio,
    compAttack,
    compRelease,
    compMakeup,
    compMix,
    limitCeiling,
    limitRelease,
    trim,
    count
};

// What the Master panel needs from whatever is hosting it.
//
// Two things implement this and they keep their values in completely different
// places. In the synth they live in SynthParameters, because that is what
// travels with a preset and what a project saves. In the effect plugin they are
// host parameters, automatable and owned by the DAW. The panel should not have
// to know which of those it is driving, and before this existed it did: it took
// a WaveLatheProcessor, which would have dragged the entire synth into an
// effect plugin that wants none of it.
//
// Deliberately not a JUCE class and deliberately not in the DSP library either.
// It is the vocabulary the user interface and the parameters share, and it
// needs to be free of both.
class Controls
{
public:
    virtual ~Controls() = default;

    virtual float get(Control control) const = 0;
    virtual void set(Control control, float value) = 0;

    // The two meters, read on the message thread while the audio thread writes
    // them. Positive decibels, zero meaning nothing is being taken away.
    virtual float compressorReductionDb() const = 0;
    virtual float limiterReductionDb() const = 0;
};
} // namespace mastering
