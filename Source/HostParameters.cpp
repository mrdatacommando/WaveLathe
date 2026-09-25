// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "HostParameters.h"

namespace wavelathe
{
namespace
{
// A number a person can read, at whatever size the parameter happens to be.
// The registry knows each parameter's range but not its units, so this says
// 166.8 rather than 166.8 Hz - which is still worth far more in a host's
// automation lane than the 0.31 it would otherwise show.
juce::String formatReal(float value)
{
    const float magnitude = std::abs(value);

    if (magnitude >= 100.0f)
        return juce::String(juce::roundToInt(value));

    if (magnitude >= 10.0f)
        return juce::String(value, 1);

    return juce::String(value, 2);
}
} // namespace

HostParameter::HostParameter(int registryId, SynthParameters& parameters, HostWriteCallback onHostWrite)
    : juce::AudioProcessorParameterWithID(juce::ParameterID{paramreg::name(registryId), 1},
                                          paramreg::name(registryId)),
      id(registryId),
      params(parameters),
      hostWrote(std::move(onHostWrite))
{
    // Read off a freshly built parameter block rather than written down again
    // here. The defaults live in SynthParameters' own member initialisers, and
    // a second copy of sixty-three of them would be a second copy to keep in
    // step - which it would not be, within a month.
    static const SynthParameters defaults;
    defaultNormalised = paramreg::readNormalised(defaults, id);
}

float HostParameter::getValue() const
{
    return paramreg::readNormalised(params, id);
}

void HostParameter::setValue(float newValue)
{
    paramreg::writeNormalised(params, id, newValue);

    // The host is now the thing moving this dial, so the step sequencer's lane
    // lets go of it for a moment. Not a new rule invented for hosts: the
    // question the sequencer has always asked is "is something other than the
    // lane in charge of this right now", and a DAW automating it is exactly
    // that - the same answer a hand on the dial and a learned controller both
    // give. Without this, a recorded lane and a Live automation lane pointed at
    // one dial would each overwrite the other every block, and the dial would
    // buzz between two values instead of following either.
    if (hostWrote)
        hostWrote(id);
}


int HostParameter::getNumSteps() const
{
    const int positions = paramreg::steps(id);

    // Continuous ones keep whatever the host's own default resolution is;
    // saying "0 steps" would be a claim, not an absence.
    return positions > 0 ? positions : juce::AudioProcessorParameter::getNumSteps();
}

bool HostParameter::isDiscrete() const
{
    return paramreg::steps(id) > 0;
}

float HostParameter::getDefaultValue() const
{
    return defaultNormalised;
}

juce::String HostParameter::getText(float normalised, int maximumLength) const
{
    const auto& d = paramreg::all()[(size_t) id];
    const float clamped = juce::jlimit(0.0f, 1.0f, normalised);

    float value;
    if (d.logarithmic)
    {
        const float low = std::log(juce::jmax(1.0e-6f, d.minValue));
        const float high = std::log(juce::jmax(1.0e-6f, d.maxValue));
        value = std::exp(low + clamped * (high - low));
    }
    else
    {
        value = d.minValue + clamped * (d.maxValue - d.minValue);
    }

    juce::String text;

    if (d.steps > 0)
    {
        // Rounded the way the ENGINE rounds it, so the host never shows a
        // position the synth would not actually play. The label when there is
        // one - "LP 24" tells you something "1.00" does not.
        const int step = juce::jlimit(0, d.steps - 1, (int) std::round(value - d.minValue));

        // stepName rather than label: AudioProcessorParameterWithID already has
        // a member called label - the unit shown after the value - and a local
        // hiding it is a /W4 warning. Two different things with one name in one
        // scope is worth renaming even where the compiler stays quiet.
        if (const char* stepName = paramreg::stepLabel(id, step))
            text = stepName;
        else
            text = juce::String((int) std::round(value));
    }
    else
    {
        text = formatReal(value);
    }

    return maximumLength > 0 ? text.substring(0, maximumLength) : text;
}

float HostParameter::getValueForText(const juce::String& text) const
{
    const auto& d = paramreg::all()[(size_t) id];

    // A named position can be typed by its name. Tried before the number,
    // because "LP 24" read as a number is 0 - which would quietly select the
    // first position rather than the one actually asked for.
    if (d.steps > 0 && d.stepLabels != nullptr)
    {
        const auto wanted = text.trim();

        for (int step = 0; step < d.steps; ++step)
            if (const char* stepName = paramreg::stepLabel(id, step))
                if (wanted.equalsIgnoreCase(stepName))
                    return juce::jlimit(0.0f, 1.0f, (float) step / (d.maxValue - d.minValue));
    }

    const float value = text.getFloatValue();

    if (d.logarithmic)
    {
        const float low = std::log(juce::jmax(1.0e-6f, d.minValue));
        const float high = std::log(juce::jmax(1.0e-6f, d.maxValue));
        const float bounded = juce::jlimit(d.minValue, d.maxValue, juce::jmax(1.0e-6f, value));
        return juce::jlimit(0.0f, 1.0f, (std::log(bounded) - low) / (high - low));
    }

    return juce::jlimit(0.0f, 1.0f, (value - d.minValue) / (d.maxValue - d.minValue));
}

void HostParameter::reportToHost()
{
    sendValueChangedMessageToListeners(getValue());
}
} // namespace wavelathe
