// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SynthParameters.h"

namespace wavelathe
{
// Hold, as a latch over everything played rather than over the last key only.
// While it is on, releasing a key does not stop its note, so a chord can be
// built up one finger at a time and left ringing while you work on the sound or
// let the arpeggiator run it.
//
// Tapping a note that is already latched lets that one go, which is what makes
// it possible to change the chord without switching hold off and starting over.
class NoteLatch
{
public:
    void reset();
    void process(juce::MidiBuffer& midi, const SynthParameters& params);

private:
    bool latched[128] = {};
    bool wasOn = false;
};
} // namespace wavelathe
