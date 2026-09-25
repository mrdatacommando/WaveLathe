// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MonoVoice.h"
#include <algorithm>

namespace wavelathe
{
void MonoVoice::reset()
{
    heldNotes.clear();
    soundingNote = -1;
    wasEnabled = false;
}

void MonoVoice::releaseHeld(int note)
{
    heldNotes.erase(std::remove(heldNotes.begin(), heldNotes.end(), note), heldNotes.end());
}

void MonoVoice::process(juce::MidiBuffer& midi, SynthParameters& params)
{
    bool enabled = params.monoMode.load() > 0.5f;
    bool legato = params.legatoMode.load() > 0.5f;

    if (!enabled)
    {
        if (wasEnabled)
        {
            // Switching back to polyphony: whatever the mono line was holding
            // is let go, or it would hang with nothing left to release it.
            juce::MidiBuffer cleanup;
            if (soundingNote >= 0)
                cleanup.addEvent(juce::MidiMessage::noteOff(1, soundingNote), 0);

            for (const auto metadata : midi)
                cleanup.addEvent(metadata.getMessage(), metadata.samplePosition);

            midi.swapWith(cleanup);
            reset();
        }

        params.monoTargetNote.store(-1.0f);
        return;
    }

    wasEnabled = true;

    juce::MidiBuffer generated;

    for (const auto metadata : midi)
    {
        auto message = metadata.getMessage();
        int sample = metadata.samplePosition;

        if (message.isNoteOn())
        {
            int note = message.getNoteNumber();
            releaseHeld(note);
            heldNotes.push_back(note);

            if (soundingNote < 0)
            {
                // Nothing sounding, so this starts the line however it is set.
                generated.addEvent(juce::MidiMessage::noteOn(1, note, message.getVelocity()), sample);
                soundingNote = note;
            }
            else if (legato)
            {
                // The sounding note changes pitch instead of a new one
                // starting. Nothing is emitted: the voices read the target.
            }
            else
            {
                generated.addEvent(juce::MidiMessage::noteOff(1, soundingNote), sample);
                generated.addEvent(juce::MidiMessage::noteOn(1, note, message.getVelocity()), sample);
                soundingNote = note;
            }
        }
        else if (message.isNoteOff())
        {
            int note = message.getNoteNumber();
            releaseHeld(note);

            if (heldNotes.empty())
            {
                if (soundingNote >= 0)
                {
                    generated.addEvent(juce::MidiMessage::noteOff(1, soundingNote), sample);
                    soundingNote = -1;
                }
            }
            else if (!legato && note == soundingNote)
            {
                // Back to the key still under your finger.
                int back = heldNotes.back();
                generated.addEvent(juce::MidiMessage::noteOff(1, soundingNote), sample);
                generated.addEvent(juce::MidiMessage::noteOn(1, back, 0.8f), sample);
                soundingNote = back;
            }
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            heldNotes.clear();
            soundingNote = -1;
            generated.addEvent(message, sample);
        }
        else
        {
            generated.addEvent(message, sample);
        }
    }

    // Where the line is reaching for: the newest key held, which in legato is
    // what the sounding voice slides to.
    params.monoTargetNote.store(heldNotes.empty() ? (float) soundingNote : (float) heldNotes.back());

    midi.swapWith(generated);
}
} // namespace wavelathe
