// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "SynthParameters.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>

namespace wavelathe
{
// Which hardware control moves which dial. Lives in the processor rather than
// the editor because in a host the plugin window is usually shut, and a MIDI
// assignment that stops working when you close the window is not an assignment.
//
// Keyed by controller number, which is what makes reassignment free: learning
// CC 74 to a second dial is one store into slot 74, and whatever it used to
// point at is simply no longer pointed at. That is also the convention every
// other synth settled on - one controller drives one parameter, though nothing
// stops two controllers driving the same one.
//
// Channel is ignored on purpose. One keyboard on one channel is the normal
// case, and a map that silently did nothing because the controller moved to
// channel 2 would be a worse problem than the one per-channel maps solve.
class MidiMap
{
public:
    static constexpr int numControllers = 128;
    static constexpr int none = -1;

    // 120 to 127 are Channel Mode messages - All Sound Off, Reset All
    // Controllers, All Notes Off and the mode selects - rather than controls.
    // Nothing sends them from a knob, and a HOST sends them routinely.
    static constexpr int firstChannelModeController = 120;

    MidiMap();

    // ---- Audio thread ------------------------------------------------------
    // Binds if a learn is armed, then applies. Returns true if the controller
    // did anything, so the caller can tell a mapped CC from a passing one.
    bool handleController(int controller, int value, SynthParameters& params);

    int parameterFor(int controller) const;

    // ---- Message thread ----------------------------------------------------
    void assign(int controller, int parameterId);
    void clearController(int controller);

    // Every controller pointing at this parameter, since more than one may.
    void clearParameter(int parameterId);
    void clearAll();

    // The first controller driving this parameter, or none. First rather than
    // all because this answers "what does the badge on this dial say".
    int controllerFor(int parameterId) const;
    int countAssignments() const;

    // ---- Learning ----------------------------------------------------------
    // Arms: the next controller to arrive binds to this parameter and nothing
    // is applied until it does. Arming a second time replaces the first, so a
    // mis-click is undone by clicking the dial you meant.
    void armLearn(int parameterId);
    void cancelLearn();
    int getLearnTarget() const { return learnTarget.load(); }
    bool isLearning() const { return learnTarget.load() != none; }

    // Bumped on every change, including the bind that happens on the audio
    // thread. The editor watches this rather than polling the whole map.
    uint32_t getVersion() const { return version.load(); }

    // "74:Cutoff,71:Reso" - by name, not by id, so a map written today still
    // reads correctly if the registry is ever renumbered. Names hold no commas
    // or colons; anything unrecognised is dropped rather than guessed at.
    juce::String toString() const;
    void fromString(const juce::String& text);

private:
    std::array<std::atomic<int>, numControllers> controllers;
    std::atomic<int> learnTarget{none};
    std::atomic<uint32_t> version{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMap)
};

// Drops note messages that arrived on another channel, leaving everything else
// alone. The other half of "which MIDI reaches what": the map above says which
// control drives which dial, this says which keys are yours - so a controller
// whose pads or keys you do not want can be silenced without also disconnecting
// its knobs, which pass on every channel regardless.
//
// channel 0 means every channel, and does nothing at all.
void filterNotesToChannel(juce::MidiBuffer& buffer, int channel);

// One note struck on the drum channel, and where in the block it landed.
struct DrumNote
{
    int note = 0;
    float velocity = 1.0f;
    int sampleOffset = 0;
};

// Splits the incoming notes between the kit and the synth.
//
// Takes the note-ons on drumChannel out of the buffer and returns them, then
// applies the keyboard's own channel filter to what is left. One function
// rather than two calls, because the ORDER matters and getting it wrong is
// silent: filtering first would throw the drum notes away before anything
// could look for them, and the kit would be mute with the setting apparently
// correct.
//
// drumChannel 0 takes nothing and leaves the buffer to the keyboard filter
// alone, which is exactly what this did before the kit could be played.
//
// A note on the drum channel is REMOVED whatever the keyboard channel says, so
// the two never layer. Setting both to the same number is then a way of saying
// "this channel is drums", not a way of playing both at once - which is the
// only reading that does not make a pad controller play a bass note under
// every kick.
//
// Note-OFFS on the drum channel are dropped rather than returned. A drum hit
// has no length: the kit is struck and rings for as long as its own decay
// says, and there is nothing for a release to do but confuse the voice that
// is already ringing.
std::vector<DrumNote> splitIncomingNotes(juce::MidiBuffer& buffer, int drumChannel,
                                         int keyboardChannel);

// Which note strikes which slot, for the slots that have been told otherwise.
//
// The twin of MidiMap above and deliberately shaped like it - learn, assign,
// clear, a version to watch and a line of text to save - because they answer
// the same question about two different kinds of message. That one says which
// knob moves which dial; this says which pad hits which drum.
//
// Keyed by SLOT rather than by note, which is the opposite of MidiMap's choice
// and is right for the opposite reason. A CC map is keyed by controller so
// that learning a second dial to CC 74 simply repoints slot 74; here the fixed
// thing is the twelve drums and the loose thing is the note, and a map keyed
// by note would be 128 entries to hold at most twelve answers.
//
// Empty by default, and an empty slot falls back to General MIDI. That is what
// keeps a GM drum file playing correctly on an untouched kit, and it is why
// this is an override layer rather than a table of twelve notes: GM FOLDS -
// its three floor and low toms all reach LoTom - and a slot reduced to one
// note each would quietly stop answering the other two.
class DrumNoteMap
{
public:
    static constexpr int none = -1;

    DrumNoteMap();

    // ---- Audio thread ------------------------------------------------------

    // The slot a note strikes, or none.
    //
    // Overrides are tried first and GM second, so learning a pad that GM
    // already spoke for moves it rather than being ignored - which is the only
    // behaviour that makes "learn whatever I need" true.
    //
    // `transpose` shifts the GM fallback ONLY. A learned note is the note that
    // was played to learn it, and shifting that afterwards would move a slot
    // away from the pad it was just taught.
    int voiceForNote(int note, int transpose) const;

    // Binds the armed slot to this note and returns true, having consumed it.
    //
    // The hit is swallowed rather than also played, the way the CC map's learn
    // does not also move the dial: a drum firing while you are assigning it is
    // noise at the exact moment you are listening for whether it worked.
    bool captureLearn(int note);

    // ---- Message thread ----------------------------------------------------

    // The note this slot was taught, or none when it is still on GM.
    int noteFor(int voice) const;

    // Teaches a slot a note, and takes that note off any other slot first.
    // One note striking two drums is a thing somebody would do by accident and
    // never on purpose, and MidiMap settled the same question the same way.
    void assign(int voice, int note);

    // Back to GM for this slot, or for all of them.
    void clearVoice(int voice);
    void clearAll();

    int countAssignments() const;

    // ---- Learning ----------------------------------------------------------
    void armLearn(int voice);
    void cancelLearn();
    int getLearnTarget() const { return learnTarget.load(); }
    bool isLearning() const { return learnTarget.load() != none; }

    // Bumped on every change, including the bind that happens on the audio
    // thread. The panel watches this rather than polling twelve slots.
    uint32_t getVersion() const { return version.load(); }

    // "Kick:60,Aux:38" - by slot NAME rather than by index, so a map written
    // today still reads correctly if the twelve rows are ever reordered. The
    // same reasoning as MidiMap writing parameter names rather than ids, and
    // the same handling of anything unrecognised: dropped, not guessed at.
    juce::String toString() const;
    void fromString(const juce::String& text);

private:
    std::array<std::atomic<int>, project::numDrumVoices> notes;
    std::atomic<int> learnTarget{none};
    std::atomic<uint32_t> version{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumNoteMap)
};

// The note to hit for a slot: what it was taught, or its GM primary shifted by
// the kit's transpose. What a strip prints, and the one place that knows the
// two sources have to be asked in that order.
int drumTriggerNote(const DrumNoteMap& map, int voice, int transpose);
} // namespace wavelathe
