// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "NoteLatch.h"

namespace wavelathe
{
void NoteLatch::reset()
{
    for (auto& note : latched)
        note = false;

    wasOn = false;
}

void NoteLatch::process(juce::MidiBuffer& midi, const SynthParameters& params)
{
    bool on = params.holdNotes.load() > 0.5f;

    // Switching hold off drops everything it was keeping down. Without this the
    // latched notes would have no way left to stop.
    if (!on && wasOn)
    {
        juce::MidiBuffer released;

        for (int note = 0; note < 128; ++note)
            if (latched[note])
            {
                released.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                latched[note] = false;
            }

        for (const auto metadata : midi)
            released.addEvent(metadata.getMessage(), metadata.samplePosition);

        midi.swapWith(released);
    }

    wasOn = on;

    if (!on)
        return;

    juce::MidiBuffer rewritten;

    for (const auto metadata : midi)
    {
        auto message = metadata.getMessage();
        int position = metadata.samplePosition;

        if (message.isNoteOn())
        {
            int note = juce::jlimit(0, 127, message.getNoteNumber());

            if (latched[note])
            {
                // Already held: this press is the one that lets it go.
                rewritten.addEvent(juce::MidiMessage::noteOff(1, note), position);
                latched[note] = false;
            }
            else
            {
                rewritten.addEvent(message, position);
                latched[note] = true;
            }
        }
        else if (message.isNoteOff())
        {
            // Swallowed: the whole point of the latch.
            if (!latched[juce::jlimit(0, 127, message.getNoteNumber())])
                rewritten.addEvent(message, position);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            for (auto& note : latched)
                note = false;

            rewritten.addEvent(message, position);
        }
        else
        {
            rewritten.addEvent(message, position);
        }
    }

    midi.swapWith(rewritten);
}
} // namespace wavelathe
