// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "HarmonyProcessor.h"
#include <cmath>

namespace wavelathe
{
void HarmonyProcessor::reset()
{
    for (auto& chord : sounding)
        chord.count = 0;
}

void HarmonyProcessor::releaseChord(juce::MidiBuffer& destination, int playedNote, int samplePosition)
{
    auto& chord = sounding[playedNote];

    for (int i = 0; i < chord.count; ++i)
        destination.addEvent(juce::MidiMessage::noteOff(1, chord.notes[i]), samplePosition);

    chord.count = 0;
}

void HarmonyProcessor::process(juce::MidiBuffer& midi, const SynthParameters& params)
{
    int key = juce::jlimit(0, music::numKeys - 1, (int) std::round(params.musicalKey.load()));
    auto scale = (music::Scale) juce::jlimit(0, (int) music::Scale::numScales - 1,
                                              (int) std::round(params.musicalScale.load()));
    auto chordMode = (music::ChordMode) juce::jlimit(0, (int) music::ChordMode::numModes - 1,
                                                      (int) std::round(params.chordMode.load()));

    // Nothing to do at all in the default state, so the common case costs one
    // comparison rather than a rebuild of the buffer.
    bool anythingHeld = false;
    for (const auto& chord : sounding)
        if (chord.count > 0)
        {
            anythingHeld = true;
            break;
        }

    if (scale == music::Scale::chromatic && chordMode == music::ChordMode::off && !anythingHeld)
        return;

    juce::MidiBuffer rewritten;

    for (const auto metadata : midi)
    {
        auto message = metadata.getMessage();
        int position = metadata.samplePosition;

        if (message.isNoteOn())
        {
            int played = juce::jlimit(0, 127, message.getNoteNumber());

            // Retriggering a key that is already down releases what it started
            // before starting again, so nothing is orphaned.
            if (sounding[played].count > 0)
                releaseChord(rewritten, played, position);

            auto notes = music::buildChord(played, key, scale, chordMode);

            auto& chord = sounding[played];
            chord.count = 0;

            for (int note : notes)
            {
                if (chord.count >= maxChordNotes)
                    break;

                rewritten.addEvent(juce::MidiMessage::noteOn(1, note, message.getFloatVelocity()), position);
                chord.notes[chord.count++] = note;
            }
        }
        else if (message.isNoteOff())
        {
            int played = juce::jlimit(0, 127, message.getNoteNumber());

            if (sounding[played].count > 0)
                releaseChord(rewritten, played, position);
            else
                rewritten.addEvent(message, position); // never swallow a note-off we did not cause
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            reset();
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
