// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SynthParameters.h"
#include "ParameterRegistry.h"
#include <functional>

namespace wavelathe
{
// Told when the host writes, so the owner can do something about it. A plain
// callback rather than a reference back to the processor: the class needs one
// fact from its owner, and asking for the whole owner to get it would make this
// impossible to test without building a synth first.
using HostWriteCallback = std::function<void(int registryId)>;

// The host's view of one registry parameter.
//
// WaveLathe keeps its parameters as plain atomics in SynthParameters, with
// ParameterRegistry mapping an id onto each one. That arrangement predates
// there being a host at all - it exists because the sequencer and MIDI Learn
// both have to address a dial from the audio thread - and it is exactly the
// manifest a host wants, so this is a thin WINDOW onto it rather than a second
// copy. getValue reads the atomic the engine is already reading and setValue
// writes the one it is already writing: nothing is mirrored, so nothing can
// drift out of step.
//
// Named rather than numbered for the host's sake. VST3 identifies a parameter
// by hashing this string, so a saved Live set keeps pointing at "Cutoff" even
// if the registry is ever reordered - which it is not allowed to be, but two
// guarantees are cheaper than finding out the hard way that one of them was
// wrong.
class HostParameter : public juce::AudioProcessorParameterWithID
{
public:
    HostParameter(int registryId, SynthParameters& parameters, HostWriteCallback onHostWrite);

    // ---- Called by the host ------------------------------------------------
    float getValue() const override;
    void setValue(float newValue) override;
    float getDefaultValue() const override;
    int getNumSteps() const override;
    bool isDiscrete() const override;
    juce::String getText(float normalised, int maximumLength) const override;
    float getValueForText(const juce::String& text) const override;

    // ---- Called by us ------------------------------------------------------
    // Tells the host this parameter moved, WITHOUT writing the value back.
    //
    // That distinction is the whole reason this is not setValueNotifyingHost. A
    // dial has already written its real value through its OWN range, which is
    // skewed for the finger and differs from the registry's log-or-linear one.
    // Going out through normalised and back in would land on a slightly
    // different number than the one just set, and the dial would creep every
    // time it was touched.
    void reportToHost();

    int getRegistryId() const { return id; }

private:
    int id;
    SynthParameters& params;
    HostWriteCallback hostWrote;
    float defaultNormalised = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostParameter)
};
} // namespace wavelathe
