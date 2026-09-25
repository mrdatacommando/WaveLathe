// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SynthParameters.h"
#include <vector>

namespace wavelathe
{
// Mono and legato, done where the notes are rather than where the voices are.
//
// Mono means one note sounds at a time and the newest key wins, with the older
// keys remembered: let the new one go while an older one is still held and the
// line falls back to it rather than stopping. That returning behaviour is most
// of what makes a mono synth playable.
//
// Legato is the difference between a line that joins up and one that stutters.
// With it on, a note played while another is held never becomes a new note-on
// at all - the sounding note simply changes pitch, so the envelope carries on
// and the glide slides between them. That is why the target is published for
// the voices to read instead of being sent as MIDI: a note-on would restart
// the envelope no matter how it was arranged.
class MonoVoice
{
public:
    void reset();

    // Rewrites the buffer so that at most one note is ever sounding.
    void process(juce::MidiBuffer& midi, SynthParameters& params);

private:
    void releaseHeld(int note);

    std::vector<int> heldNotes; // in the order they were pressed, newest last
    int soundingNote = -1;      // the note the voices actually have down
    bool wasEnabled = false;
};
} // namespace wavelathe
